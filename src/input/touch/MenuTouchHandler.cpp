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
#include "../../stream_benchmark.hpp"
#include "../../stream_telemetry_store.hpp"
#include "../../system/dispatcher.hpp"
#include "TouchHandler.hpp"
#include "select_menu_layout.hpp"
#include "stream_bottom_ui.hpp"
#include <Limelight.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
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
    SaveCsv,
    Empty,
};

struct MenuTile {
    const char *label;
    MenuTileKind kind;
    N3dsTouchType touch_type = N3dsTouchType::DISABLED;
    PresentationMode presentation = PresentationMode::Fit;
};

const MenuTile kInputPage[4][2] = {
    {{"Gamepad", MenuTileKind::TouchMode, N3dsTouchType::GAMEPAD},
     {"Mouse", MenuTileKind::TouchMode, N3dsTouchType::MOUSEPAD}},
    {{"Keyboard", MenuTileKind::TouchMode, N3dsTouchType::KEYBOARD},
     {"Mirror", MenuTileKind::TouchMode, N3dsTouchType::ABSOLUTE_TOUCH}},
    {{"Magnify", MenuTileKind::TouchMode, N3dsTouchType::MAGNIFY_TOUCH},
     {"Stretch", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Stretch}},
    {{"", MenuTileKind::Empty}, {"", MenuTileKind::Empty}},
};

const MenuTile kDisplayPage[4][2] = {
    {{"Fit", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Fit},
     {"Fill", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Fill}},
    {{"Stretch", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Stretch},
     {"SBS", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::StereoSideBySide}},
    {{"Zoom+", MenuTileKind::ZoomIn}, {"Zoom-", MenuTileKind::ZoomOut}},
    {{"Reset", MenuTileKind::ZoomReset}, {"", MenuTileKind::Empty}},
};

const MenuTile kSessionPage[4][2] = {
    {{"Perf", MenuTileKind::Performance}, {"Save", MenuTileKind::SaveCsv}},
    {{"Quit", MenuTileKind::Exit}, {"", MenuTileKind::Empty}},
    {{"", MenuTileKind::Empty}, {"", MenuTileKind::Empty}},
    {{"", MenuTileKind::Empty}, {"", MenuTileKind::Empty}},
};

const char *kTabLabels[3] = {"INPUT", "DISPLAY", "SESSION"};

