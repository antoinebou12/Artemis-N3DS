#pragma once

// Citro2D / legacy Moonlight framebuffer bridge.
//
// The existing streaming renderer writes RGB565 directly to the LCD
// framebuffers, while upstream Citro2D is designed around the normal 3DS BGR8
// framebuffer path and emits GX_TRANSFER_FMT_RGB8 from C2D_CreateScreenTarget().
// Trying to force Citro2D's output transfer to RGB565 produced the striped /
// cyan-corrupted shell seen on hardware.
//
// Keep Citro2D completely upstream-compatible while the shell is active, then
// restore RGB565 when Citro2D is torn down so the legacy PICA200 video renderer
// continues to receive the framebuffer format it expects.

#include <3ds.h>
#include_next <citro2d.h>

static inline bool artemisC2DInit(size_t maxObjects) {
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);

    const bool ok = C2D_Init(maxObjects);
    if (!ok) {
        // A failed shell init must not leave the application in BGR8 because
        // streaming may still fall back to the legacy RGB565 renderer.
        gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
        gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    }
    return ok;
}

static inline void artemisC2DFini() {
    C2D_Fini();

    // The video renderer's PICA/GX path and framebuffer clears use RGB565.
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
}

// n3ds_ui.cpp includes <citro2d.h>. Since src/ is the first project include
// directory, this shim is selected first and include_next loads the real
// Citro2D API. Only lifecycle calls are redirected; screen-target creation and
// shaders remain exactly as upstream Citro2D expects.
#define C2D_Init(maxObjects) artemisC2DInit((maxObjects))
#define C2D_Fini() artemisC2DFini()
