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
#include "../../presentation_state.hpp"
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
constexpr int kPageSwipeThreshold = 48;
constexpr u64 kInitialRepeat =
    static_cast<u64>(SYSCLOCK_ARM11) * 300 / 1000;
constexpr u64 kRepeat = static_cast<u64>(SYSCLOCK_ARM11) * 120 / 1000;

enum class MenuTileKind {
    TouchMode,
    Exit,
    Performance,
    Presentation,
    ZoomIn,
    ZoomOut,
    ZoomReset,
};

struct MenuTile {
    const char *label;
    MenuTileKind kind;
    N3dsTouchType touch_type = N3dsTouchType::DISABLED;
    PresentationMode presentation = PresentationMode::Fit;
};

const MenuTile kInputPage[4][2] = {
    {{"GAMEPAD", MenuTileKind::TouchMode, N3dsTouchType::GAMEPAD},
     {"MOUSE", MenuTileKind::TouchMode, N3dsTouchType::MOUSEPAD}},
    {{"KEYBOARD", MenuTileKind::TouchMode, N3dsTouchType::KEYBOARD},
     {"ABS TOUCH", MenuTileKind::TouchMode, N3dsTouchType::ABSOLUTE_TOUCH}},
    {{"DS STRETCH", MenuTileKind::TouchMode, N3dsTouchType::DS_TOUCH},
     {"MAGNIFY", MenuTileKind::TouchMode, N3dsTouchType::MAGNIFY_TOUCH}},
    {{"PERF", MenuTileKind::Performance},
     {"QUIT", MenuTileKind::Exit}},
};

const MenuTile kDisplayPage[4][2] = {
    {{"FIT", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Fit},
     {"FILL", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Fill}},
    {{"STRETCH", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Stretch},
     {"SBS", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::StereoSideBySide}},
    {{"ZOOM+", MenuTileKind::ZoomIn},
     {"ZOOM-", MenuTileKind::ZoomOut}},
    {{"RESET", MenuTileKind::ZoomReset},
     {"QUIT", MenuTileKind::Exit}},
};

const MenuTile &tile_at(int page, int row, int col) {
    return page == 0 ? kInputPage[row][col] : kDisplayPage[row][col];
}

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

void apply_presentation_mode(PresentationMode mode) {
    PresentationState state = global_presentation_state();
    state.mode = mode;
    if (mode == PresentationMode::Magnify) {
        state.zoom = std::max(state.zoom, 2.0f);
    } else {
        state.zoom = 1.0f;
        state.pan_x = 0.0f;
        state.pan_y = 0.0f;
    }
    set_global_presentation_state(state);
}

void adjust_stream_zoom(float delta) {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = std::clamp(state.zoom + delta, 1.0f, 4.0f);
    set_global_presentation_state(state);
}

void reset_stream_zoom() {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = 2.0f;
    state.pan_x = 0.0f;
    state.pan_y = 0.0f;
    set_global_presentation_state(state);
}

std::shared_ptr<IMessage> message_for_tile(const MenuTile &tile) {
    switch (tile.kind) {
    case MenuTileKind::Exit:
        return std::make_shared<GenericEventMsg>(MessageType::EXIT_STREAM);
    case MenuTileKind::Performance:
        return std::make_shared<TouchStateChangedMsg>(
            N3dsTouchType::PERFORMANCE_TOUCH);
    case MenuTileKind::TouchMode:
        return std::make_shared<TouchStateChangedMsg>(tile.touch_type);
    case MenuTileKind::Presentation:
        apply_presentation_mode(tile.presentation);
        return nullptr;
    case MenuTileKind::ZoomIn:
        adjust_stream_zoom(0.25f);
        return nullptr;
    case MenuTileKind::ZoomOut:
        adjust_stream_zoom(-0.25f);
        return nullptr;
    case MenuTileKind::ZoomReset:
        reset_stream_zoom();
        return nullptr;
    }
    return nullptr;
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

    consoleSelect(&DebugTouchHandler::bottomScreen);
    consoleClear();
    std::printf("HOME/SELECT: menu  L/R: page %d/2 %s\n", page + 1,
                page == 0 ? "INPUT" : "DISPLAY");
    std::printf("L3/R3 on pad = host sticks  A:ok B:pad\n");

    for (int row = 0; row < 4; ++row) {
        const bool left_pressed = active_row == row && active_col == 0;
        const bool right_pressed = active_row == row && active_col == 1;
        const MenuTile &left = tile_at(page, row, 0);
        const MenuTile &right = tile_at(page, row, 1);
        std::printf("\n");
        print_tile(left.label, selected_row == row && selected_col == 0,
                   left_pressed);
        print_tile(right.label, selected_row == row && selected_col == 1,
                   right_pressed);
        std::printf("\n");
    }

    if (page == 1) {
        std::printf("\nMode: %s  Zoom: %.1fx\n",
                    presentation_mode_name(global_presentation_state().mode),
                    global_presentation_state().zoom);
    }
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
    const MenuTile &tile = tile_at(page, selected_row, selected_col);
    if (std::shared_ptr<IMessage> next_message = message_for_tile(tile);
        next_message != nullptr) {
        MessageDispatcher::get_instance()->post(next_message);
    } else {
        redraw(true);
    }
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
    if (keys_down & KEY_L) {
        page = (page + 1) % 2;
        selected_row = 0;
        selected_col = 0;
        redraw(true);
        return;
    }
    if (keys_down & KEY_R) {
        page = (page + 1) % 2;
        selected_row = 0;
        selected_col = 0;
        redraw(true);
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
    message = message_for_tile(tile_at(page, row, col));
}

void MenuTouchHandler::change_page(int delta) {
    page = (page + delta + 2) % 2;
    selected_row = 0;
    selected_col = 0;
    redraw(true);
}

void MenuTouchHandler::_handle_touch_down(touchPosition touch) {
    touch_start_x = touch.px;
    touch_page_swipe = false;
    update_touch_target(touch);
    redraw(true);
}

void MenuTouchHandler::_handle_touch_up(touchPosition touch) {
    const int pressed_row = active_row;
    const int pressed_col = active_col;
    const int swipe_dx = touch.px - touch_start_x;
    if (touch_page_swipe || std::abs(swipe_dx) >= kPageSwipeThreshold) {
        change_page(swipe_dx < 0 ? 1 : -1);
    } else {
        update_touch_target(touch);
        if (message != nullptr && active_row == pressed_row &&
            active_col == pressed_col) {
            MessageDispatcher::get_instance()->post(message);
        }
    }
    message = nullptr;
    active_row = -1;
    active_col = -1;
    redraw(true);
}

void MenuTouchHandler::_handle_touch_hold(touchPosition touch) {
    const int old_row = active_row;
    const int old_col = active_col;
    const int swipe_dx = touch.px - touch_start_x;
    if (std::abs(swipe_dx) >= kPageSwipeThreshold) {
        touch_page_swipe = true;
        active_row = -1;
        active_col = -1;
        message = nullptr;
        redraw(false);
        return;
    }
    update_touch_target(touch);
    redraw(old_row != active_row || old_col != active_col);
}
