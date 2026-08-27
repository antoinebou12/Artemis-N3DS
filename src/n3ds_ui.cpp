#include "n3ds_ui.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
bool g_active = false;
C3D_RenderTarget *g_top = nullptr;
C3D_RenderTarget *g_bottom = nullptr;
C2D_TextBuf g_text_buffer = nullptr;

constexpr u32 kBackground = C2D_Color32(13, 17, 23, 255);
constexpr u32 kSurface = C2D_Color32(29, 35, 44, 255);
constexpr u32 kSurfaceSelected = C2D_Color32(43, 54, 68, 255);
constexpr u32 kAccent = C2D_Color32(72, 171, 255, 255);
constexpr u32 kText = C2D_Color32(240, 244, 248, 255);
constexpr u32 kMuted = C2D_Color32(155, 166, 179, 255);
constexpr u32 kDarkText = C2D_Color32(10, 22, 34, 255);

constexpr int kTopVisibleRows = 5;
constexpr int kTouchVisibleRows = 4;
constexpr float kTouchRowsY = 62.0f;
constexpr float kTouchRowHeight = 27.0f;
constexpr float kTouchRowGap = 3.0f;
constexpr float kTouchRowsBottom =
    kTouchRowsY + kTouchVisibleRows * (kTouchRowHeight + kTouchRowGap);
constexpr float kActionBarY = 188.0f;
constexpr float kActionBarHeight = 42.0f;

struct TouchMenuState {
    bool active = false;
    bool moved = false;
    int start_y = 0;
    int start_selected = -1;
};

void draw_text(const std::string &value, float x, float y, float scale,
               u32 color, float wrap_width = 0.0f) {
    if (g_text_buffer == nullptr || value.empty()) {
        return;
    }

    C2D_Text text;
    if (C2D_TextParse(&text, g_text_buffer, value.c_str()) == nullptr) {
        return;
    }
    C2D_TextOptimize(&text);

    u32 flags = C2D_WithColor;
    if (wrap_width > 0.0f) {
        flags |= C2D_WordWrap;
        C2D_DrawText(&text, flags, x, y, 0.5f, scale, scale, color,
                     wrap_width);
    } else {
        C2D_DrawText(&text, flags, x, y, 0.5f, scale, scale, color);
    }
}

int visible_window_start(int item_count, int selected, int visible_count) {
    if (item_count <= 0 || visible_count <= 0) {
        return 0;
    }

    const int safe_selected = std::clamp(selected, 0, item_count - 1);
    int first = safe_selected - visible_count / 2;
    first = std::max(0, first);
    first = std::min(first, std::max(0, item_count - visible_count));
    return first;
}

void draw_header(const std::string &title, const std::string &subtitle) {
    draw_text(title, 18.0f, 12.0f, 0.72f, kText);
    if (!subtitle.empty()) {
        draw_text(subtitle, 19.0f, 42.0f, 0.42f, kMuted, 365.0f);
    }
    C2D_DrawRectSolid(18.0f, 65.0f, 0.4f, 364.0f, 2.0f, kAccent);
}

void draw_top_overview(const std::vector<std::string> &items, int selected) {
    if (items.empty()) {
        C2D_DrawRectSolid(18.0f, 82.0f, 0.3f, 364.0f, 92.0f, kSurface);
        draw_text("No items found", 34.0f, 101.0f, 0.58f, kText);
        draw_text("Use the touch controls below to refresh or add an item.",
                  34.0f, 132.0f, 0.39f, kMuted, 325.0f);
        return;
    }

    const int safe_selected =
        std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
    const int first = visible_window_start(
        static_cast<int>(items.size()), safe_selected, kTopVisibleRows);

    for (int row = 0; row < kTopVisibleRows; ++row) {
        const int item_index = first + row;
        if (item_index >= static_cast<int>(items.size())) {
            break;
        }

        const float y = 77.0f + row * 31.0f;
        const bool is_selected = item_index == safe_selected;
        C2D_DrawRectSolid(18.0f, y, 0.3f, 364.0f, 27.0f,
                          is_selected ? kSurfaceSelected : kSurface);
        if (is_selected) {
            C2D_DrawRectSolid(18.0f, y, 0.4f, 4.0f, 27.0f, kAccent);
        }
        draw_text(items[item_index], 30.0f, y + 5.0f, 0.43f,
                  is_selected ? kText : kMuted, 340.0f);
    }

    char counter[48];
    std::snprintf(counter, sizeof(counter), "%d / %d", safe_selected + 1,
                  static_cast<int>(items.size()));
    draw_text(counter, 326.0f, 218.0f, 0.32f, kMuted);
}

