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
constexpr int kPadBottom = 200;

struct GamepadPaintState {
    bool l3 = false;
    bool r3 = false;
    bool guide = false;
};

bool operator==(const GamepadPaintState &a, const GamepadPaintState &b) {
    return a.l3 == b.l3 && a.r3 == b.r3 && a.guide == b.guide;
}

GamepadPaintState g_last_paint{};

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
    draw_header(canvas, "GAMEPAD", "TOUCH HOST");

    canvas.round_fill(6, 36, 150, 100, state.l3 ? kColAccent : kColSurface);
    canvas.text_centered("L3", 6, 78, 150, state.l3 ? kColDark : kColText, 1);
    canvas.round_fill(164, 36, 150, 100, state.r3 ? kColAccent : kColSurface);
    canvas.text_centered("R3", 164, 78, 150, state.r3 ? kColDark : kColText, 1);

    canvas.round_fill(6, 144, 308, 48, state.guide ? kColAccent : kColRaised);
    canvas.text_centered("GUIDE / HOME", 6, 162, 308,
                         state.guide ? kColDark : kColText, 1);

    canvas.round_fill(6, kMenuY, 308, 26, kColAccent);
    canvas.text_centered("MENU  SELECT", 6, kMenuY + 8, 308, kColDark, 1);
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

    gamepad_state->buttons &= ~(SPECIAL_FLAG | LS_CLK_FLAG | RS_CLK_FLAG);

    if (touch.py >= kMenuY) {
        go_menu();
        return;
    }

    GamepadPaintState state{};
    if (touch.py >= 144 && touch.py < kPadBottom) {
        gamepad_state->buttons |= SPECIAL_FLAG;
        state.guide = true;
    } else if (touch.py >= 36 && touch.py < 136) {
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
        gamepad_state->buttons &= ~(SPECIAL_FLAG | LS_CLK_FLAG | RS_CLK_FLAG);
    }
    paint_gamepad({}, false);
}

void GamepadTouchHandler::_handle_touch_hold(touchPosition touch) {
    apply_host_buttons(touch);
}
