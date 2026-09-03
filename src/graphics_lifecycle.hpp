#pragma once

#include "graphics_lifecycle_state.hpp"

#include <citro2d.h>
#include <citro3d.h>

// Acquires the BGR8 Citro2D shell. Targets and the text buffer stay valid
// until the stream client acquires RGB565 or the application shuts down.
bool n3ds_graphics_acquire_shell();

// Releases shell resources, then configures both LCDs for the legacy RGB565
// stream renderer. It is safe to call repeatedly.
void n3ds_graphics_acquire_stream();

// Tear down stream GPU state and clear both screens before re-entering the
// Citro2D shell. Call after LiStopConnection() and before n3ds_ui_init().
void n3ds_graphics_reset_after_stream();

// Releases all graphics resources. Call before gfxExit().
void n3ds_graphics_shutdown();

bool n3ds_graphics_shell_active();
C3D_RenderTarget *n3ds_graphics_top_target();
C3D_RenderTarget *n3ds_graphics_bottom_target();
C2D_TextBuf n3ds_graphics_text_buffer();
