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

#include "../system/dispatcher.hpp"
#include "../graphics_lifecycle.hpp"
#include "../presentation_state.hpp"
#include "video.hpp"

#include <3ds.h>
#include <algorithm>
#include <stdio.h>

namespace {
void enable_top_magnify() {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = std::max(state.zoom, 2.0f);
    set_global_presentation_state(state);
}
} // namespace

VideoDecoderBase::VideoDecoderBase(int width, int height) {
    n3ds_graphics_acquire_stream();
    surface_height = GSP_SCREEN_WIDTH;
    surface_width = width > GSP_SCREEN_HEIGHT_TOP ? GSP_SCREEN_HEIGHT_TOP_2X
                                                  : GSP_SCREEN_HEIGHT_TOP;
    image_width = width > MOON_CTR_VIDEO_TEX_W ? MOON_CTR_VIDEO_TEX_W : width;
    image_height =
        height > MOON_CTR_VIDEO_TEX_H ? MOON_CTR_VIDEO_TEX_H : height;

    GSPGPU_FramebufferFormat px_fmt = gfxGetScreenFormat(GFX_TOP);
    pixel_size = gspGetBytesPerPixel(px_fmt);

    auto pDispatcher = MessageDispatcher::get_instance();
    pDispatcher->subscribe(MessageType::TOUCH_STATE_CHANGED, this);
    pDispatcher->subscribe(MessageType::KEYBOARD_STATE_CHANGED, this);
    pDispatcher->subscribe(MessageType::EXIT_STREAM, this);

    // Default GameStream helper: top = video, bottom = SELECT UI.
    _accept_touch_state_changed(N3dsTouchType::MENU_TOUCH);
}

VideoDecoderBase::~VideoDecoderBase() {
    renderer.reset();
    auto pDispatcher = MessageDispatcher::get_instance();
    pDispatcher->unsubscribe(MessageType::TOUCH_STATE_CHANGED, this);
    pDispatcher->unsubscribe(MessageType::KEYBOARD_STATE_CHANGED, this);
    pDispatcher->unsubscribe(MessageType::EXIT_STREAM, this);
}

void VideoDecoderBase::accept(IMessage *msg) {
    switch (msg->getMessageType()) {
    case MessageType::TOUCH_STATE_CHANGED: {
        auto touch_msg = static_cast<TouchStateChangedMsg *>(msg);
        _accept_touch_state_changed(touch_msg->ttype);
        break;
    }
    case MessageType::KEYBOARD_STATE_CHANGED:
        _accept_keyboard_state_changed(
            static_cast<KeyboardStateChangedMsg *>(msg));
        break;
    case MessageType::EXIT_STREAM:
        n3ds_stream_render_abort();
        renderer_lock.lock();
        renderer.reset();
        bottom_overlay_active = false;
        renderer_lock.unlock();
        break;
    default:
        break;
    }
}

void VideoDecoderBase::_accept_touch_state_changed(N3dsTouchType ttype) {
    renderer_lock.lock();

    // SELECT hub re-entry: keep the existing top-only renderer so video does
    // not flicker while the bottom UI repaints.
    const bool top_only =
        ttype == N3dsTouchType::MENU_TOUCH ||
        ttype == N3dsTouchType::PERFORMANCE_TOUCH ||
        ttype == N3dsTouchType::MAGNIFY_TOUCH ||
        ttype == N3dsTouchType::ABSOLUTE_TOUCH ||
        ttype == N3dsTouchType::DS_TOUCH ||
        ttype == N3dsTouchType::GAMEPAD ||
        ttype == N3dsTouchType::MOUSEPAD ||
        ttype == N3dsTouchType::KEYBOARD;
    if (top_only && active_touch_type == ttype && renderer != nullptr &&
        !bottom_overlay_active) {
        if (ttype == N3dsTouchType::MAGNIFY_TOUCH) {
            enable_top_magnify();
        }
        renderer_lock.unlock();
        return;
    }

    // Switching between top-only software UIs: reuse the same renderer.
    if (top_only && !bottom_overlay_active && renderer != nullptr &&
        (active_touch_type == N3dsTouchType::MENU_TOUCH ||
         active_touch_type == N3dsTouchType::PERFORMANCE_TOUCH ||
         active_touch_type == N3dsTouchType::MAGNIFY_TOUCH ||
         active_touch_type == N3dsTouchType::ABSOLUTE_TOUCH ||
         active_touch_type == N3dsTouchType::DS_TOUCH ||
         active_touch_type == N3dsTouchType::GAMEPAD ||
         active_touch_type == N3dsTouchType::MOUSEPAD ||
         active_touch_type == N3dsTouchType::KEYBOARD)) {
        if (ttype == N3dsTouchType::MAGNIFY_TOUCH) {
            enable_top_magnify();
        }
        active_touch_type = ttype;
        bottom_overlay_active = false;
        renderer_lock.unlock();
        return;
    }

    bottom_overlay_active = false;
    active_touch_type = ttype;
    switch (ttype) {
    case (N3dsTouchType::DEBUG_TOUCH):
        renderer = std::make_unique<N3dsRendererNormal>(
            surface_width, surface_height, image_width, image_height,
            pixel_size, true);
        break;
    case (N3dsTouchType::GAMEPAD):
    case (N3dsTouchType::MOUSEPAD):
    case (N3dsTouchType::KEYBOARD):
    case (N3dsTouchType::ABSOLUTE_TOUCH):
    case (N3dsTouchType::DS_TOUCH):
    case (N3dsTouchType::MENU_TOUCH):
    case (N3dsTouchType::PERFORMANCE_TOUCH):
        // Software bottom UI; video stays on the top screen only.
        renderer = std::make_unique<N3dsRendererNormal>(
            surface_width, surface_height, image_width, image_height,
            pixel_size, false, false);
        break;
    case (N3dsTouchType::MAGNIFY_TOUCH):
        enable_top_magnify();
        renderer = std::make_unique<N3dsRendererNormal>(
            surface_width, surface_height, image_width, image_height,
            pixel_size, false, false);
        break;
    default:
        renderer = std::make_unique<N3dsRendererMock>();
        break;
    }
    renderer_lock.unlock();
}

void VideoDecoderBase::_accept_keyboard_state_changed(
    KeyboardStateChangedMsg *msg) {
    renderer_lock.lock();
    if (bottom_overlay_active && renderer != nullptr) {
        (static_cast<N3dsRendererNormal *>(renderer.get()))
            ->set_bottom_screen(msg->keyboard_image, msg->key_offset,
                                msg->key_size);
    }
    renderer_lock.unlock();
}
