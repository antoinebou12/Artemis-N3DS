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
#include <cstdio>
#include <memory>

namespace {
constexpr int kButtonHeight = 60;
constexpr int kButtonWidth = 160;

const char *kButtonLabels[4][2] = {
    {"GAMEPAD", "MOUSEPAD"},
    {"KEYBOARD", "ABS TOUCH"},
    {"DUAL SCREEN", "MAGNIFY"},
    {"PERFORMANCE", "EXIT STREAM"},
};

N3dsTouchType kButtonTypes[4][2] = {
    {N3dsTouchType::GAMEPAD, N3dsTouchType::MOUSEPAD},
    {N3dsTouchType::KEYBOARD, N3dsTouchType::ABSOLUTE_TOUCH},
    {N3dsTouchType::DS_TOUCH, N3dsTouchType::MAGNIFY_TOUCH},
    {N3dsTouchType::PERFORMANCE_TOUCH, N3dsTouchType::DISABLED},
};

void print_tile(const char *label, bool active) {
    if (active) {
        std::printf("> %-14s <", label);
    } else {
        std::printf("  %-14s  ", label);
    }
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
        now - last_redraw_ticks < (SYSCLOCK_ARM11 / 8)) {
        return;
    }
    last_redraw_ticks = now;

    // While streaming, the top screen remains entirely owned by the video
    // renderer. Navigation is intentionally confined to the touch screen so
    // opening Quick Actions never obscures gameplay/desktop content.
    consoleSelect(&DebugTouchHandler::bottomScreen);
    consoleClear();
    std::printf("ARTEMIS 3DS   QUICK ACTIONS\n");
    std::printf("Top: live stream | Bottom: touch controls\n");
    std::printf("----------------------------------------\n");

    for (int row = 0; row < 4; ++row) {
        std::printf("\n");
        print_tile(kButtonLabels[row][0], active_row == row && active_col == 0);
        print_tile(kButtonLabels[row][1], active_row == row && active_col == 1);
        std::printf("\n");
    }

    std::printf("\nTap and release a tile to switch mode.\n");
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
    update_touch_target(touch);
    if (message != nullptr) {
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