const MenuTile &tile_at(int page, int row, int col) {
    if (page == 1) {
        return kDisplayPage[row][col];
    }
    if (page == 2) {
        return kSessionPage[row][col];
    }
    return kInputPage[row][col];
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

bool tile_is_live(const MenuTile &tile, const PresentationState &state) {
    if (tile.kind == MenuTileKind::Presentation) {
        return tile.presentation == state.mode;
    }
    if (tile.kind == MenuTileKind::TouchMode &&
        tile.touch_type == N3dsTouchType::MAGNIFY_TOUCH) {
        return state.mode == PresentationMode::Magnify;
    }
    if (tile.kind == MenuTileKind::ZoomIn || tile.kind == MenuTileKind::ZoomOut ||
        tile.kind == MenuTileKind::ZoomReset) {
        return state.mode == PresentationMode::Magnify;
    }
    return false;
}

std::shared_ptr<IMessage> message_for_tile(const MenuTile &tile,
                                           bool apply_side_effects) {
    switch (tile.kind) {
    case MenuTileKind::Exit:
        return std::make_shared<GenericEventMsg>(MessageType::EXIT_STREAM);
    case MenuTileKind::Performance:
        return std::make_shared<TouchStateChangedMsg>(
            N3dsTouchType::PERFORMANCE_TOUCH);
    case MenuTileKind::TouchMode:
        if (apply_side_effects &&
            tile.touch_type == N3dsTouchType::MAGNIFY_TOUCH) {
            apply_presentation_mode(PresentationMode::Magnify);
        }
        return std::make_shared<TouchStateChangedMsg>(tile.touch_type);
    case MenuTileKind::Presentation:
        if (apply_side_effects) {
            apply_presentation_mode(tile.presentation);
        }
        return nullptr;
    case MenuTileKind::ZoomIn:
        if (apply_side_effects) {
            adjust_stream_zoom(0.25f);
        }
        return nullptr;
    case MenuTileKind::ZoomOut:
        if (apply_side_effects) {
            adjust_stream_zoom(-0.25f);
        }
        return nullptr;
    case MenuTileKind::ZoomReset:
        if (apply_side_effects) {
            reset_stream_zoom();
        }
        return nullptr;
    case MenuTileKind::SaveCsv:
        if (apply_side_effects) {
            char path[96] = {0};
            export_stream_benchmark_csv(path, sizeof(path));
        }
        return nullptr;
    case MenuTileKind::Empty:
        return nullptr;
    }
    return nullptr;
}

void paint_select_menu(int page, int selected_row, int selected_col,
                       int pressed_row, int pressed_col) {
    using namespace StreamUi;
    const BottomCanvas canvas = lock_bottom_canvas();
    if (!canvas.ready()) {
        return;
    }

    const PresentationState presentation = global_presentation_state();
    const auto summary = global_stream_telemetry_summary();
    canvas.clear();

    char status[40];
    {
        float zoom = presentation.zoom;
        float fps = summary.avg_fps;
        if (!std::isfinite(zoom) || zoom < 1.0f) {
            zoom = 1.0f;
        }
        if (!std::isfinite(fps) || fps < 0.0f) {
            fps = 0.0f;
        }
        const unsigned zoom_x10 = static_cast<unsigned>(zoom * 10.0f + 0.5f);
        const unsigned fps_i = static_cast<unsigned>(fps + 0.5f);
        std::snprintf(status, sizeof(status), "%s %u.%ux %uf",
                      presentation_mode_name(presentation.mode), zoom_x10 / 10u,
                      zoom_x10 % 10u, fps_i);
    }
    draw_header(canvas, "SELECT", status);

    for (int tab = 0; tab < SelectMenuLayout::tabs; ++tab) {
        const int x = SelectMenuLayout::tab_x(tab);
        const bool active = page == tab;
        canvas.round_fill(x, SelectMenuLayout::header_h,
                          SelectMenuLayout::tab_width(),
                          SelectMenuLayout::tab_h - 2,
                          active ? kColAccent : kColRaised);
        canvas.text_centered(kTabLabels[tab], x,
                             SelectMenuLayout::header_h + 4,
                             SelectMenuLayout::tab_width(),
                             active ? kColDark : kColMuted, 1);
    }

    for (int row = 0; row < SelectMenuLayout::rows; ++row) {
        for (int col = 0; col < SelectMenuLayout::cols; ++col) {
            const MenuTile &tile = tile_at(page, row, col);
            if (tile.kind == MenuTileKind::Empty || tile.label[0] == '\0') {
                continue;
            }
            draw_card(canvas, SelectMenuLayout::tile_x(col),
                      SelectMenuLayout::tile_y(row),
                      SelectMenuLayout::tile_width(),
                      SelectMenuLayout::tile_height(), tile.label,
                      row == selected_row && col == selected_col,
                      row == pressed_row && col == pressed_col,
                      tile_is_live(tile, presentation),
                      tile.kind == MenuTileKind::Exit);
        }
    }

    if (page == 2) {
        char chip[48];
        std::snprintf(chip, sizeof(chip), "BR %uK DROP %u",
                      static_cast<unsigned>(summary.bitrate_kbps),
                      static_cast<unsigned>(summary.dropped_frames));
        canvas.round_fill(6, SelectMenuLayout::tile_y(2), 308,
                          SelectMenuLayout::tile_height(), kColRaised);
        canvas.text_centered(chip, 6,
                             SelectMenuLayout::tile_y(2) +
                                 (SelectMenuLayout::tile_height() - 7) / 2,
                             308, kColText, 1);
        float dec = summary.avg_decode_ms;
        float ren = summary.avg_render_ms;
        if (!std::isfinite(dec) || dec < 0.0f) {
            dec = 0.0f;
        }
        if (!std::isfinite(ren) || ren < 0.0f) {
            ren = 0.0f;
        }
        const unsigned dec_x10 = static_cast<unsigned>(dec * 10.0f + 0.5f);
        const unsigned ren_x10 = static_cast<unsigned>(ren * 10.0f + 0.5f);
        std::snprintf(chip, sizeof(chip), "DEC %u.%u REN %u.%u", dec_x10 / 10u,
                      dec_x10 % 10u, ren_x10 / 10u, ren_x10 % 10u);
        canvas.round_fill(6, SelectMenuLayout::tile_y(3), 308,
                          SelectMenuLayout::tile_height(), kColSurface);
        canvas.text_centered(chip, 6,
                             SelectMenuLayout::tile_y(3) +
                                 (SelectMenuLayout::tile_height() - 7) / 2,
                             308, kColMuted, 1);
    }

    draw_footer_three(canvas, "B HUB", "L/R TAB", "A OPEN");
    canvas.present();
}

bool tile_leaves_menu(const MenuTile &tile) {
    return tile.kind == MenuTileKind::Exit || tile.kind == MenuTileKind::TouchMode ||
           tile.kind == MenuTileKind::Performance;
}
} // namespace

MenuTouchHandler::MenuTouchHandler() {
    aptSetHomeAllowed(true);
    paint_page();
}

MenuTouchHandler::~MenuTouchHandler() {
    // Keep HOME allowed while other stream helpers are active; the stream
    // exit path restores the shell and HOME policy.
}

void MenuTouchHandler::paint_page() {
    paint_select_menu(page, selected_row, selected_col, active_row, active_col);
}

void MenuTouchHandler::move_selection(int dx, int dy) {
    selected_row = std::clamp(selected_row + dy, 0, 3);
    selected_col = std::clamp(selected_col + dx, 0, 1);
    // Skip empty tiles when moving.
    for (int guard = 0; guard < 8; ++guard) {
        const MenuTile &tile = tile_at(page, selected_row, selected_col);
        if (tile.kind != MenuTileKind::Empty && tile.label[0] != '\0') {
            break;
        }
        if (dx != 0) {
            selected_col = std::clamp(selected_col + dx, 0, 1);
        } else {
            selected_row = std::clamp(selected_row + (dy == 0 ? 1 : dy), 0, 3);
        }
    }
    paint_page();
}

