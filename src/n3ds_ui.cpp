#include "n3ds_ui.hpp"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include <algorithm>
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
constexpr u32 kDanger = C2D_Color32(255, 103, 115, 255);

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

void draw_header(const std::string &title, const std::string &subtitle) {
    draw_text(title, 18.0f, 12.0f, 0.72f, kText);
    if (!subtitle.empty()) {
        draw_text(subtitle, 19.0f, 42.0f, 0.42f, kMuted, 365.0f);
    }
    C2D_DrawRectSolid(18.0f, 65.0f, 0.4f, 364.0f, 2.0f, kAccent);
}

void draw_bottom_actions(const std::string &secondary_label,
                         bool allow_refresh) {
    draw_text("Artemis 3DS", 14.0f, 12.0f, 0.58f, kText);
    draw_text("Streaming client", 14.0f, 37.0f, 0.38f, kMuted);

    const float button_y = 188.0f;
    const float button_h = 42.0f;
    C2D_DrawRectSolid(6.0f, button_y, 0.3f, 72.0f, button_h, kSurface);
    C2D_DrawRectSolid(84.0f, button_y, 0.3f, 72.0f, button_h, kSurface);
    C2D_DrawRectSolid(162.0f, button_y, 0.3f, 72.0f, button_h, kSurface);
    C2D_DrawRectSolid(240.0f, button_y, 0.3f, 74.0f, button_h, kAccent);

    draw_text("B  Back", 13.0f, 199.0f, 0.36f, kText);
    if (!secondary_label.empty()) {
        draw_text("Y", 91.0f, 199.0f, 0.36f, kAccent);
        draw_text(secondary_label, 105.0f, 199.0f, 0.31f, kText,
                  48.0f);
    }
    if (allow_refresh) {
        draw_text("X Refresh", 168.0f, 199.0f, 0.33f, kText);
    }
    draw_text("A Select", 248.0f, 199.0f, 0.34f,
              C2D_Color32(10, 22, 34, 255));
}

void begin_frame() {
    C2D_TextBufClear(g_text_buffer);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(g_top, kBackground);
    C2D_TargetClear(g_bottom, kBackground);
}

void end_frame() { C3D_FrameEnd(0); }

void draw_menu_frame(const std::string &title, const std::string &subtitle,
                     const std::vector<std::string> &items, int selected,
                     const std::string &secondary_label,
                     bool allow_refresh) {
    begin_frame();

    C2D_SceneBegin(g_top);
    draw_header(title, subtitle);

    if (items.empty()) {
        C2D_DrawRectSolid(18.0f, 82.0f, 0.3f, 364.0f, 92.0f, kSurface);
        draw_text("No items found", 34.0f, 101.0f, 0.58f, kText);
        draw_text("Press X to search again or add a host manually.", 34.0f,
                  132.0f, 0.39f, kMuted, 325.0f);
    } else {
        const int visible_count = 5;
        const int safe_selected = std::clamp(selected, 0,
                                             static_cast<int>(items.size()) - 1);
        int first = safe_selected - visible_count / 2;
        first = std::max(0, first);
        first = std::min(first,
                         std::max(0, static_cast<int>(items.size()) -
                                         visible_count));

        for (int row = 0; row < visible_count; ++row) {
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
    }

    C2D_SceneBegin(g_bottom);
    draw_bottom_actions(secondary_label, allow_refresh);
    if (!items.empty()) {
        const int safe_selected = std::clamp(selected, 0,
                                             static_cast<int>(items.size()) - 1);
        draw_text(items[safe_selected], 14.0f, 76.0f, 0.48f, kText, 292.0f);
        char index_text[48];
        std::snprintf(index_text, sizeof(index_text), "%d of %d",
                      safe_selected + 1, static_cast<int>(items.size()));
        draw_text(index_text, 14.0f, 113.0f, 0.36f, kMuted);
    }

    end_frame();
}
} // namespace

bool n3ds_ui_init() {
    if (g_active) {
        return true;
    }

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        return false;
    }
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }

    C2D_Prepare();
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_text_buffer = C2D_TextBufNew(4096);

    if (g_top == nullptr || g_bottom == nullptr || g_text_buffer == nullptr) {
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
    int selected = items.empty()
                       ? -1
                       : std::clamp(selected_index, 0,
                                    static_cast<int>(items.size()) - 1);

    while (aptMainLoop()) {
        draw_menu_frame(title, subtitle, items, selected, secondary_label,
                        allow_refresh);

        hidScanInput();
        const u32 down = hidKeysDown();

        if (!items.empty()) {
            if (down & KEY_DUP) {
                selected = std::max(0, selected - 1);
            }
            if (down & KEY_DDOWN) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + 1);
            }
            if (down & KEY_L) {
                selected = std::max(0, selected - 5);
            }
            if (down & KEY_R) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + 5);
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
            if (touch.py >= 185) {
                if (touch.px < 80) {
                    result.action = UiMenuAction::Back;
                    result.index = selected;
                    return result;
                }
                if (touch.px < 160 && !secondary_label.empty()) {
                    result.action = UiMenuAction::Secondary;
                    result.index = selected;
                    return result;
                }
                if (touch.px < 240 && allow_refresh) {
                    result.action = UiMenuAction::Refresh;
                    result.index = selected;
                    return result;
                }
                if (touch.px >= 240 && !items.empty()) {
                    result.action = UiMenuAction::Select;
                    result.index = selected;
                    return result;
                }
            }
        }
    }

    result.action = UiMenuAction::Back;
    result.index = selected;
    return result;
}

void n3ds_ui_message(const std::string &title, const std::string &message,
                     const std::string &hint) {
    while (aptMainLoop()) {
        begin_frame();
        C2D_SceneBegin(g_top);
        draw_header(title, "");
        C2D_DrawRectSolid(18.0f, 82.0f, 0.3f, 364.0f, 112.0f, kSurface);
        draw_text(message, 32.0f, 99.0f, 0.45f, kText, 336.0f);

        C2D_SceneBegin(g_bottom);
        draw_text("Artemis 3DS", 14.0f, 12.0f, 0.58f, kText);
        draw_text(hint, 14.0f, 92.0f, 0.42f, kMuted, 290.0f);
        end_frame();

        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & (KEY_A | KEY_B | KEY_START)) {
            return;
        }
    }
}

void n3ds_ui_status(const std::string &title, const std::string &subtitle,
                    const std::vector<std::string> &lines,
                    const std::string &hint) {
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
    draw_text(hint, 14.0f, 91.0f, 0.42f, kMuted, 290.0f);
    end_frame();
}
