#include "graphics_lifecycle.hpp"

#include <3ds.h>
#include <stdio.h>
#include <cstring>

namespace {
GraphicsLifecycleState g_state;
C3D_RenderTarget *g_top_target = nullptr;
C3D_RenderTarget *g_bottom_target = nullptr;
C2D_TextBuf g_text_buffer = nullptr;
bool g_c3d_initialized = false;
bool g_c2d_initialized = false;

void wait_gpu_idle() {
    if (!g_c3d_initialized && !g_c2d_initialized) {
        return;
    }
    gspWaitForP3D();
    gspWaitForPPF();
}

void wait_gpu_idle_unconditional() {
    gspWaitForP3D();
    gspWaitForPPF();
    for (int i = 0; i < 3; ++i) {
        gspWaitForVBlank();
    }
}

void clear_gfx_screen(gfxScreen_t screen, u16 rgb565_color) {
    const int width = GSP_SCREEN_WIDTH;
    const int height = screen == GFX_TOP ? GSP_SCREEN_HEIGHT_TOP
                                         : GSP_SCREEN_HEIGHT_BOTTOM;
    u8 *framebuffer = gfxGetFramebuffer(screen, GFX_LEFT, nullptr, nullptr);
    if (framebuffer == nullptr) {
        return;
    }

    const int pixel_size = gspGetBytesPerPixel(gfxGetScreenFormat(screen));
    if (pixel_size == 2) {
        u16 *pixels = reinterpret_cast<u16 *>(framebuffer);
        for (int i = 0; i < width * height; ++i) {
            pixels[i] = rgb565_color;
        }
    } else if (pixel_size == 3) {
        const u8 red = static_cast<u8>((rgb565_color >> 11) & 0x1F) << 3;
        const u8 green =
            static_cast<u8>((rgb565_color >> 5) & 0x3F) << 2;
        const u8 blue = static_cast<u8>(rgb565_color & 0x1F) << 3;
        for (int y = 0; y < height; ++y) {
            u8 *row = framebuffer + y * width * 3;
            for (int x = 0; x < width; ++x) {
                row[x * 3 + 0] = blue;
                row[x * 3 + 1] = green;
                row[x * 3 + 2] = red;
            }
        }
    } else {
        std::memset(framebuffer, 0, width * height * pixel_size);
    }

    gfxScreenSwapBuffers(screen, true);
}

void reset_stream_gfx_state() {
    printf("[gfx] Waiting for stream GPU work to finish\n");
    wait_gpu_idle_unconditional();

    if (gfxIs3D()) {
        printf("[gfx] Disabling stereoscopic output\n");
        gfxSet3D(false);
    }
    gfxSetWide(false);

    const u16 background = RGB565(13, 17, 23);
    printf("[gfx] Clearing top and bottom framebuffers\n");
    clear_gfx_screen(GFX_TOP, background);
    clear_gfx_screen(GFX_BOTTOM, background);
    gfxFlushBuffers();
    gspWaitForVBlank();
}

void release_shell_resources() {
    wait_gpu_idle();
    if (g_text_buffer != nullptr) {
        C2D_TextBufDelete(g_text_buffer);
        g_text_buffer = nullptr;
    }
    if (g_top_target != nullptr) {
        C3D_RenderTargetDelete(g_top_target);
        g_top_target = nullptr;
    }
    if (g_bottom_target != nullptr) {
        C3D_RenderTargetDelete(g_bottom_target);
        g_bottom_target = nullptr;
    }
    if (g_c2d_initialized) {
        C2D_Fini();
        g_c2d_initialized = false;
    }
    if (g_c3d_initialized) {
        C3D_Fini();
        g_c3d_initialized = false;
    }
}

void configure_stream_framebuffers() {
    wait_gpu_idle();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
}
} // namespace

bool n3ds_graphics_acquire_shell() {
    if (g_state.shell_active()) {
        return true;
    }

    if (g_state.mode() == GraphicsMode::Stream) {
        reset_stream_gfx_state();
        g_state.finish_stream();
    }

    release_shell_resources();
    wait_gpu_idle_unconditional();
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        printf("[gfx] ERROR: C3D_Init failed while restoring shell\n");
        configure_stream_framebuffers();
        g_state.acquire_stream();
        return false;
    }
    g_c3d_initialized = true;

    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        printf("[gfx] ERROR: C2D_Init failed while restoring shell\n");
        release_shell_resources();
        configure_stream_framebuffers();
        g_state.acquire_stream();
        return false;
    }
    g_c2d_initialized = true;

    C2D_Prepare();
    g_top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buffer = C2D_TextBufNew(16384);
    if (g_top_target == nullptr || g_bottom_target == nullptr ||
        g_text_buffer == nullptr) {
        printf("[gfx] ERROR: Failed to allocate Citro2D shell targets\n");
        release_shell_resources();
        configure_stream_framebuffers();
        g_state.acquire_stream();
        return false;
    }

    g_state.acquire_shell();
    printf("[gfx] Artemis shell graphics ready\n");
    return true;
}

void n3ds_graphics_reset_after_stream() {
    if (g_state.mode() != GraphicsMode::Stream) {
        return;
    }
    reset_stream_gfx_state();
    g_state.finish_stream();
}

void n3ds_graphics_acquire_stream() {
    if (g_state.mode() == GraphicsMode::Stream) {
        return;
    }
    printf("[gfx] Handing GPU to stream renderer\n");
    release_shell_resources();
    configure_stream_framebuffers();
    g_state.acquire_stream();
}

void n3ds_graphics_shutdown() {
    release_shell_resources();
    g_state.shutdown();
}

bool n3ds_graphics_shell_active() { return g_state.shell_active(); }

C3D_RenderTarget *n3ds_graphics_top_target() { return g_top_target; }

C3D_RenderTarget *n3ds_graphics_bottom_target() { return g_bottom_target; }

C2D_TextBuf n3ds_graphics_text_buffer() { return g_text_buffer; }
