#pragma once

// Compatibility shim for the 3DS shell renderer.
//
// This project initializes both LCD framebuffers as GSP_RGB565_OES. Upstream
// Citro2D's C2D_CreateScreenTarget() always configures its display transfer as
// GX_TRANSFER_FMT_RGB8, which makes the PICA output byte layout disagree with
// the RGB565 framebuffer layout. On hardware that shows up as repeated cyan /
// horizontal stripes and mangled text/rectangles.
//
// Keep the application's RGB565 framebuffer contract (also used by the legacy
// Moonlight renderer) and only override the screen-target helper so the GPU
// display transfer writes RGB565 bytes.

#include <3ds.h>
#include_next <citro2d.h>

static inline C3D_RenderTarget *artemisC2DCreateScreenTarget(
    gfxScreen_t screen, gfx3dSide_t side) {
    int height;
    switch (screen) {
    default:
    case GFX_BOTTOM:
        height = GSP_SCREEN_HEIGHT_BOTTOM;
        break;
    case GFX_TOP:
        height = !gfxIsWide() ? GSP_SCREEN_HEIGHT_TOP
                              : GSP_SCREEN_HEIGHT_TOP_2X;
        break;
    }

    C3D_RenderTarget *target = C3D_RenderTargetCreate(
        GSP_SCREEN_WIDTH, height, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (target != nullptr) {
        C3D_RenderTargetSetOutput(
            target, screen, side,
            GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                GX_TRANSFER_RAW_COPY(0) |
                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
                GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    }
    return target;
}

// n3ds_ui.cpp includes <citro2d.h>. Because src/ is the first project include
// directory, this shim is selected first and then include_next loads Citro2D's
// real public API. Redirect only screen-target creation; every other Citro2D
// call remains untouched.
#define C2D_CreateScreenTarget(screen, side) \
    artemisC2DCreateScreenTarget((screen), (side))
