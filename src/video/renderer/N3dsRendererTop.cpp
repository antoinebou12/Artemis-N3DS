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
    const bool stereo_sbs =
        global_presentation_state().mode ==
            PresentationMode::StereoSideBySide &&
        surface_width >= GSP_SCREEN_HEIGHT_TOP_2X;

    if (stereo_sbs) {
        // The host frame contains [left eye | right eye]. The base renderer
        // produces an 800-wide surface and its 3D copy path sends each 400px
        // half to the corresponding top-screen eye framebuffer.
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