void draw_bottom_actions(const std::string &secondary_label,
                         bool allow_refresh) {
    C2D_DrawRectSolid(6.0f, kActionBarY, 0.3f, 72.0f, kActionBarHeight,
                      kSurface);
    C2D_DrawRectSolid(84.0f, kActionBarY, 0.3f, 72.0f, kActionBarHeight,
                      kSurface);
    C2D_DrawRectSolid(162.0f, kActionBarY, 0.3f, 72.0f, kActionBarHeight,
                      kSurface);
    C2D_DrawRectSolid(240.0f, kActionBarY, 0.3f, 74.0f, kActionBarHeight,
                      kAccent);

    draw_text("Back", 22.0f, 199.0f, 0.36f, kText);
    if (!secondary_label.empty()) {
        draw_text(secondary_label, 94.0f, 199.0f, 0.32f, kText, 54.0f);
    }
    if (allow_refresh) {
        draw_text("Refresh", 173.0f, 199.0f, 0.31f, kText);
    }
    draw_text("Open", 258.0f, 199.0f, 0.36f, kDarkText);
}

void draw_bottom_touch_menu(const std::vector<std::string> &items,
                            int selected,
                            const std::string &secondary_label,
                            bool allow_refresh) {
    draw_text("Artemis 3DS", 12.0f, 8.0f, 0.52f, kText);
    draw_text("Touch navigation", 12.0f, 32.0f, 0.34f, kAccent);
    draw_text("Tap again to open / drag to scroll", 128.0f, 34.0f, 0.29f,
              kMuted, 182.0f);

    if (!items.empty()) {
        const int safe_selected =
            std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
        const int first = visible_window_start(
            static_cast<int>(items.size()), safe_selected, kTouchVisibleRows);

        for (int row = 0; row < kTouchVisibleRows; ++row) {
            const int item_index = first + row;
            if (item_index >= static_cast<int>(items.size())) {
                break;
            }

            const float y =
                kTouchRowsY + row * (kTouchRowHeight + kTouchRowGap);
            const bool is_selected = item_index == safe_selected;
            C2D_DrawRectSolid(7.0f, y, 0.3f, 306.0f, kTouchRowHeight,
                              is_selected ? kSurfaceSelected : kSurface);
            if (is_selected) {
                C2D_DrawRectSolid(7.0f, y, 0.4f, 4.0f, kTouchRowHeight,
                                  kAccent);
            }
            draw_text(items[item_index], 18.0f, y + 5.0f, 0.36f,
                      is_selected ? kText : kMuted, 286.0f);
        }
    } else {
        C2D_DrawRectSolid(7.0f, kTouchRowsY, 0.3f, 306.0f, 57.0f, kSurface);
        draw_text("No items available", 18.0f, kTouchRowsY + 10.0f, 0.42f,
                  kText);
        draw_text("Refresh or add one manually", 18.0f,
                  kTouchRowsY + 33.0f, 0.33f, kMuted);
    }

    draw_bottom_actions(secondary_label, allow_refresh);
}

void begin_frame() {
    if (!g_active) {
        return;
    }
    C2D_TextBufClear(g_text_buffer);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(g_top, kBackground);
    C2D_TargetClear(g_bottom, kBackground);
}

void end_frame() {
    if (g_active) {
        C3D_FrameEnd(0);
    }
}

void draw_menu_frame(const std::string &title, const std::string &subtitle,
                     const std::vector<std::string> &items, int selected,
                     const std::string &secondary_label,
                     bool allow_refresh) {
    begin_frame();

    // The non-touch top screen is the wide overview/details surface.
    C2D_SceneBegin(g_top);
    draw_header(title, subtitle);
    draw_top_overview(items, selected);

    // The bottom screen mirrors the current window as large touch targets so
    // every normal menu can be navigated without the D-pad.
    C2D_SceneBegin(g_bottom);
    draw_bottom_touch_menu(items, selected, secondary_label, allow_refresh);

    end_frame();
}

