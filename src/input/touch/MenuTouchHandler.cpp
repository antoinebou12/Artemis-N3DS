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
#include "../../graphics_lifecycle.hpp"
#include "../../presentation_state.hpp"
#include "../../stream_benchmark.hpp"
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
    Filtering,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    SaveCsv,
    Empty,
};

constexpr float kZoomStep = 0.25f;

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
     {"Shortcuts", MenuTileKind::TouchMode, N3dsTouchType::SHORTCUTS_TOUCH}},
    {{"Magnify", MenuTileKind::TouchMode, N3dsTouchType::MAGNIFY_TOUCH},
     {"", MenuTileKind::Empty}},
    {{"", MenuTileKind::Empty}, {"", MenuTileKind::Empty}},
};

// Filter moved to X on this page so both zoom steps and Reset View fit.
const MenuTile kDisplayPage[4][2] = {
    {{"Fit", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Fit},
     {"Fill", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Fill}},
    {{"Stretch", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Stretch},
     {"Stereo SBS", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::StereoSideBySide}},
    {{"Magnify", MenuTileKind::Presentation, N3dsTouchType::DISABLED,
      PresentationMode::Magnify},
     {"Zoom+", MenuTileKind::ZoomIn}},
    {{"Zoom-", MenuTileKind::ZoomOut}, {"Reset View", MenuTileKind::ZoomReset}},
};

// Quit first: it is the reason most people open this tab.
const MenuTile kExitPage[4][2] = {
    {{"Quit", MenuTileKind::Exit}, {"Perf", MenuTileKind::Performance}},
    {{"CSV", MenuTileKind::SaveCsv}, {"", MenuTileKind::Empty}},
    {{"", MenuTileKind::Empty}, {"", MenuTileKind::Empty}},
    {{"", MenuTileKind::Empty}, {"", MenuTileKind::Empty}},
};

const char *kTabLabels[3] = {"INPUT", "DISPLAY", "EXIT"};

