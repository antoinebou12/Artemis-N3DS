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
 */
#include "../../system/dispatcher.hpp"
#include "TouchHandler.hpp"
#include <Limelight.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
constexpr int kButtonHeight = 60;
constexpr int kButtonWidth = 160;
constexpr int kNavThreshold = 45;
constexpr u64 kInitialRepeat =
    static_cast<u64>(SYSCLOCK_ARM11) * 300 / 1000;
constexpr u64 kRepeat = static_cast<u64>(SYSCLOCK_ARM11) * 120 / 1000;

const char *kButtonLabels[4][2] = {
    {"GAMEPAD", "MOUSEPAD"},
    {"KEYBOARD", "ABS TOUCH"},
    {"DS STRETCH", "MAGNIFY"},
    {"PERFORMANCE", "QUIT"},
};

N3dsTouchType kButtonTypes[4][2] = {
    {N3dsTouchType::GAMEPAD, N3dsTouchType::MOUSEPAD},
    {N3dsTouchType::KEYBOARD, N3dsTouchType::ABSOLUTE_TOUCH},
    {N3dsTouchType::DS_TOUCH, N3dsTouchType::MAGNIFY_TOUCH},
    {N3dsTouchType::PERFORMANCE_TOUCH, N3dsTouchType::DISABLED},
};

void print_tile(const char *label, bool selected, bool pressed) {
    if (pressed) {
        std::printf("[%-14s]", label);
    } else if (selected) {
        std::printf(">%-14s<", label);
    } else {
        std::printf(" %-14s ", label);
    }
}

int strongest_axis(int a, int b) {
    return std::abs(a) >= std::abs(b) ? a : b;
}

int axis_dir(int value) {
    if (value >= kNavThreshold) {
        return 1;
    }
    if (value <= -kNavThreshold) {
        return -1;
    }
    return 0;
}
} // namespace

MenuTouchHandler::MenuTouchHandler() {
    aptSetHomeAllowed(true);
    redraw(true);
}

MenuTouchHandler::~MenuTouchHandler() {
    consoleSelect(&DebugTouchHandler::topScreen);
    aptSetHomeAllowed(false);
}

void MenuTouchHandler::redraw(bool force) {
    const u64 now = svcGetSystemTick();
    if (!force && last_redraw_ticks != 0 &&
        now - last_redraw_ticks < (SYSCLOCK_ARM11 / 10)) {
        return;
    }
    last_redraw_ticks = now;

    // The top screen remains 100% dedicated to the remote video. The bottom
    // screen is a local control surface and captures navigation while open.
    consoleSelect(&DebugTouchHandler::bottomScreen);
    consoleClear();
    std::printf("ARTEMIS 3DS  |  QUICK ACTIONS\n");
    std::printf("TOP: video\n");
    std::printf("BOTTOM: menu\n");
    std::printf("----------------------------------------\n");

    for (int row = 0; row < 4; ++row) {
        const bool left_pressed = active_row == row && active_col == 0;
        const bool right_pressed = active_row == row && active_col == 1;
        std::printf("\n");
        print_tile(kButtonLabels[row][0],
                   selected_row == row && selected_col == 0, left_pressed);
        print_tile(kButtonLabels[row][1],
                   selected_row == row && selected_col == 1, right_pressed);
        std::printf("\n");
    }

    std::printf("\nHOME: menu B: gamepad A: select\n");
    std::printf("Local menu input is not sent to the PC.\n");
}

void MenuTouchHandler::move_selection(int dx, int dy) {
    const int old_row = selected_row;
    const int old_col = selected_col;
    selected_row = std::clamp(selected_row + dy, 0, 3);
    selected_col = std::clamp(selected_col + dx, 0, 1);
    if (old_row != selected_row || old_col != selected_col) {
        redraw(true);
    }
}

void MenuTouchHandler::activate_selected() {
    const N3dsTouchType touch_type = kButtonTypes[selected_row][selected_col];
    std::shared_ptr<IMessage> next_message;
    if (touch_type == N3dsTouchType::DISABLED) {
        next_message =
            std::make_shared<GenericEventMsg>(MessageType::EXIT_STREAM);
    } else {
        next_message = std::make_shared<TouchStateChangedMsg>(touch_type);
    }
    MessageDispatcher::get_instance()->post(next_message);
}

