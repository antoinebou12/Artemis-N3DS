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

namespace {
constexpr int kMenuY = 208;
constexpr int kTopY = 34;
constexpr int kTopH = 52;
constexpr int kMiddleY = 92;
constexpr int kMiddleH = 46;
constexpr int kGuideY = 144;
constexpr int kGuideH = 48;
constexpr int kTouchButtonMask =
    SPECIAL_FLAG | LS_CLK_FLAG | RS_CLK_FLAG | BACK_FLAG | PLAY_FLAG;

struct GamepadPaintState {
    bool l3 = false;
    bool r3 = false;
    bool back = false;
    bool start = false;
    bool guide = false;
};

bool operator==(const GamepadPaintState &a, const GamepadPaintState &b) {
    return a.l3 == b.l3 && a.r3 == b.r3 && a.back == b.back &&
           a.start == b.start && a.guide == b.guide;
}

GamepadPaintState g_last_paint{};

void paint_button(const StreamUi::BottomCanvas &canvas, int x, int y, int w,
                  int h, const char *label, bool active) {
    using namespace StreamUi;
    canvas.round_fill(x, y, w, h, active ? kColAccent : kColSurface);
    canvas.text_centered(label, x, y + (h - 7) / 2, w,
                         active ? kColDark : kColText, 1);
}

void paint_gamepad(const GamepadPaintState &state, bool force) {
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
    draw_header(canvas, "GAMEPAD", "EXTRA HOST BUTTONS");

    paint_button(canvas, 6, kTopY, 150, kTopH, "L3", state.l3);
    paint_button(canvas, 164, kTopY, 150, kTopH, "R3", state.r3);
    paint_button(canvas, 6, kMiddleY, 150, kMiddleH, "VIEW / BACK", state.back);
    paint_button(canvas, 164, kMiddleY, 150, kMiddleH, "MENU / START",
                 state.start);

    canvas.round_fill(6, kGuideY, 308, kGuideH,
                      state.guide ? kColAccent : kColRaised);
    canvas.text_centered("GUIDE / HOME", 6, kGuideY + (kGuideH - 7) / 2, 308,
                         state.guide ? kColDark : kColText, 1);

    canvas.round_fill(6, kMenuY, 308, 26, kColAccent);
    canvas.text_centered("ARTEMIS MENU  SELECT", 6, kMenuY + 8, 308, kColDark,
                         1);
    canvas.present();
}

void go_menu() {
    MessageDispatcher::get_instance()->post(
        std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH));
}
} // namespace

GamepadTouchHandler::GamepadTouchHandler(GAMEPAD_STATE *gamepad_in)
    : gamepad_state(gamepad_in) {
    g_last_paint = {};
    paint_gamepad({}, true);
}

GamepadTouchHandler::~GamepadTouchHandler() = default;

void GamepadTouchHandler::apply_host_buttons(touchPosition touch) {
    if (gamepad_state == nullptr) {
        return;
    }

    gamepad_state->buttons &= ~kTouchButtonMask;

    if (touch.py >= kMenuY) {
        go_menu();
        return;
    }

    GamepadPaintState state{};
    if (touch.py >= kGuideY && touch.py < kGuideY + kGuideH) {
        gamepad_state->buttons |= SPECIAL_FLAG;
        state.guide = true;
    } else if (touch.py >= kMiddleY && touch.py < kMiddleY + kMiddleH) {
        if (touch.px < 160) {
            gamepad_state->buttons |= BACK_FLAG;
            state.back = true;
        } else {
            gamepad_state->buttons |= PLAY_FLAG;
            state.start = true;
        }
    } else if (touch.py >= kTopY && touch.py < kTopY + kTopH) {
        if (touch.px < 160) {
            gamepad_state->buttons |= LS_CLK_FLAG;
            state.l3 = true;
        } else {
            gamepad_state->buttons |= RS_CLK_FLAG;
            state.r3 = true;
        }
    }
    paint_gamepad(state, false);
}

void GamepadTouchHandler::_handle_touch_down(touchPosition touch) {
    apply_host_buttons(touch);
}

void GamepadTouchHandler::_handle_touch_up(touchPosition touch) {
    (void)touch;
    if (gamepad_state != nullptr) {
        gamepad_state->buttons &= ~kTouchButtonMask;
    }
    paint_gamepad({}, false);
}

void GamepadTouchHandler::_handle_touch_hold(touchPosition touch) {
    apply_host_buttons(touch);
}