const MenuTile &tile_at(int page, int row, int col) {
    if (page == 1) {
        return kDisplayPage[row][col];
    }
    if (page == 2) {
        return kExitPage[row][col];
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

// Stereo SBS only reaches the two eye framebuffers from an 800-wide surface.
// Anything narrower stays flat, so the hub says so instead of looking broken.
bool stereo_surface_available() {
    return n3ds_stream_surface_width() >= GSP_SCREEN_HEIGHT_TOP_2X;
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
    // Persist the mode itself, not every zoom tick: this writes to the SD card
    // and a write per zoom step would hitch the stream.
    config_save_runtime();
}

void adjust_stream_zoom(float delta) {
    PresentationState state = global_presentation_state();
    state.mode = PresentationMode::Magnify;
    state.zoom = std::clamp(state.zoom + delta, 1.0f, 4.0f);
    set_global_presentation_state(state);
}

void reset_stream_view() {
    PresentationState state = global_presentation_state();
    if (state.mode == PresentationMode::Magnify) {
        state.zoom = 2.0f;
    } else {
        state.zoom = 1.0f;
    }
    state.pan_x = 0.0f;
    state.pan_y = 0.0f;
    set_global_presentation_state(state);
    config_save_runtime();
}

void toggle_filtering() {
    PresentationState state = global_presentation_state();
    state.linear_filtering = !state.linear_filtering;
    set_global_presentation_state(state);
    config_save_runtime();
}

bool tile_is_live(const MenuTile &tile, const PresentationState &state) {
    if (tile.kind == MenuTileKind::Presentation) {
        return tile.presentation == state.mode;
    }
    if (tile.kind == MenuTileKind::TouchMode &&
        tile.touch_type == N3dsTouchType::MAGNIFY_TOUCH) {
        return state.mode == PresentationMode::Magnify;
    }
    if (tile.kind == MenuTileKind::Filtering) {
        return state.linear_filtering;
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
    case MenuTileKind::Filtering:
        if (apply_side_effects) {
            toggle_filtering();
        }
        return nullptr;
    case MenuTileKind::ZoomIn:
        if (apply_side_effects) {
            adjust_stream_zoom(kZoomStep);
        }
        return nullptr;
    case MenuTileKind::ZoomOut:
        if (apply_side_effects) {
            adjust_stream_zoom(-kZoomStep);
        }
        return nullptr;
    case MenuTileKind::ZoomReset:
        if (apply_side_effects) {
            reset_stream_view();
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

// Everything the hub draws. Painting is skipped when this has not moved, so
// idle SELECT costs no bottom-screen buffer swaps and never fights top video.
u32 menu_paint_signature(int page, int selected_row, int selected_col,
                         int pressed_row, int pressed_col,
                         const PresentationState &presentation) {
    float zoom = presentation.zoom;
    if (!std::isfinite(zoom) || zoom < 1.0f) {
        zoom = 1.0f;
    }
    u32 signature = static_cast<u32>(page & 0x3);
    signature = (signature << 3) | static_cast<u32>((selected_row + 1) & 0x7);
    signature = (signature << 2) | static_cast<u32>((selected_col + 1) & 0x3);
    signature = (signature << 3) | static_cast<u32>((pressed_row + 1) & 0x7);
    signature = (signature << 2) | static_cast<u32>((pressed_col + 1) & 0x3);
    signature = (signature << 3) | static_cast<u32>(presentation.mode) ;
    signature = (signature << 6) |
                (static_cast<u32>(zoom * 10.0f + 0.5f) & 0x3Fu);
    signature = (signature << 1) | (presentation.linear_filtering ? 1u : 0u);
    signature = (signature << 1) | (stereo_surface_available() ? 1u : 0u);
    return signature | 0x80000000u; // never 0: that means "nothing painted yet"
}

void paint_select_menu(int page, int selected_row, int selected_col,
                       int pressed_row, int pressed_col) {
    using namespace StreamUi;
    const BottomCanvas canvas = lock_bottom_canvas();
    if (!canvas.ready()) {
        return;
    }

    const PresentationState presentation = global_presentation_state();
    canvas.clear();

    char status[48];
    {
        float zoom = presentation.zoom;
        if (!std::isfinite(zoom) || zoom < 1.0f) {
            zoom = 1.0f;
        }
        const unsigned zoom_x10 = static_cast<unsigned>(zoom * 10.0f + 0.5f);
        if (presentation.mode == PresentationMode::StereoSideBySide &&
            !stereo_surface_available()) {
            // Say why the picture stayed flat instead of looking broken.
            std::snprintf(status, sizeof(status), "STEREO NEEDS 800X240");
        } else {
            // Mode and zoom only. Live FPS here meant reading telemetry on
            // every paint, exactly the work the hub should not be doing.
            std::snprintf(status, sizeof(status), "%s %u.%ux %s",
                          presentation_mode_name(presentation.mode),
                          zoom_x10 / 10u, zoom_x10 % 10u,
                          presentation.linear_filtering ? "LIN" : "PIX");
        }
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

    // Live bitrate/drop/decode chips used to sit here. They crowded Quit and
    // pulled telemetry on every paint; Perf owns those numbers now.
    draw_footer_three(canvas, "B HUB", "L/R TAB",
                      page == 1 ? "X FILT" : "A OPEN");
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

void MenuTouchHandler::paint_page(bool force) {
    const u32 signature =
        menu_paint_signature(page, selected_row, selected_col, active_row,
                             active_col, global_presentation_state());
    if (!force && signature == last_paint_signature) {
        return; // nothing on screen would change: no clear, no buffer swap
    }
    paint_select_menu(page, selected_row, selected_col, active_row, active_col);
    last_paint_signature = signature;
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
    // Already on the hub: B changes nothing, so do not repaint either.
    if (keys_down & KEY_B) {
        return;
    }
    if (keys_down & KEY_X) {
        if (page == 1) {
            toggle_filtering(); // Filter lives on X so Display keeps 8 tiles
            paint_page();
            return;
        }
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
