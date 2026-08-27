/*
 * This file is part of Moonlight Embedded.
 *
 * Based on Moonlight Pc implementation
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "video.hpp"
#include "../stream_telemetry.hpp"
#include "../stream_telemetry_store.hpp"

#include <3ds.h>

#include <Limelight.h>
#include <libavcodec/avcodec.h>

#include <memory>
#include <pthread.h>
#include <stdbool.h>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>

static std::unique_ptr<MvdDecoder> instance = nullptr;

static inline float ticks_to_ms(u64 ticks) {
    return (static_cast<float>(ticks) * 1000.0f) /
           static_cast<float>(SYSCLOCK_ARM11);
}

MvdDecoder::MvdDecoder(int videoFormat, int width, int height, int redrawRate,
                       void *context, int drFlags)
    : VideoDecoderBase(width, height) {
    bool is_new_3ds;
    APT_CheckNew3DS(&is_new_3ds);
    if (!is_new_3ds) {
        fprintf(stderr, "Hardware decoding is only available on the New 3DS\n");
        throw std::runtime_error("Unsupported hardware");
    }

    MVDSTD_CalculateWorkBufSizeConfig config = {
        0,
    };
    config.level.enable = true;
    config.level.flag = (MVD_CALC_WITH_LEVEL_FLAG_ENABLE_CALC |
                         MVD_CALC_WITH_LEVEL_FLAG_ENABLE_EXTRA_OP |
                         MVD_CALC_WITH_LEVEL_FLAG_UNK);
    config.level.level = MVD_H264_LEVEL_4_2;
    config.width = width;
    config.height = height;
    uint32_t size = 0;
    int status = mvdstdCalculateBufferSize(&config, &size);
    if (status) {
        fprintf(stderr, "mvdstdCalculateBufferSize failed: %d\n", status);
        throw std::runtime_error("mvdstdCalculateBufferSize failed");
    }

    first_frame = true;
    last_present_ticks = 0;
    reset_global_stream_telemetry();
    status = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264,
                        MVD_OUTPUT_BGR565, size, NULL);
    if (status) {
        fprintf(stderr, "mvdstdInit failed: %d\n", status);
        mvdstdExit();
        throw std::runtime_error("mvdstdInit failed");
    }

    const int texture_width = moon_video_texture_width(image_width);
    const int texture_height = moon_video_texture_height(image_height);
    rgb_img_buffer =
        (u8 *)linearAlloc(texture_width * texture_height * pixel_size);
    if (!rgb_img_buffer) {
        fprintf(stderr, "Out of memory!\n");
        throw std::runtime_error("Out of memory");
    }

    status = ensure_linear_buf_size(&nal_unit_buffer, &nal_unit_buffer_size,
                                    INITIAL_DECODER_BUFFER_SIZE +
                                        AV_INPUT_BUFFER_PADDING_SIZE);
    if (status) {
        fprintf(stderr, "Out of linear memory!\n");
        throw std::runtime_error("Out of linear memory");
    }
    mvdstdGenerateDefaultConfig(&mvdstd_config, width, height, image_width,
                                image_height, NULL, (u32 *)rgb_img_buffer,
                                NULL);

    // Keep MVD output stride exactly aligned with the renderer's power-of-two
    // texture. 400x240 therefore uses 512x256 instead of 1024x512.
    mvdstd_config.flag_x104 = 1;
    mvdstd_config.output_width_override = texture_width;
    mvdstd_config.output_height_override = texture_height;
    MVDSTD_SetConfig(&mvdstd_config);
}

MvdDecoder::~MvdDecoder() {
    mvdstdExit();
    linearFree(nal_unit_buffer);
    linearFree(rgb_img_buffer);
    printf("Video decoder shutdown successfully\n");
}

DecodeReturnStatus MvdDecoder::_decode(unsigned char *indata, int inlen) {
    int ret = mvdstdProcessVideoFrame(indata, inlen, 1, NULL);
    if (!MVD_CHECKNALUPROC_SUCCESS(ret)) {
        return DecodeReturnStatus::ERROR;
    }

    if (ret != MVD_STATUS_PARAMSET && ret != MVD_STATUS_INCOMPLETEPROCESSING) {
        return DecodeReturnStatus::NO_FRAME_PRODUCED;
    }
    ret = mvdstdRenderVideoFrame(&mvdstd_config, true);
    if (ret != MVD_STATUS_OK) {
        return DecodeReturnStatus::ERROR;
    }
    return DecodeReturnStatus::SUCCESS;
}

int MvdDecoder::submit_decode_unit(PDECODE_UNIT decodeUnit) {
    const u64 decode_start_ticks = svcGetSystemTick();
    PLENTRY entry = decodeUnit->bufferList;
    int length = 0;

    if (ensure_linear_buf_size(&nal_unit_buffer, &nal_unit_buffer_size,
                               decodeUnit->fullLength +
                                   AV_INPUT_BUFFER_PADDING_SIZE)) {
        printf("Out of linear memory!\n");
        return DR_OK;
    }

    while (entry != NULL) {
        memcpy(nal_unit_buffer + length, entry->data, entry->length);
        length += entry->length;
        entry = entry->next;
    }
    GSPGPU_FlushDataCache(nal_unit_buffer, length);

    const DecodeReturnStatus decode_status =
        _decode((unsigned char *)nal_unit_buffer, length);
    const u64 decode_end_ticks = svcGetSystemTick();

    // Do not push the same RGB buffer through PICA when MVD only consumed
    // parameter/incomplete data and produced no new frame.
    if (decode_status == DecodeReturnStatus::NO_FRAME_PRODUCED) {
        return DR_OK;
    }
    if (decode_status == DecodeReturnStatus::ERROR) {
        // Recover quickly from corrupted/missing references instead of
        // presenting stale video indefinitely.
        return DR_NEED_IDR;
    }

    renderer_lock.lock();
    renderer->set_perf_decode_ticks(decode_end_ticks - decode_start_ticks);
    const u64 render_start_ticks = svcGetSystemTick();
    renderer->write_px_to_framebuffer(rgb_img_buffer);
    const u64 present_ticks = svcGetSystemTick();
    renderer_lock.unlock();

    StreamTelemetrySample sample{};
    sample.decode_ms = ticks_to_ms(decode_end_ticks - decode_start_ticks);
    sample.render_ms = ticks_to_ms(present_ticks - render_start_ticks);
    if (last_present_ticks != 0) {
        sample.frame_ms = ticks_to_ms(present_ticks - last_present_ticks);
        if (sample.frame_ms > 0.0f) {
            sample.fps = 1000.0f / sample.frame_ms;
        }
    }
    push_global_stream_telemetry(sample);
    last_present_ticks = present_ticks;

    // Ask for an IDR immediately after the first presented frame to avoid the
    // MVD gray-output failure mode when joining mid-GOP.
    if (first_frame) {
        first_frame = false;
        return DR_NEED_IDR;
    }
    return DR_OK;
}

static int n3ds_init(int videoFormat, int width, int height, int redrawRate,
                     void *context, int drFlags) {
    try {
        instance = std::make_unique<MvdDecoder>(videoFormat, width, height,
                                                redrawRate, context, drFlags);
        return 0;
    } catch (const std::exception &e) {
        fprintf(stderr, "Failed to initialize N3DS MVD decoder: %s\n",
                e.what());
        return -1;
    }
}

static void n3ds_destroy() { instance = nullptr; }

static int n3ds_submit_decode_unit(PDECODE_UNIT decodeUnit) {
    if (instance == nullptr) {
        return DR_OK;
    }
    return instance->submit_decode_unit(decodeUnit);
}

DECODER_RENDERER_CALLBACKS decoder_callbacks_n3ds_mvd = {
    .setup = n3ds_init,
    .cleanup = n3ds_destroy,
    .submitDecodeUnit = n3ds_submit_decode_unit,
    .capabilities = CAPABILITY_DIRECT_SUBMIT,
};
