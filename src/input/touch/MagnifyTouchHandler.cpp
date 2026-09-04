/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
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

#include "../../config.hpp"
#include "../../presentation_state.hpp"
#include "../../system/dispatcher.hpp"
#include "TouchHandler.hpp"
#include "stream_bottom_ui.hpp"
#include <3ds.h>
#include <Limelight.h>
#include <algorithm>
#include <cstdio>
#include <memory>

namespace {
constexpr float kPanSensitivity = 0.012f;
constexpr float kZoomStep = 0.25f;
constexpr int kPadTop = 30;
constexpr int kPadBottom = 168;
constexpr int kBtnY = 174;
constexpr int kBtnH = 28;
constexpr int kBtnW = 59;
constexpr int kBtnGap = 3;
constexpr int kBtnX0 = 6;

void ensure_magnify_mode() {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = std::max(state.zoom, 2.0f);
    set_global_presentation_state(state);
}

void pan_magnify(float dx, float dy) {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = std::max(state.zoom, 1.0f);
    state.pan_x = std::clamp(state.pan_x + dx, -1.0f, 1.0f);
    state.pan_y = std::clamp(state.pan_y + dy, -1.0f, 1.0f);
    set_global_presentation_state(state);
}

void zoom_magnify(float delta) {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = std::clamp(state.zoom + delta, 1.0f, 4.0f);
    set_global_presentation_state(state);
}

void reset_magnify() {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = 2.0f;
    state.pan_x = 0.0f;
    state.pan_y = 0.0f;
    set_global_presentation_state(state);
}

void paint_magnify_help(const char *notice = nullptr) {
    using namespace StreamUi;
    const BottomCanvas canvas = lock_bottom_canvas();
    if (!canvas.ready()) {
        return;
    }

    const PresentationState presentation = global_presentation_state();
    canvas.clear();

    char status[24];
    if (notice != nullptr && notice[0] != '\0') {
        std::snprintf(status, sizeof(status), "%s", notice);
    } else {
        std::snprintf(status, sizeof(status), "%.2fx", presentation.zoom);
    }
    draw_header(canvas, "MAGNIFY", status);

    canvas.round_fill(6, kPadTop, 308, kPadBottom - kPadTop, kColSurface);
    canvas.text_centered("DRAG TO PAN", 6, 82, 308, kColMuted, 1);
    char pan[40];
    std::snprintf(pan, sizeof(pan), "X:%+.2f  Y:%+.2f", presentation.pan_x,
                  presentation.pan_y);
    canvas.text_centered(pan, 6, 102, 308, kColText, 1);
    canvas.text_centered("L/R ZOOM  Y RESET  X SAVE", 6, 124, 308,
                         kColMuted, 1);
    canvas.text_centered("HOLD L/R + TOUCH = CLICK", 6, 142, 308, kColMuted,
                         1);

    const char *labels[5] = {"ZOOM-", "ZOOM+", "RESET", "SAVE", "MENU"};
    for (int i = 0; i < 5; ++i) {
        const int x = kBtnX0 + i * (kBtnW + kBtnGap);
        canvas.round_fill(x, kBtnY, kBtnW, kBtnH,
                          i == 4 ? kColAccent : kColRaised);
        canvas.text_centered(labels[i], x, kBtnY + 10, kBtnW,
                             i == 4 ? kColDark : kColText, 1);
    }

    canvas.present();
}

void go_menu() {
    MessageDispatcher::get_instance()->post(
        std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH));
}
} // namespace

MagnifyTouchHandler::MagnifyTouchHandler(GAMEPAD_STATE *gamepad_state,
                                         int image_width, int image_height)
    : gamepad_state(gamepad_state), image_width(image_width),
      image_height(image_height) {
    ensure_magnify_mode();
    paint_magnify_help();
}

bool MagnifyTouchHandler::_lock_view() {
    return gamepad_state != nullptr &&
           (gamepad_state->buttons & (LB_FLAG | RB_FLAG)) != 0;
}

