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

#include "../../system/dispatcher.hpp"
#include "TouchHandler.hpp"
#include "stream_bottom_ui.hpp"
#include <Limelight.h>
#include <cstring>

#define N3DS_MOUSEPAD_SENSITIVITY 3

namespace {
constexpr int kPadTop = 30;
constexpr int kPadBottom = 168;
constexpr int kScrollX = 286;
constexpr int kBtnY = 174;
constexpr int kMenuY = 210;

struct MousePaintState {
    bool left = false;
    bool right = false;
    bool scrolling = false;
};

bool operator==(const MousePaintState &a, const MousePaintState &b) {
    return a.left == b.left && a.right == b.right && a.scrolling == b.scrolling;
}

MousePaintState g_last_paint{};

void paint_mouse(const MousePaintState &state, bool force) {
    if (!force && state == g_last_paint) {
        return;
    }
    g_last_paint = state;

    using namespace StreamUi;
    const BottomCanvas canvas = lock_bottom_canvas();
    if (!canvas.ready()) {
        return;
    }
    canvas.clear();
    draw_header(canvas, "MOUSE", state.scrolling ? "SCROLL" : "MOVE");

    canvas.round_fill(6, kPadTop, kScrollX - 12, kPadBottom - kPadTop,
                      kColSurface);
    canvas.text_centered("TRACKPAD", 6, 90, kScrollX - 12, kColMuted, 1);

    canvas.round_fill(kScrollX, kPadTop, 28, kPadBottom - kPadTop, kColRaised);
    canvas.text_centered("SCR", kScrollX, 90, 28, kColMuted, 1);

    canvas.round_fill(6, kBtnY, 150, 28, state.left ? kColAccent : kColSurface);
    canvas.text_centered("LEFT", 6, kBtnY + 10, 150,
                         state.left ? kColDark : kColText, 1);
    canvas.round_fill(164, kBtnY, 150, 28,
                      state.right ? kColAccent : kColSurface);
    canvas.text_centered("RIGHT", 164, kBtnY + 10, 150,
                         state.right ? kColDark : kColText, 1);

    canvas.round_fill(6, kMenuY, 308, 24, kColAccent);
    canvas.text_centered("MENU", 6, kMenuY + 7, 308, kColDark, 1);
    canvas.present();
}

void go_menu() {
    MessageDispatcher::get_instance()->post(
        std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH));
}
} // namespace

MouseTouchHandler::MouseTouchHandler() {
    g_last_paint = {};
    paint_mouse({}, true);
}

void MouseTouchHandler::_handle_touch_down(touchPosition touch) {
    if (touch.py >= kMenuY) {
        go_menu();
        return;
    }

    if (touch.py >= kBtnY && touch.py < kMenuY) {
        if (touch.px > 160) {
            mouse_button = BUTTON_RIGHT;
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);
            paint_mouse({false, true, false}, false);
        } else {
            mouse_button = BUTTON_LEFT;
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
            paint_mouse({true, false, false}, false);
        }
        previous_x = -1;
        previous_y = -1;
        return;
    }

    if (touch.py >= kPadTop && touch.py < kPadBottom) {
        previous_x = touch.px;
        previous_y = touch.py;
        v_scroll = touch.px >= kScrollX;
        h_scroll = false;
        paint_mouse({false, false, v_scroll}, false);
    }
}

void MouseTouchHandler::_handle_touch_up(touchPosition touch) {
    (void)touch;
    if (mouse_button > -1) {
        LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, mouse_button);
    }
    mouse_button = -1;
    previous_x = -1;
    previous_y = -1;
    v_scroll = false;
    h_scroll = false;
    paint_mouse({}, false);
}

void MouseTouchHandler::_handle_touch_hold(touchPosition touch) {
    if (previous_x == -1) {
        return;
    }
    short deltaX = touch.px - previous_x;
    short deltaY = touch.py - previous_y;
    previous_x = touch.px;
    previous_y = touch.py;

    if (v_scroll) {
        LiSendScrollEvent(-1 * deltaY);
    } else if (h_scroll) {
        LiSendHScrollEvent(deltaX);
    } else {
        LiSendMouseMoveEvent(N3DS_MOUSEPAD_SENSITIVITY * deltaX,
                             N3DS_MOUSEPAD_SENSITIVITY * deltaY);
    }
}
