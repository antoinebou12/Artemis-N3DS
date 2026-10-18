/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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

#include "N3dsRenderer.hpp"
#include "../../presentation_state.hpp"

#include <3ds.h>
#include <cstdlib>
#include <cstring>
#include <stdbool.h>
#include <stdexcept>
#include <unistd.h>

N3dsRendererTop::N3dsRendererTop(int dest_width, int dest_height, int src_width,
                                 int src_height, int px_size, bool debug_in)
    : N3dsRendererBase(GFX_TOP, dest_width, dest_height, src_width, src_height,
                       px_size, debug_in) {}

void N3dsRendererTop::write_px_to_framebuffer(uint8_t *source) {
    const PresentationState presentation = global_presentation_state();
    const bool wide_surface = surface_width >= GSP_SCREEN_HEIGHT_TOP_2X;

    // The host frame contains [left eye | right eye]. The base renderer
    // produces an 800-wide surface and its 3D copy path sends each 400px half
    // to the corresponding top-screen eye framebuffer.
    const bool stereo_sbs =
        presentation.mode == PresentationMode::StereoSideBySide && wide_surface;
    // Depth built from a flat stream needs that same two-eye plumbing.
    const bool stereo_depth =
        presentation.stereo_depth != StereoDepth::Off && wide_surface;
    // A narrow stream only has the wide surface because stereo asked for it.
    // Leaving 3D would put the top screen into 800px wide mode and overrun the
    // 400-wide eye framebuffer, so stay in 3D and let a zero offset stand in
    // for 2D: both eyes then receive an identical picture.
    const bool stereo_only_surface =
        wide_surface && image_width <= GSP_SCREEN_HEIGHT_TOP;

    if (stereo_sbs || stereo_depth || stereo_only_surface) {
        ensure_3d_enabled();
    } else {
        // 800-wide 2D streams use wide mode but must not become stereoscopic
        // merely because the physical 3D slider is raised.
        ensure_3d_disabled();
    }
    write_px_to_framebuffer_gpu(source);
}

void N3dsRendererTop::set_perf_decode_ticks(u64 ticks) {
    perf_decode_ticks = ticks;
}
