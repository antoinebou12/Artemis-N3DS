#include "n3ds_ui.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
bool g_active = false;
C3D_RenderTarget *g_top = nullptr;
C3D_RenderTarget *g_bottom = nullptr;
C2D_TextBuf g_text_buffer = nullptr;

constexpr u32 kBackground = C2D_Color32(13, 17, 23, 255);
constexpr u32 kSurface = C2D_Color32(29, 35, 44, 255);
constexpr u32 kSurfaceSelected = C2D_Color32(43, 54, 68, 255);
constexpr u32 kSurfaceRaised = C2D_Color32(35, 42, 52, 255);
constexpr u32 kAccent = C2D_Color32(72, 171, 255, 255);
constexpr u32 kAccentSoft = C2D_Color32(30, 72, 105, 255);
constexpr u32 kSuccess = C2D_Color32(79, 201, 126, 255);
constexpr u32 kText = C2D_Color32(240, 244, 248, 255);
constexpr u32 kMuted = C2D_Color32(155, 166, 179, 255);
constexpr u32 kDisabled = C2D_Color32(84, 92, 104, 255);
constexpr u32 kDarkText = C2D_Color32(10, 22, 34, 255);

constexpr int kTopVisibleRows = 5;
constexpr int kTouchVisibleRows = 4;
constexpr float kTouchRowsY = 59.0f;
constexpr float kTouchRowHeight = 28.0f;
constexpr float kTouchRowGap = 3.0f;
constexpr float kTouchRowsBottom =
    kTouchRowsY + kTouchVisibleRows * (kTouchRowHeight + kTouchRowGap);
constexpr float kActionBarY = 188.0f;
constexpr float kActionBarHeight = 42.0f;
constexpr std::size_t kMaxDrawTextBytes = 384;

struct TouchMenuState {
    bool active = false;
    bool moved = false;
    int start_y = 0;
    int start_selected = -1;
    int last_x = 0;
    int last_y = 0;
    int action_column = -1;
};

std::string bounded_text(const std::string &value) {
    if (value.size() <= kMaxDrawTextBytes) {
        return value;
    }
    std::string result = value.substr(0, kMaxDrawTextBytes - 3);
    result += "...";
    return result;
}