int touch_row_at(const std::vector<std::string> &items, int selected,
                 const touchPosition &touch) {
    if (items.empty() || touch.py < kTouchRowsY ||
        touch.py >= kTouchRowsBottom || touch.px < 7 || touch.px > 313) {
        return -1;
    }

    const int row = static_cast<int>(
        (touch.py - kTouchRowsY) / (kTouchRowHeight + kTouchRowGap));
    if (row < 0 || row >= kTouchVisibleRows) {
        return -1;
    }

    const float row_y = kTouchRowsY + row * (kTouchRowHeight + kTouchRowGap);
    if (touch.py > row_y + kTouchRowHeight) {
        return -1;
    }

    const int first = visible_window_start(
        static_cast<int>(items.size()), selected, kTouchVisibleRows);
    const int index = first + row;
    return index < static_cast<int>(items.size()) ? index : -1;
}

bool handle_touch_action_bar(const touchPosition &touch, bool has_items,
                             bool has_secondary, bool allow_refresh,
                             UiMenuResult &result, int selected) {
    if (touch.py < kActionBarY || touch.py > kActionBarY + kActionBarHeight) {
        return false;
    }

    result.index = selected;
    if (touch.px < 80) {
        result.action = UiMenuAction::Back;
        return true;
    }
    if (touch.px < 160 && has_secondary) {
        result.action = UiMenuAction::Secondary;
        return true;
    }
    if (touch.px < 240 && allow_refresh) {
        result.action = UiMenuAction::Refresh;
        return true;
    }
    if (touch.px >= 240 && has_items) {
        result.action = UiMenuAction::Select;
        return true;
    }
    return false;
}
} // namespace

bool n3ds_ui_init() {
    if (g_active) {
        return true;
    }

    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        gfxSetDoubleBuffering(GFX_TOP, false);
        gfxSetDoubleBuffering(GFX_BOTTOM, false);
        return false;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        gfxSetDoubleBuffering(GFX_TOP, false);
        gfxSetDoubleBuffering(GFX_BOTTOM, false);
        return false;
    }

    C2D_Prepare();
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buffer = C2D_TextBufNew(4096);

    if (g_top == nullptr || g_bottom == nullptr || g_text_buffer == nullptr) {
        g_active = true;
        n3ds_ui_shutdown();
        return false;
    }

    g_active = true;
    return true;
}

void n3ds_ui_shutdown() {
    if (!g_active && g_top == nullptr && g_bottom == nullptr &&
        g_text_buffer == nullptr) {
        return;
    }

    if (g_text_buffer != nullptr) {
        C2D_TextBufDelete(g_text_buffer);
        g_text_buffer = nullptr;
    }
    if (g_top != nullptr) {
        C3D_RenderTargetDelete(g_top);
        g_top = nullptr;
    }
    if (g_bottom != nullptr) {
        C3D_RenderTargetDelete(g_bottom);
        g_bottom = nullptr;
    }

    C2D_Fini();
    C3D_Fini();

    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    g_active = false;
}

bool n3ds_ui_active() { return g_active; }

