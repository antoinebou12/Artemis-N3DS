#include "presentation_state.hpp"
#include "stream_telemetry.hpp"
#include "video/video_layout.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

static bool nearly_equal(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) <= epsilon;
}

int main() {
    PresentationState presentation;
    presentation.zoom = 8.0f;
    presentation.pan_x = -2.0f;
    presentation.pan_y = 2.0f;
    normalize_presentation_state(presentation);

    assert(nearly_equal(presentation.zoom, 4.0f));
    assert(nearly_equal(presentation.pan_x, -1.0f));
    assert(nearly_equal(presentation.pan_y, 1.0f));
    assert(std::strcmp(presentation_mode_name(PresentationMode::StereoSideBySide),
                       "Stereo SBS") == 0);

    PresentationState fit;
    fit.mode = PresentationMode::Fit;
    const auto fit_geometry =
        compute_presentation_geometry(1280, 720, 400, 240, fit);
    assert(nearly_equal(fit_geometry.destination_scale_x, 1.0f));
    assert(nearly_equal(fit_geometry.destination_scale_y, 0.9375f));
    assert(nearly_equal(fit_geometry.source_u_min, 0.0f));
    assert(nearly_equal(fit_geometry.source_u_max, 1.0f));

    PresentationState fill;
    fill.mode = PresentationMode::Fill;
    const auto fill_geometry =
        compute_presentation_geometry(1280, 720, 400, 240, fill);
    assert(nearly_equal(fill_geometry.destination_scale_x, 1.0f));
    assert(nearly_equal(fill_geometry.destination_scale_y, 1.0f));
    assert(nearly_equal(fill_geometry.source_u_min, 0.03125f));
    assert(nearly_equal(fill_geometry.source_u_max, 0.96875f));

    PresentationState portrait_fit;
    portrait_fit.mode = PresentationMode::Fit;
    const auto portrait_fit_geometry =
        compute_presentation_geometry(400, 800, 400, 240, portrait_fit);
    assert(nearly_equal(portrait_fit_geometry.destination_scale_x, 0.3f));
    assert(nearly_equal(portrait_fit_geometry.destination_scale_y, 1.0f));

    PresentationState portrait_fill;
    portrait_fill.mode = PresentationMode::Fill;
    const auto portrait_fill_geometry =
        compute_presentation_geometry(400, 800, 400, 240, portrait_fill);
    assert(nearly_equal(portrait_fill_geometry.source_v_min, 0.35f));
    assert(nearly_equal(portrait_fill_geometry.source_v_max, 0.65f));

    PresentationState magnify;
    magnify.mode = PresentationMode::Magnify;
    magnify.zoom = 2.0f;
    const auto magnify_geometry =
        compute_presentation_geometry(800, 480, 400, 240, magnify);
    assert(nearly_equal(magnify_geometry.source_u_min, 0.25f));
    assert(nearly_equal(magnify_geometry.source_u_max, 0.75f));
    assert(nearly_equal(magnify_geometry.source_v_min, 0.25f));
    assert(nearly_equal(magnify_geometry.source_v_max, 0.75f));

    magnify.pan_x = 1.0f;
    magnify.pan_y = -1.0f;
    const auto panned_magnify_geometry =
        compute_presentation_geometry(800, 480, 400, 240, magnify);
    assert(nearly_equal(panned_magnify_geometry.source_u_min, 0.5f));
    assert(nearly_equal(panned_magnify_geometry.source_u_max, 1.0f));
    assert(nearly_equal(panned_magnify_geometry.source_v_min, 0.0f));
    assert(nearly_equal(panned_magnify_geometry.source_v_max, 0.5f));

    const auto invalid_geometry =
        compute_presentation_geometry(0, 480, 400, 240, magnify);
    assert(nearly_equal(invalid_geometry.destination_scale_x, 1.0f));
    assert(nearly_equal(invalid_geometry.source_u_min, 0.0f));

    // Adaptive video backing surfaces keep PICA's power-of-two requirement but
    // stop moving a 1024x512 texture for every small 3DS stream.
    assert(moon_video_texture_width(400) == 512);
    assert(moon_video_resolution_is_supported(400, 240));
    assert(moon_video_resolution_is_supported(800, 240));
    assert(moon_video_resolution_is_supported(800, 480));
    assert(moon_video_resolution_is_supported(1024, 512));
    assert(!moon_video_resolution_is_supported(0, 240));
    assert(!moon_video_resolution_is_supported(1025, 512));
    assert(!moon_video_resolution_is_supported(1024, 513));
    assert(moon_video_texture_width(512) == 512);
    assert(moon_video_texture_width(513) == 1024);
    assert(moon_video_texture_height(240) == 256);
    assert(moon_video_texture_height(256) == 256);
    assert(moon_video_texture_height(257) == 512);
    assert(moon_video_texture_width(800) == 1024);
    assert(moon_video_texture_height(240) == 256);
    assert(moon_video_texture_height(480) == 512);
    assert(moon_video_texture_bytes(400, 240, 2) == 512 * 256 * 2);
    assert(moon_video_texture_bytes(800, 240, 2) == 1024 * 256 * 2);
    assert(moon_video_texture_bytes(800, 480, 2) == 1024 * 512 * 2);

    StreamTelemetry telemetry;
    telemetry.push({4.0f, 2.0f, 16.0f, 60.0f, 1500, 0});
    telemetry.push({6.0f, 4.0f, 20.0f, 50.0f, 1500, 1});

    const auto summary = telemetry.summary();
    assert(summary.sample_count == 2);
    assert(nearly_equal(summary.avg_decode_ms, 5.0f));
    assert(nearly_equal(summary.avg_render_ms, 3.0f));
    assert(nearly_equal(summary.avg_frame_ms, 18.0f));
    assert(nearly_equal(summary.avg_fps, 55.0f));
    assert(nearly_equal(summary.max_frame_ms, 20.0f));
    assert(summary.bitrate_kbps == 1500);
    assert(summary.dropped_frames == 1);

    StreamTelemetrySample copied[2]{};
    assert(telemetry.copy_samples(copied, 2) == 2);
    assert(nearly_equal(copied[0].decode_ms, 4.0f));
    assert(nearly_equal(copied[1].decode_ms, 6.0f));

    StreamTelemetry ring_buffer;
    for (std::size_t i = 0; i < StreamTelemetry::kCapacity + 2; ++i) {
        ring_buffer.push({static_cast<float>(i), 0.0f, 0.0f, 0.0f,
                          static_cast<std::uint32_t>(i),
                          static_cast<std::uint32_t>(i)});
    }
    assert(ring_buffer.size() == StreamTelemetry::kCapacity);
    StreamTelemetrySample ring_samples[StreamTelemetry::kCapacity]{};
    assert(ring_buffer.copy_samples(ring_samples, StreamTelemetry::kCapacity) ==
           StreamTelemetry::kCapacity);
    assert(nearly_equal(ring_samples[0].decode_ms, 2.0f));
    assert(nearly_equal(ring_samples[StreamTelemetry::kCapacity - 1].decode_ms,
                        static_cast<float>(StreamTelemetry::kCapacity + 1)));
    const auto ring_summary = ring_buffer.summary();
    assert(ring_summary.bitrate_kbps == StreamTelemetry::kCapacity + 1);
    assert(ring_summary.dropped_frames == StreamTelemetry::kCapacity + 1);
    assert(ring_buffer.copy_samples(nullptr, 1) == 0);
    assert(ring_buffer.copy_samples(ring_samples, 0) == 0);

    telemetry.reset();
    assert(telemetry.size() == 0);

    return 0;
}