void draw_text(const std::string &value, float x, float y, float scale,
               u32 color, float wrap_width = 0.0f) {
    if (g_text_buffer == nullptr || value.empty()) {
        return;
    }

    const std::string safe_value = bounded_text(value);
    C2D_Text text;
    if (C2D_TextParse(&text, g_text_buffer, safe_value.c_str()) == nullptr) {
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

u32 item_accent(const std::string &item) {
    if (item.rfind("Saved", 0) == 0 || item.find("Paired") != std::string::npos ||
        item.find("On") != std::string::npos) {
        return kSuccess;
    }
    if (item.rfind("Found", 0) == 0) {
        return kAccent;
    }
    return kAccent;
}

void draw_pill(const std::string &label, float x, float y, float width,
               u32 background, u32 foreground) {
    C2D_DrawRectSolid(x, y, 0.45f, width, 18.0f, background);
    draw_text(label, x + 7.0f, y + 3.0f, 0.29f, foreground, width - 12.0f);
}

void draw_header(const std::string &title, const std::string &subtitle) {
    draw_text("ARTEMIS 3DS", 18.0f, 10.0f, 0.30f, kAccent);
    draw_text(title, 18.0f, 27.0f, 0.69f, kText, 275.0f);
    if (!subtitle.empty()) {
        draw_text(subtitle, 19.0f, 54.0f, 0.37f, kMuted, 360.0f);
    }
    C2D_DrawRectSolid(18.0f, 72.0f, 0.4f, 364.0f, 2.0f, kAccent);
}

void draw_top_overview(const std::vector<std::string> &items, int selected) {
    if (items.empty()) {
        C2D_DrawRectSolid(18.0f, 88.0f, 0.3f, 364.0f, 104.0f, kSurface);
        draw_pill("EMPTY", 32.0f, 101.0f, 56.0f, kAccentSoft, kAccent);
        draw_text("Nothing here yet", 32.0f, 130.0f, 0.56f, kText);
        draw_text("Refresh the network or add a host manually from the touch screen.",
                  32.0f, 160.0f, 0.36f, kMuted, 330.0f);
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

        const float y = 84.0f + row * 29.0f;
        const bool is_selected = item_index == safe_selected;
        const u32 accent = item_accent(items[item_index]);
        C2D_DrawRectSolid(18.0f, y, 0.3f, 364.0f, 25.0f,
                          is_selected ? kSurfaceSelected : kSurface);
        if (is_selected) {
            C2D_DrawRectSolid(18.0f, y, 0.45f, 4.0f, 25.0f, accent);
            C2D_DrawRectSolid(361.0f, y + 8.0f, 0.45f, 9.0f, 9.0f, accent);
        }
        draw_text(items[item_index], 30.0f, y + 4.0f, 0.40f,
                  is_selected ? kText : kMuted, 323.0f);
    }

    char counter[48];
    std::snprintf(counter, sizeof(counter), "%d / %d", safe_selected + 1,
                  static_cast<int>(items.size()));
    draw_pill(counter, 326.0f, 216.0f, 56.0f, kSurfaceRaised, kMuted);
}

void draw_action_button(float x, float width, const char *key,
                        const std::string &label, bool enabled, bool primary) {
    const u32 background =
        !enabled ? kSurface : (primary ? kAccent : kSurfaceRaised);
    const u32 foreground = !enabled ? kDisabled : (primary ? kDarkText : kText);
    C2D_DrawRectSolid(x, kActionBarY, 0.3f, width, kActionBarHeight,
                      background);

    draw_pill(key, x + 5.0f, kActionBarY + 5.0f, 22.0f,
              primary && enabled ? C2D_Color32(174, 221, 255, 255)
                                 : kSurfaceSelected,
              primary && enabled ? kDarkText : foreground);
    draw_text(label, x + 31.0f, kActionBarY + 13.0f, 0.30f, foreground,
              width - 34.0f);
}

void draw_bottom_actions(const std::string &secondary_label,
                         bool allow_refresh, bool has_items) {
    draw_action_button(6.0f, 72.0f, "B", "Back", true, false);
    draw_action_button(84.0f, 72.0f, "Y",
                       secondary_label.empty() ? "More" : secondary_label,
                       !secondary_label.empty(), false);
    draw_action_button(162.0f, 72.0f, "X", "Refresh", allow_refresh, false);
    draw_action_button(240.0f, 74.0f, "A", "Open", has_items, true);
}

void draw_bottom_touch_menu(const std::string &title,
                            const std::vector<std::string> &items,
                            int selected,
                            const std::string &secondary_label,
                            bool allow_refresh) {
    draw_text(title, 10.0f, 8.0f, 0.48f, kText, 205.0f);
    draw_text("Touch", 258.0f, 10.0f, 0.31f, kAccent);
    draw_text("tap to open  |  drag to scroll", 10.0f, 34.0f, 0.29f, kMuted,
              300.0f);

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
            const u32 accent = item_accent(items[item_index]);
            C2D_DrawRectSolid(7.0f, y, 0.3f, 306.0f, kTouchRowHeight,
                              is_selected ? kSurfaceSelected : kSurface);
            if (is_selected) {
                C2D_DrawRectSolid(7.0f, y, 0.45f, 4.0f, kTouchRowHeight,
                                  accent);
                draw_text(">", 292.0f, y + 5.0f, 0.38f, accent);
            }
            draw_text(items[item_index], 18.0f, y + 5.0f, 0.35f,
                      is_selected ? kText : kMuted, 266.0f);
        }
    } else {
        C2D_DrawRectSolid(7.0f, kTouchRowsY, 0.3f, 306.0f, 58.0f, kSurface);
        draw_text("No items available", 18.0f, kTouchRowsY + 10.0f, 0.42f,
                  kText);
        draw_text("Use Refresh or Add Host", 18.0f, kTouchRowsY + 34.0f,
                  0.33f, kMuted);
    }

    draw_bottom_actions(secondary_label, allow_refresh, !items.empty());
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

    C2D_SceneBegin(g_top);
    draw_header(title, subtitle);
    draw_top_overview(items, selected);

    C2D_SceneBegin(g_bottom);
    draw_bottom_touch_menu(title, items, selected, secondary_label,
                           allow_refresh);

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

int action_column_at(const touchPosition &touch) {
    if (touch.py < kActionBarY || touch.py > kActionBarY + kActionBarHeight) {
        return -1;
    }
    if (touch.px < 80) {
        return 0;
    }
    if (touch.px < 160) {
        return 1;
    }
    if (touch.px < 240) {
        return 2;
    }
    return 3;
}

bool action_from_column(int column, bool has_items, bool has_secondary,
                        bool allow_refresh, UiMenuResult &result,
                        int selected) {
    result.index = selected;
    switch (column) {
    case 0:
        result.action = UiMenuAction::Back;
        return true;
    case 1:
        if (has_secondary) {
            result.action = UiMenuAction::Secondary;
            return true;
        }
        break;
    case 2:
        if (allow_refresh) {
            result.action = UiMenuAction::Refresh;
            return true;
        }
        break;
    case 3:
        if (has_items) {
            result.action = UiMenuAction::Select;
            return true;
        }
        break;
    default:
        break;
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
    g_text_buffer = C2D_TextBufNew(8192);

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
    bool dirty = true;

    while (aptMainLoop()) {
        if (dirty) {
            draw_menu_frame(title, subtitle, items, selected, secondary_label,
                            allow_refresh);
            dirty = false;
        }

        gspWaitForVBlank();
        hidScanInput();
        const u32 down = hidKeysDown();
        const u32 held = hidKeysHeld();
        const u32 up = hidKeysUp();
        const int previous_selected = selected;

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

        if (selected != previous_selected) {
            dirty = true;
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
            touchPosition touch{};
            hidTouchRead(&touch);
            touch_state.active = true;
            touch_state.moved = false;
            touch_state.start_y = touch.py;
            touch_state.start_selected = selected;
            touch_state.last_x = touch.px;
            touch_state.last_y = touch.py;
            touch_state.action_column = action_column_at(touch);

            if (touch_state.action_column < 0) {
                const int touched_row = touch_row_at(items, selected, touch);
                if (touched_row >= 0 && touched_row != selected) {
                    selected = touched_row;
                    dirty = true;
                }
            }
        }

        if (touch_state.active && (held & KEY_TOUCH)) {
            touchPosition touch{};
            hidTouchRead(&touch);
            touch_state.last_x = touch.px;
            touch_state.last_y = touch.py;

            if (touch_state.action_column < 0 && !items.empty()) {
                const int delta_y = touch_state.start_y - touch.py;
                if (std::abs(delta_y) >= 12) {
                    touch_state.moved = true;
                    const int rows = delta_y / 24;
                    const int next_selected = std::clamp(
                        touch_state.start_selected + rows, 0,
                        static_cast<int>(items.size()) - 1);
                    if (next_selected != selected) {
                        selected = next_selected;
                        dirty = true;
                    }
                }
            }
        }

        if (touch_state.active && (up & KEY_TOUCH)) {
            touchPosition release{};
            release.px = touch_state.last_x;
            release.py = touch_state.last_y;

            if (!touch_state.moved) {
                const int release_column = action_column_at(release);
                if (touch_state.action_column >= 0 &&
                    release_column == touch_state.action_column) {
                    if (action_from_column(
                            release_column, !items.empty(),
                            !secondary_label.empty(), allow_refresh, result,
                            selected)) {
                        return result;
                    }
                } else if (touch_state.action_column < 0) {
                    const int touched_row = touch_row_at(items, selected, release);
                    if (touched_row >= 0) {
                        result.action = UiMenuAction::Select;
                        result.index = touched_row;
                        return result;
                    }
                }
            }

            touch_state = {};
            dirty = true;
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

    begin_frame();
    C2D_SceneBegin(g_top);
    draw_header(title, "");
    C2D_DrawRectSolid(18.0f, 88.0f, 0.3f, 364.0f, 112.0f, kSurface);
    draw_text(message, 32.0f, 102.0f, 0.42f, kText, 336.0f);

    C2D_SceneBegin(g_bottom);
    draw_text("Artemis 3DS", 14.0f, 12.0f, 0.50f, kText);
    draw_pill("NOTICE", 14.0f, 43.0f, 64.0f, kAccentSoft, kAccent);
    draw_text(hint, 14.0f, 78.0f, 0.38f, kMuted, 290.0f);
    C2D_DrawRectSolid(22.0f, 178.0f, 0.3f, 276.0f, 48.0f, kAccent);
    draw_pill("B", 34.0f, 188.0f, 24.0f,
              C2D_Color32(174, 221, 255, 255), kDarkText);
    draw_text("Back", 132.0f, 192.0f, 0.42f, kDarkText);
    end_frame();

    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & (KEY_A | KEY_B | KEY_START)) {
            return;
        }
        if (down & KEY_TOUCH) {
            touchPosition touch{};
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
    float y = 88.0f;
    for (const auto &line : lines) {
        C2D_DrawRectSolid(18.0f, y, 0.3f, 364.0f, 27.0f, kSurface);
        C2D_DrawRectSolid(18.0f, y, 0.45f, 4.0f, 27.0f, kAccent);
        draw_text(line, 30.0f, y + 5.0f, 0.39f, kText, 340.0f);
        y += 31.0f;
        if (y > 220.0f) {
            break;
        }
    }

    C2D_SceneBegin(g_bottom);
    draw_text("Artemis 3DS", 14.0f, 12.0f, 0.50f, kText);
    draw_pill("WORKING", 14.0f, 43.0f, 72.0f, kAccentSoft, kAccent);
    draw_text(hint, 14.0f, 83.0f, 0.39f, kMuted, 290.0f);
    C2D_DrawRectSolid(14.0f, 140.0f, 0.3f, 292.0f, 3.0f, kSurfaceRaised);
    C2D_DrawRectSolid(14.0f, 140.0f, 0.4f, 92.0f, 3.0f, kAccent);
    end_frame();
}