UiMenuResult n3ds_ui_menu(const std::string &title,
                          const std::string &subtitle,
                          const std::vector<std::string> &items,
                          int selected_index,
                          const std::string &secondary_label,
                          bool allow_refresh) {
    UiMenuResult result{};
    if (!g_active) {
        return result;
    }

    int selected = items.empty()
                       ? -1
                       : std::clamp(selected_index, 0,
                                    static_cast<int>(items.size()) - 1);
    TouchMenuState touch_state{};

    while (aptMainLoop()) {
        draw_menu_frame(title, subtitle, items, selected, secondary_label,
                        allow_refresh);

        hidScanInput();
        const u32 down = hidKeysDown();
        const u32 held = hidKeysHeld();
        const u32 up = hidKeysUp();

        if (!items.empty()) {
            if (down & KEY_DUP) {
                selected = std::max(0, selected - 1);
            }
            if (down & KEY_DDOWN) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + 1);
            }
            if (down & KEY_DLEFT) {
                selected = std::max(0, selected - kTouchVisibleRows);
            }
            if (down & KEY_DRIGHT) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + kTouchVisibleRows);
            }
            if (down & KEY_L) {
                selected = std::max(0, selected - kTopVisibleRows);
            }
            if (down & KEY_R) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + kTopVisibleRows);
            }
        }

        if ((down & KEY_A) && !items.empty()) {
            result.action = UiMenuAction::Select;
            result.index = selected;
            return result;
        }
        if (down & KEY_B) {
            result.action = UiMenuAction::Back;
            result.index = selected;
            return result;
        }
        if ((down & KEY_X) && allow_refresh) {
            result.action = UiMenuAction::Refresh;
            result.index = selected;
            return result;
        }
        if ((down & KEY_Y) && !secondary_label.empty()) {
            result.action = UiMenuAction::Secondary;
            result.index = selected;
            return result;
        }

        if (down & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            if (handle_touch_action_bar(
                    touch, !items.empty(), !secondary_label.empty(),
                    allow_refresh, result, selected)) {
                return result;
            }

            const int touched_row = touch_row_at(items, selected, touch);
            if (touched_row >= 0) {
                touch_state.active = true;
                touch_state.moved = false;
                touch_state.start_y = touch.py;
                touch_state.start_selected = selected;
            }
        }

        if (touch_state.active && (held & KEY_TOUCH) && !items.empty()) {
            touchPosition touch;
            hidTouchRead(&touch);
            const int delta_y = touch_state.start_y - touch.py;
            if (std::abs(delta_y) >= 12) {
                touch_state.moved = true;
                const int rows = delta_y / 24;
                selected = std::clamp(touch_state.start_selected + rows, 0,
                                      static_cast<int>(items.size()) - 1);
            }
        }

        if (touch_state.active && (up & KEY_TOUCH)) {
            touchPosition touch;
            hidTouchRead(&touch);

            if (!touch_state.moved) {
                const int touched_row = touch_row_at(items, selected, touch);
                if (touched_row >= 0) {
                    if (touched_row == selected) {
                        result.action = UiMenuAction::Select;
                        result.index = selected;
                        return result;
                    }
                    selected = touched_row;
                }
            }

            touch_state = {};
        }
    }

    result.action = UiMenuAction::Back;
    result.index = selected;
    return result;
}

void n3ds_ui_message(const std::string &title, const std::string &message,
                     const std::string &hint) {
    if (!g_active) {
        return;
    }

    while (aptMainLoop()) {
        begin_frame();
        C2D_SceneBegin(g_top);
        draw_header(title, "");
        C2D_DrawRectSolid(18.0f, 82.0f, 0.3f, 364.0f, 112.0f, kSurface);
        draw_text(message, 32.0f, 99.0f, 0.45f, kText, 336.0f);

        C2D_SceneBegin(g_bottom);
        draw_text("Artemis 3DS", 14.0f, 12.0f, 0.58f, kText);
        draw_text(hint, 14.0f, 72.0f, 0.42f, kMuted, 290.0f);
        C2D_DrawRectSolid(22.0f, 178.0f, 0.3f, 276.0f, 48.0f, kAccent);
        draw_text("Back", 139.0f, 192.0f, 0.42f, kDarkText);
        end_frame();

        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & (KEY_A | KEY_B | KEY_START)) {
            return;
        }
        if (down & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            if (touch.py >= 174) {
                return;
            }
        }
    }
}

void n3ds_ui_status(const std::string &title, const std::string &subtitle,
                    const std::vector<std::string> &lines,
                    const std::string &hint) {
    if (!g_active) {
        return;
    }

    begin_frame();
    C2D_SceneBegin(g_top);
    draw_header(title, subtitle);
    float y = 82.0f;
    for (const auto &line : lines) {
        C2D_DrawRectSolid(18.0f, y, 0.3f, 364.0f, 27.0f, kSurface);
        draw_text(line, 30.0f, y + 5.0f, 0.41f, kText, 340.0f);
        y += 31.0f;
        if (y > 220.0f) {
            break;
        }
    }

    C2D_SceneBegin(g_bottom);
    draw_text("Artemis 3DS", 14.0f, 12.0f, 0.58f, kText);
    draw_text("Working", 14.0f, 39.0f, 0.36f, kAccent);
    draw_text(hint, 14.0f, 91.0f, 0.42f, kMuted, 290.0f);
    end_frame();
}