void MenuTouchHandler::activate_selected() {
    const MenuTile &tile = tile_at(page, selected_row, selected_col);
    if (tile.kind == MenuTileKind::Empty) {
        return;
    }
    if (std::shared_ptr<IMessage> next_message =
            message_for_tile(tile, true);
        next_message != nullptr) {
        MessageDispatcher::get_instance()->post(next_message);
    } else {
        paint_page();
    }
}

void MenuTouchHandler::handle_navigation(u32 keys_down,
                                         const circlePosition &cpad,
                                         const circlePosition &cstick) {
    // Already on the hub: B is a no-op (stay on SELECT).
    if (keys_down & KEY_B) {
        paint_page();
        return;
    }
    if (keys_down & KEY_X) {
        MessageDispatcher::get_instance()->post(
            std::make_shared<TouchStateChangedMsg>(
                N3dsTouchType::PERFORMANCE_TOUCH));
        return;
    }
    if (keys_down & KEY_L) {
        change_page(-1);
        return;
    }
    if (keys_down & KEY_R) {
        change_page(1);
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

    int row = -1;
    int col = -1;
    const auto hit = SelectMenuLayout::hit(touch.px, touch.py, row, col);
    if (hit != SelectMenuLayout::Hit::Tile) {
        active_row = -1;
        active_col = -1;
        message = nullptr;
        return;
    }

    const MenuTile &tile = tile_at(page, row, col);
    if (tile.kind == MenuTileKind::Empty) {
        active_row = -1;
        active_col = -1;
        message = nullptr;
        return;
    }

    active_row = row;
    active_col = col;
    if (selected_row != row || selected_col != col) {
        selected_row = row;
        selected_col = col;
        paint_page();
    }
    message = message_for_tile(tile, false);
}

void MenuTouchHandler::change_page(int delta) {
    page = (page + delta + SelectMenuLayout::tabs) % SelectMenuLayout::tabs;
    selected_row = 0;
    selected_col = 0;
    // Land on first non-empty tile.
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 2; ++c) {
            const MenuTile &tile = tile_at(page, r, c);
            if (tile.kind != MenuTileKind::Empty && tile.label[0] != '\0') {
                selected_row = r;
                selected_col = c;
                paint_page();
                return;
            }
        }
    }
    paint_page();
}

void MenuTouchHandler::_handle_touch_down(touchPosition touch) {
    touch_start_x = touch.px;
    touch_page_swipe = false;
    update_touch_target(touch);
    paint_page();
}

void MenuTouchHandler::_handle_touch_up(touchPosition touch) {
    const int pressed_row = active_row;
    const int pressed_col = active_col;
    const int swipe_dx = touch.px - touch_start_x;
    bool left_menu = false;

    if (touch_page_swipe || std::abs(swipe_dx) >= kPageSwipeThreshold) {
        change_page(swipe_dx < 0 ? 1 : -1);
    } else {
        int row = -1;
        int col = -1;
        const auto hit = SelectMenuLayout::hit(touch.px, touch.py, row, col);
        switch (hit) {
        case SelectMenuLayout::Hit::Tab0:
            page = 0;
            selected_row = 0;
            selected_col = 0;
            break;
        case SelectMenuLayout::Hit::Tab1:
            page = 1;
            selected_row = 0;
            selected_col = 0;
            break;
        case SelectMenuLayout::Hit::Tab2:
            page = 2;
            selected_row = 0;
            selected_col = 0;
            break;
        case SelectMenuLayout::Hit::FooterBack:
            paint_page();
            break;
        case SelectMenuLayout::Hit::FooterPage:
            change_page(1);
            break;
        case SelectMenuLayout::Hit::FooterOpen: {
            const MenuTile &tile = tile_at(page, selected_row, selected_col);
            if (tile_leaves_menu(tile)) {
                left_menu = true;
            }
            activate_selected();
            break;
        }
        case SelectMenuLayout::Hit::None:
        case SelectMenuLayout::Hit::Tile:
        default:
            update_touch_target(touch);
            if (active_row == pressed_row && active_col == pressed_col &&
                pressed_row >= 0) {
                const MenuTile &tile =
                    tile_at(page, pressed_row, pressed_col);
                if (std::shared_ptr<IMessage> next_message =
                        message_for_tile(tile, true);
                    next_message != nullptr) {
                    MessageDispatcher::get_instance()->post(next_message);
                    left_menu = true;
                }
            }
            break;
        }
    }

    message = nullptr;
    active_row = -1;
    active_col = -1;
    if (!left_menu) {
        paint_page();
    }
}

void MenuTouchHandler::_handle_touch_hold(touchPosition touch) {
    const int swipe_dx = touch.px - touch_start_x;
    if (std::abs(swipe_dx) >= kPageSwipeThreshold) {
        touch_page_swipe = true;
        active_row = -1;
        active_col = -1;
        message = nullptr;
        return;
    }
    update_touch_target(touch);
}
