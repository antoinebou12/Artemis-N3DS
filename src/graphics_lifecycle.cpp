#include "graphics_lifecycle.hpp"

#include <3ds.h>
#include <atomic>
#include <stdio.h>
#include <cstring>

namespace {
GraphicsLifecycleState g_state;
C3D_RenderTarget *g_top_target = nullptr;
C3D_RenderTarget *g_bottom_target = nullptr;
C2D_TextBuf g_text_buffer = nullptr;
bool g_c3d_initialized = false;
bool g_c2d_initialized = false;
std::atomic<bool> g_stream_render_active{true};

void wait_vblanks(int count) {
    for (int i = 0; i < count; ++i) {
        gspWaitForVBlank();
    }
}

void wait_gpu_idle() {
    if (!g_c3d_initialized && !g_c2d_initialized) {
        return;
    }
    // Never wait on P3D/PPF here: a missed GPU event hangs QUIT forever.
    wait_vblanks(2);
}

void clear_gfx_screen(gfxScreen_t screen) {
    u8 *framebuffer = gfxGetFramebuffer(screen, GFX_LEFT, nullptr, nullptr);
    if (framebuffer == nullptr) {
        return;
    }
    const int width = GSP_SCREEN_WIDTH;
    const int height = screen == GFX_TOP ? GSP_SCREEN_HEIGHT_TOP
                                         : GSP_SCREEN_HEIGHT_BOTTOM;
    const int pixel_size = gspGetBytesPerPixel(gfxGetScreenFormat(screen));
    std::memset(framebuffer, 0, width * height * pixel_size);
    gfxScreenSwapBuffers(screen, false);
}

void reset_stream_gfx_state() {
    if (gfxIs3D()) {
        gfxSet3D(false);
    }
    gfxSetWide(false);
    wait_vblanks(2);
    clear_gfx_screen(GFX_TOP);
    clear_gfx_screen(GFX_BOTTOM);
    gfxFlushBuffers();
    wait_vblanks(1);
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
    // Top needs RGB565 for the video path. Bottom stays BGR8 (same as the
    // Citro2D shell) so software helper UI never fights a half-committed
    // RGB565↔BGR8 LCD mode (green scramble / "compressed line").
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    // Format/DB changes only take effect on present.
    clear_gfx_screen(GFX_TOP);
    clear_gfx_screen(GFX_BOTTOM);
    wait_vblanks(1);
}

bool try_init_shell_targets() {
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        return false;
    }
    g_c3d_initialized = true;

    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        release_shell_resources();
        return false;
    }
    g_c2d_initialized = true;

    C2D_Prepare();
    g_top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buffer = C2D_TextBufNew(16384);
    if (g_top_target == nullptr || g_bottom_target == nullptr ||
        g_text_buffer == nullptr) {
        release_shell_resources();
        return false;
    }
    return true;
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
    wait_vblanks(2);
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    if (!try_init_shell_targets()) {
        printf("[gfx] ERROR: Citro2D shell restore failed\n");
        configure_stream_framebuffers();
        g_state.acquire_stream();
        return false;
    }

    g_state.acquire_shell();
    return true;
}

void n3ds_graphics_reset_after_stream() {
    aptSetHomeAllowed(true);
    n3ds_stream_render_abort();
    if (g_state.mode() != GraphicsMode::Stream) {
        return;
    }
    reset_stream_gfx_state();
    g_state.finish_stream();
}

void n3ds_graphics_acquire_stream() {
    if (g_state.mode() == GraphicsMode::Stream) {
        g_stream_render_active.store(true);
        return;
    }
    release_shell_resources();
    configure_stream_framebuffers();
    g_stream_render_active.store(true);
    g_state.acquire_stream();
}

void n3ds_stream_render_abort() { g_stream_render_active.store(false); }

bool n3ds_stream_render_active() { return g_stream_render_active.load(); }

void n3ds_graphics_shutdown() {
    n3ds_stream_render_abort();
    release_shell_resources();
    g_state.shutdown();
}

bool n3ds_graphics_shell_active() { return g_state.shell_active(); }

C3D_RenderTarget *n3ds_graphics_top_target() { return g_top_target; }

C3D_RenderTarget *n3ds_graphics_bottom_target() { return g_bottom_target; }

C2D_TextBuf n3ds_graphics_text_buffer() { return g_text_buffer; }