void MenuTouchHandler::handle_navigation(u32 keys_down,
                                         const circlePosition &cpad,
                                         const circlePosition &cstick) {
    if (keys_down & KEY_B) {
        MessageDispatcher::get_instance()->post(
            std::make_shared<TouchStateChangedMsg>(N3dsTouchType::GAMEPAD));
        return;
    }
    if (keys_down & KEY_X) {
        MessageDispatcher::get_instance()->post(
            std::make_shared<TouchStateChangedMsg>(
                N3dsTouchType::PERFORMANCE_TOUCH));
        return;
    }
    if (keys_down & KEY_A) {
        activate_selected();
        return;
    }

    int dx = 0;
    int dy = 0;
    if (keys_down & KEY_DLEFT) {
        dx = -1;
    } else if (keys_down & KEY_DRIGHT) {
        dx = 1;
    } else if (keys_down & KEY_DUP) {
        dy = -1;
    } else if (keys_down & KEY_DDOWN) {
        dy = 1;
    }

    if (dx != 0 || dy != 0) {
        move_selection(dx, dy);
        return;
    }

    const int raw_x = strongest_axis(cpad.dx, cstick.dx);
    const int raw_y = strongest_axis(cpad.dy, cstick.dy);
    int nav_x = axis_dir(raw_x);
    int nav_y = axis_dir(raw_y);

    // Prefer the dominant axis so diagonal analog movement never skips across
    // two grid dimensions at once.
    if (nav_x != 0 && nav_y != 0) {
        if (std::abs(raw_x) >= std::abs(raw_y)) {
            nav_y = 0;
        } else {
            nav_x = 0;
        }
    }

    const u64 now = svcGetSystemTick();
    if (nav_x == 0 && nav_y == 0) {
        last_nav_x = 0;
        last_nav_y = 0;
        next_nav_repeat_ticks = 0;
        return;
    }

    const bool changed = nav_x != last_nav_x || nav_y != last_nav_y;
    if (changed) {
        last_nav_x = nav_x;
        last_nav_y = nav_y;
        next_nav_repeat_ticks = now + kInitialRepeat;
        move_selection(nav_x, -nav_y);
        return;
    }

    if (next_nav_repeat_ticks != 0 && now >= next_nav_repeat_ticks) {
        next_nav_repeat_ticks = now + kRepeat;
        move_selection(nav_x, -nav_y);
    }
}

void MenuTouchHandler::update_touch_target(touchPosition touch) {
    touch.py = touch.py < GSP_SCREEN_WIDTH ? touch.py : (touch.py - 1);
    touch.px = touch.px < GSP_SCREEN_HEIGHT_BOTTOM ? touch.px : (touch.px - 1);

    const int row = touch.py / kButtonHeight;
    const int col = touch.px / kButtonWidth;
    if (row < 0 || row > 3 || col < 0 || col > 1) {
        active_row = -1;
        active_col = -1;
        message = nullptr;
        return;
    }

    active_row = row;
    active_col = col;
    selected_row = row;
    selected_col = col;
    const N3dsTouchType touch_type = kButtonTypes[row][col];
    if (touch_type == N3dsTouchType::DISABLED) {
        message = std::make_shared<GenericEventMsg>(MessageType::EXIT_STREAM);
    } else {
        message = std::make_shared<TouchStateChangedMsg>(touch_type);
    }
}

void MenuTouchHandler::_handle_touch_down(touchPosition touch) {
    update_touch_target(touch);
    redraw(true);
}

void MenuTouchHandler::_handle_touch_up(touchPosition touch) {
    const int pressed_row = active_row;
    const int pressed_col = active_col;
    update_touch_target(touch);
    if (message != nullptr && active_row == pressed_row &&
        active_col == pressed_col) {
        MessageDispatcher::get_instance()->post(message);
    }
    message = nullptr;
    active_row = -1;
    active_col = -1;
    redraw(true);
}

void MenuTouchHandler::_handle_touch_hold(touchPosition touch) {
    const int old_row = active_row;
    const int old_col = active_col;
    update_touch_target(touch);
    redraw(old_row != active_row || old_col != active_col);
}
