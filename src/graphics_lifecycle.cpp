#include "graphics_lifecycle.hpp"

#include <3ds.h>

namespace {
GraphicsLifecycleState g_state;
C3D_RenderTarget *g_top_target = nullptr;
C3D_RenderTarget *g_bottom_target = nullptr;
C2D_TextBuf g_text_buffer = nullptr;
bool g_c3d_initialized = false;
bool g_c2d_initialized = false;

void wait_gpu_idle() {
    gspWaitForP3D();
    gspWaitForPPF();
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

    release_shell_resources();
    wait_gpu_idle();
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        configure_stream_framebuffers();
        g_state.acquire_stream();
        return false;
    }
    g_c3d_initialized = true;

    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        release_shell_resources();
        configure_stream_framebuffers();
        g_state.acquire_stream();
        return false;
    }
    g_c2d_initialized = true;

    C2D_Prepare();
    g_top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buffer = C2D_TextBufNew(8192);
    if (g_top_target == nullptr || g_bottom_target == nullptr ||
        g_text_buffer == nullptr) {
        release_shell_resources();
        configure_stream_framebuffers();
        g_state.acquire_stream();
        return false;
    }

    g_state.acquire_shell();
    return true;
}

void n3ds_graphics_acquire_stream() {
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