void MagnifyTouchHandler::_set_touch_offsets(int center_x, int center_y) {
    int x_center_image = (center_x * image_width) / GSP_SCREEN_HEIGHT_BOTTOM;
    int y_center_image = (center_y * image_height) / GSP_SCREEN_WIDTH;

    x_touch_offset = x_center_image - (GSP_SCREEN_HEIGHT_BOTTOM / 2);
    y_touch_offset = y_center_image - (GSP_SCREEN_WIDTH / 2);

    const int max_offset_x = std::max(0, image_width - GSP_SCREEN_HEIGHT_BOTTOM);
    const int max_offset_y = std::max(0, image_height - GSP_SCREEN_WIDTH);
    x_touch_offset = std::clamp(x_touch_offset, 0, max_offset_x);
    y_touch_offset = std::clamp(y_touch_offset, 0, max_offset_y);
}

void MagnifyTouchHandler::handle_navigation(u32 keys_down,
                                            const circlePosition &cpad,
                                            const circlePosition &cstick) {
    (void)cpad;
    (void)cstick;
    if (keys_down & KEY_B) {
        go_menu();
        return;
    }
    if (keys_down & KEY_X) {
        paint_magnify_help(config_save_runtime() ? "SAVED" : "SAVE ERR");
        return;
    }
    if (keys_down & KEY_Y) {
        reset_magnify();
        paint_magnify_help();
        return;
    }
    if (keys_down & KEY_L) {
        zoom_magnify(-kZoomStep);
        paint_magnify_help();
        return;
    }
    if (keys_down & KEY_R) {
        zoom_magnify(kZoomStep);
        paint_magnify_help();
    }
}

void MagnifyTouchHandler::_handle_touch_down(touchPosition touch) {
    if (touch.py >= kBtnY && touch.py <= kBtnY + kBtnH) {
        previous_x = -1;
        previous_y = -1;
        mouse_pressed = false;
        const int stride = kBtnW + kBtnGap;
        const int index = std::clamp((touch.px - kBtnX0) / stride, 0, 4);
        if (index == 0) {
            zoom_magnify(-kZoomStep);
            paint_magnify_help();
        } else if (index == 1) {
            zoom_magnify(kZoomStep);
            paint_magnify_help();
        } else if (index == 2) {
            reset_magnify();
            paint_magnify_help();
        } else if (index == 3) {
            paint_magnify_help(config_save_runtime() ? "SAVED" : "SAVE ERR");
        } else {
            go_menu();
        }
        return;
    }

    previous_x = touch.px;
    previous_y = touch.py;
    mouse_pressed = false;
    if (_lock_view()) {
        _set_touch_offsets(touch.px, touch.py);
        LiSendMousePositionEvent(touch.px + x_touch_offset,
                                 touch.py + y_touch_offset, image_width,
                                 image_height);
        LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
        mouse_pressed = true;
    }
}

void MagnifyTouchHandler::_handle_touch_up(touchPosition touch) {
    (void)touch;
    if (mouse_pressed) {
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
        mouse_pressed = false;
    }
    previous_x = -1;
    previous_y = -1;
    paint_magnify_help();
}

void MagnifyTouchHandler::_handle_touch_hold(touchPosition touch) {
    if (previous_x < 0) {
        previous_x = touch.px;
        previous_y = touch.py;
        return;
    }

    if (_lock_view()) {
        _set_touch_offsets(touch.px, touch.py);
        LiSendMousePositionEvent(touch.px + x_touch_offset,
                                 touch.py + y_touch_offset, image_width,
                                 image_height);
        return;
    }

    const float dx = static_cast<float>(touch.px - previous_x) * kPanSensitivity;
    const float dy =
        static_cast<float>(previous_y - touch.py) * kPanSensitivity;
    previous_x = touch.px;
    previous_y = touch.py;
    pan_magnify(dx, dy);
}
