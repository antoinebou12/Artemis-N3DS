#include "n3ds_ui.hpp"

#include "graphics_lifecycle.hpp"
#include "system/pair_record.hpp"

#include <3ds.h>
#include <citro2d.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
constexpr u32 kBackground = C2D_Color32(13, 17, 23, 255);
constexpr u32 kSurface = C2D_Color32(29, 35, 44, 255);
constexpr u32 kSurfaceSelected = C2D_Color32(43, 54, 68, 255);
constexpr u32 kSurfaceRaised = C2D_Color32(35, 42, 52, 255);
constexpr u32 kAccent = C2D_Color32(72, 171, 255, 255);
constexpr u32 kAccentSoft = C2D_Color32(30, 72, 105, 255);
constexpr u32 kSuccess = C2D_Color32(79, 201, 126, 255);
constexpr u32 kText = C2D_Color32(250, 252, 255, 255);
constexpr u32 kMuted = C2D_Color32(184, 195, 208, 255);
constexpr u32 kDisabled = C2D_Color32(84, 92, 104, 255);
constexpr u32 kDarkText = C2D_Color32(10, 22, 34, 255);

constexpr int kTouchVisibleRows = 4;
constexpr float kTouchRowsY = 59.0f;
constexpr float kTouchRowHeight = 28.0f;
constexpr float kTouchRowGap = 3.0f;
constexpr float kTouchRowsBottom =
    kTouchRowsY + kTouchVisibleRows * (kTouchRowHeight + kTouchRowGap);
constexpr float kActionBarY = 188.0f;
constexpr float kActionBarHeight = 42.0f;
constexpr std::size_t kMaxDrawTextBytes = 384;
constexpr int kAnalogThreshold = 45;
constexpr u64 kAnalogInitialRepeatTicks =
    static_cast<u64>(SYSCLOCK_ARM11) * 300 / 1000;
constexpr u64 kAnalogRepeatTicks =
    static_cast<u64>(SYSCLOCK_ARM11) * 115 / 1000;
constexpr int kDetailsVisibleLines = 6;
constexpr std::size_t kDetailsWrapChars = 55;

// Citro2D's built-in font stays crisp only on half-integer scales. Keep all UI
// typography on this ladder — sizes match the pre-regression shell UI.
constexpr float kFontMicro = 0.50f;
constexpr float kFontSmall = 0.50f;
constexpr float kFontBody = 0.50f;
constexpr float kFontTitle = 0.50f;
constexpr float kFontHero = 0.75f;

float crisp_scale(float scale) {
    return std::max(kFontMicro, std::round(scale * 2.0f) / 2.0f);
}

struct TouchMenuState {
    bool active = false;
    bool moved = false;
    int start_x = 0;
    int start_y = 0;
    int start_selected = -1;
    int pressed_index = -1;
    int last_x = 0;
    int last_y = 0;
    int action_column = -1;
};

struct AnalogNavState {
    int x_dir = 0;
    int y_dir = 0;
    u64 next_x_repeat = 0;
    u64 next_y_repeat = 0;
};

struct DetailsTouchState {
    bool active = false;
    bool moved = false;
    int start_y = 0;
    int last_x = 0;
    int last_y = 0;
    int start_offset = 0;
    int action = -1;
};

std::string bounded_text(const std::string &value) {
    if (value.size() <= kMaxDrawTextBytes) {
        return value;
    }
    std::string result = value.substr(0, kMaxDrawTextBytes - 3);
    result += "...";
    return result;
}

std::string ellipsize(const std::string &value, std::size_t max_chars) {
    if (value.size() <= max_chars || max_chars < 4) {
        return value;
    }
    return value.substr(0, max_chars - 3) + "...";
}

std::string compact_action_label(const std::string &label) {
    if (label == "Add Host") {
        return "Add";
    }
    if (label == "Remove") {
        return "Del";
    }
    if (label == "Settings") {
        return "Sets";
    }
    return ellipsize(label, 5);
}

std::string trim_copy(const std::string &value) {
    std::size_t first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return value.substr(first, last - first);
}

void draw_text(const std::string &value, float x, float y, float scale,
               u32 color, float wrap_width = 0.0f) {
    C2D_TextBuf text_buffer = n3ds_graphics_text_buffer();
    if (text_buffer == nullptr || value.empty()) {
        return;
    }

    const std::string safe_value = bounded_text(value);
    C2D_Text text;
    if (C2D_TextParse(&text, text_buffer, safe_value.c_str()) == nullptr) {
        return;
    }
    C2D_TextOptimize(&text);

    const float text_scale = crisp_scale(scale);
    const float snapped_x = std::floor(x + 0.5f);
    const float snapped_y = std::floor(y + 0.5f);

    u32 flags = C2D_WithColor | C2D_AlignLeft;
    if (wrap_width > 0.0f) {
        flags |= C2D_WordWrap;
        C2D_DrawText(&text, flags, snapped_x, snapped_y, 0.5f, text_scale,
                     text_scale, color, wrap_width);
    } else {
        C2D_DrawText(&text, flags, snapped_x, snapped_y, 0.5f, text_scale,
                     text_scale, color);
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
    return kAccent;
}

void draw_pill(const std::string &label, float x, float y, float width,
               u32 background, u32 foreground) {
    C2D_DrawRectSolid(x, y, 0.45f, width, 18.0f, background);
    draw_text(ellipsize(label, 12), x + 7.0f, y + 2.0f, kFontMicro, foreground);
}

void draw_header(const std::string &title, const std::string &subtitle) {
    draw_text("ARTEMIS 3DS", 18.0f, 10.0f, kFontMicro, kAccent);
    draw_text(ellipsize(title, 34), 18.0f, 27.0f, kFontHero, kText);
    if (!subtitle.empty()) {
        draw_text(ellipsize(subtitle, 66), 19.0f, 54.0f, kFontSmall, kMuted);
    }
    C2D_DrawRectSolid(18.0f, 72.0f, 0.4f, 364.0f, 2.0f, kAccent);
}

void draw_context_preview(const char *label, const std::string &value, float x,
                          float width) {
    C2D_DrawRectSolid(x, 184.0f, 0.3f, width, 39.0f, kSurface);
    draw_text(label, x + 10.0f, 188.0f, kFontMicro, kMuted);
    draw_text(ellipsize(value, 25), x + 10.0f, 202.0f, kFontSmall, kText);
}

void draw_top_context(const std::vector<std::string> &items, int selected) {
    if (items.empty()) {
        C2D_DrawRectSolid(18.0f, 88.0f, 0.3f, 364.0f, 104.0f, kSurface);
        draw_pill("EMPTY", 32.0f, 101.0f, 56.0f, kAccentSoft, kAccent);
        draw_text("No hosts yet", 32.0f, 130.0f, kFontSmall, kText);
        draw_text("Use bottom screen", 32.0f, 154.0f, kFontMicro, kMuted);
        return;
    }

    const int safe_selected =
        std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
    const u32 accent = item_accent(items[safe_selected]);

    C2D_DrawRectSolid(18.0f, 87.0f, 0.3f, 364.0f, 84.0f, kSurfaceSelected);
    C2D_DrawRectSolid(18.0f, 87.0f, 0.45f, 5.0f, 84.0f, accent);
    draw_pill("SELECTED", 33.0f, 99.0f, 76.0f, kAccentSoft, kAccent);

    char counter[48];
    std::snprintf(counter, sizeof(counter), "%d / %d", safe_selected + 1,
                  static_cast<int>(items.size()));
    draw_pill(counter, 314.0f, 99.0f, 54.0f, kSurfaceRaised, kMuted);

    draw_text(ellipsize(items[safe_selected], 92), 33.0f, 124.0f, kFontBody,
              kText, 324.0f);
    draw_text("Top: context   Bottom: navigation", 33.0f, 152.0f, kFontMicro,
              kMuted);

    const std::string previous =
        safe_selected > 0 ? items[safe_selected - 1] : "Start of list";
    const std::string next =
        safe_selected + 1 < static_cast<int>(items.size())
            ? items[safe_selected + 1]
            : "End of list";
    draw_context_preview("PREVIOUS", previous, 18.0f, 177.0f);
    draw_context_preview("NEXT", next, 205.0f, 177.0f);
}

void draw_action_button(float x, float width, const char *key,
                        const std::string &label, bool enabled, bool primary) {
    const u32 background =
        !enabled ? kSurface : (primary ? kAccent : kSurfaceRaised);
    const u32 foreground = !enabled ? kDisabled : (primary ? kDarkText : kText);
    C2D_DrawRectSolid(x, kActionBarY, 0.3f, width, kActionBarHeight,
                      background);

    draw_pill(key, x + 4.0f, kActionBarY + 5.0f, 20.0f,
              primary && enabled ? C2D_Color32(174, 221, 255, 255)
                                 : kSurfaceSelected,
              primary && enabled ? kDarkText : foreground);
    draw_text(compact_action_label(label), x + 27.0f, kActionBarY + 11.0f,
              kFontMicro, foreground);
}

void draw_bottom_actions(const std::string &secondary_label,
                         bool allow_refresh, bool has_items) {
    constexpr float kActionX0 = 4.0f;
    constexpr float kActionWidth = 78.0f;
    constexpr float kActionStep = 78.0f;

    draw_action_button(kActionX0, kActionWidth, "B", "Back", true, false);
    draw_action_button(kActionX0 + kActionStep, kActionWidth, "Y",
                       secondary_label.empty() ? "More" : secondary_label,
                       !secondary_label.empty(), false);
    draw_action_button(kActionX0 + kActionStep * 2, kActionWidth, "X", "Scan",
                       allow_refresh, false);
    draw_action_button(kActionX0 + kActionStep * 3, kActionWidth, "A", "Open",
                       has_items, true);
}

void draw_bottom_touch_menu(const std::string &title,
                            const std::vector<std::string> &items,
                            int selected,
                            const std::string &secondary_label,
                            bool allow_refresh) {
    draw_text(ellipsize(title, 25), 10.0f, 8.0f, kFontTitle, kText);
    draw_pill("TOUCH", 252.0f, 6.0f, 58.0f, kAccentSoft, kAccent);
    draw_text("Tap / stick to move", 10.0f, 34.0f, kFontMicro, kMuted);

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
                draw_text(">", 292.0f, y + 4.0f, kFontSmall, accent);
            }
            draw_text(ellipsize(items[item_index], 44), 18.0f, y + 4.0f,
                      kFontSmall, is_selected ? kText : kMuted);
        }
    } else {
        C2D_DrawRectSolid(7.0f, kTouchRowsY, 0.3f, 306.0f, 58.0f, kSurface);
        draw_text(ellipsize("Refresh or Add below", 22), 18.0f,
                  kTouchRowsY + 10.0f, kFontSmall, kText);
        draw_text("Tap X to scan", 18.0f, kTouchRowsY + 34.0f, kFontMicro,
                  kMuted);
    }

    draw_bottom_actions(secondary_label, allow_refresh, !items.empty());
}

void begin_frame() {
    if (!n3ds_graphics_shell_active()) {
        return;
    }
    C2D_TextBufClear(n3ds_graphics_text_buffer());
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(n3ds_graphics_top_target(), kBackground);
    C2D_TargetClear(n3ds_graphics_bottom_target(), kBackground);
}

void end_frame() {
    if (n3ds_graphics_shell_active()) {
        C3D_FrameEnd(0);
    }
}

void draw_menu_frame(const std::string &title, const std::string &subtitle,
                     const std::vector<std::string> &items, int selected,
                     const std::string &secondary_label,
                     bool allow_refresh) {
    begin_frame();

    C2D_SceneBegin(n3ds_graphics_top_target());
    draw_header(title, subtitle);
    draw_top_context(items, selected);

    C2D_SceneBegin(n3ds_graphics_bottom_target());
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
    if (touch.px < 82) {
        return 0;
    }
    if (touch.px < 160) {
        return 1;
    }
    if (touch.px < 238) {
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

int strongest_axis(int primary, int secondary) {
    return std::abs(primary) >= std::abs(secondary) ? primary : secondary;
}

int axis_direction(int value) {
    if (value >= kAnalogThreshold) {
        return 1;
    }
    if (value <= -kAnalogThreshold) {
        return -1;
    }
    return 0;
}

bool repeat_axis(int direction, int &last_direction, u64 &next_repeat,
                 u64 now) {
    if (direction == 0) {
        last_direction = 0;
        next_repeat = 0;
        return false;
    }
    if (direction != last_direction) {
        last_direction = direction;
        next_repeat = now + kAnalogInitialRepeatTicks;
        return true;
    }
    if (next_repeat != 0 && now >= next_repeat) {
        next_repeat = now + kAnalogRepeatTicks;
        return true;
    }
    return false;
}

void analog_navigation(AnalogNavState &state, int &x_step, int &y_step) {
    circlePosition cpad{};
    circlePosition cstick{};
    hidCircleRead(&cpad);
    hidCstickRead(&cstick);

    const int x_dir =
        axis_direction(strongest_axis(cpad.dx, cstick.dx));
    const int y_dir =
        axis_direction(strongest_axis(cpad.dy, cstick.dy));
    const u64 now = svcGetSystemTick();

    x_step = repeat_axis(x_dir, state.x_dir, state.next_x_repeat, now) ? x_dir
                                                                       : 0;
    y_step = repeat_axis(y_dir, state.y_dir, state.next_y_repeat, now) ? y_dir
                                                                       : 0;
}

std::vector<std::string> wrap_details_text(const std::string &text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        std::string paragraph =
            text.substr(start, newline == std::string::npos
                                   ? std::string::npos
                                   : newline - start);

        if (paragraph.empty()) {
            lines.emplace_back("");
        } else {
            while (paragraph.size() > kDetailsWrapChars) {
                std::size_t split = paragraph.rfind(' ', kDetailsWrapChars);
                if (split == std::string::npos || split == 0) {
                    split = kDetailsWrapChars;
                }
                lines.push_back(trim_copy(paragraph.substr(0, split)));
                paragraph = trim_copy(paragraph.substr(split));
            }
            if (!paragraph.empty()) {
                lines.push_back(paragraph);
            }
        }

        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1;
    }

    if (lines.empty()) {
        lines.emplace_back("");
    }
    return lines;
}

bool save_diagnostic(const std::string &title, const std::string &message,
                     std::string &path) {
    const char *directory = MOONLIGHT_3DS_PATH "/diagnostics";
    mkdir(MOONLIGHT_3DS_PATH, 0777);
    mkdir(directory, 0777);

    char output[128] = {0};
    std::snprintf(output, sizeof(output), "%s/diagnostic_%ld.txt", directory,
                  static_cast<long>(std::time(nullptr)));
    FILE *fd = std::fopen(output, "w");
    if (fd == nullptr) {
        path = "Unable to write diagnostic";
        return false;
    }

    std::fprintf(fd, "Artemis 3DS diagnostic\n\n%s\n\n%s\n", title.c_str(),
                 message.c_str());
    std::fclose(fd);
    path = output;
    return true;
}

void draw_details_button(float x, float width, const char *key,
                         const std::string &label, bool enabled, bool primary) {
    const u32 background =
        !enabled ? kSurface : (primary ? kAccent : kSurfaceRaised);
    const u32 foreground = !enabled ? kDisabled : (primary ? kDarkText : kText);
    C2D_DrawRectSolid(x, 181.0f, 0.3f, width, 48.0f, background);
    draw_pill(key, x + 7.0f, 188.0f, 24.0f,
              primary && enabled ? C2D_Color32(174, 221, 255, 255)
                                 : kSurfaceSelected,
              primary && enabled ? kDarkText : foreground);
    draw_text(ellipsize(label, 10), x + 38.0f, 194.0f, kFontSmall, foreground);
}

void draw_details_frame(const std::string &title, const std::string &subtitle,
                        const std::vector<std::string> &lines, int offset,
                        bool allow_retry, const std::string &retry_label,
                        const std::string &status) {
    begin_frame();

    C2D_SceneBegin(n3ds_graphics_top_target());
    draw_header(title, subtitle.empty() ? "Scrollable details" : subtitle);
    C2D_DrawRectSolid(18.0f, 86.0f, 0.3f, 364.0f, 140.0f, kSurface);
    C2D_DrawRectSolid(18.0f, 86.0f, 0.45f, 4.0f, 140.0f, kAccent);

    const int max_offset =
        std::max(0, static_cast<int>(lines.size()) - kDetailsVisibleLines);
    const int safe_offset = std::clamp(offset, 0, max_offset);
    float y = 96.0f;
    for (int i = 0; i < kDetailsVisibleLines; ++i) {
        const int line_index = safe_offset + i;
        if (line_index >= static_cast<int>(lines.size())) {
            break;
        }
        draw_text(ellipsize(lines[line_index], 58), 31.0f, y, kFontSmall,
                  lines[line_index].empty() ? kMuted : kText);
        y += 22.0f;
    }

    if (lines.size() > kDetailsVisibleLines) {
        const float track_y = 94.0f;
        const float track_h = 122.0f;
        C2D_DrawRectSolid(371.0f, track_y, 0.5f, 3.0f, track_h,
                          kSurfaceRaised);
        const float thumb_h = std::max(
            18.0f, track_h * kDetailsVisibleLines /
                       static_cast<float>(lines.size()));
        const float ratio = max_offset > 0
                                ? static_cast<float>(safe_offset) / max_offset
                                : 0.0f;
        C2D_DrawRectSolid(371.0f, track_y + ratio * (track_h - thumb_h), 0.55f,
                          3.0f, thumb_h, kAccent);
    }

    C2D_SceneBegin(n3ds_graphics_bottom_target());
    draw_text("Details & diagnostics", 12.0f, 8.0f, kFontTitle, kText);
    draw_pill("SCROLL", 247.0f, 6.0f, 63.0f, kAccentSoft, kAccent);
    draw_text("Swipe vertically or use Circle Pad / C-Stick", 12.0f, 36.0f,
              kFontMicro, kMuted);

    char position[64];
    const int first_line = lines.empty() ? 0 : safe_offset + 1;
    const int last_line = std::min(static_cast<int>(lines.size()),
                                   safe_offset + kDetailsVisibleLines);
    std::snprintf(position, sizeof(position), "Lines %d-%d of %d", first_line,
                  last_line, static_cast<int>(lines.size()));
    draw_pill(position, 12.0f, 67.0f, 112.0f, kSurfaceRaised, kMuted);

    if (!status.empty()) {
        draw_text(ellipsize(status, 48), 12.0f, 94.0f, kFontMicro, kSuccess);
    } else {
        draw_text("X saves this diagnostic to the SD card", 12.0f, 94.0f,
                  kFontMicro, kMuted);
    }

    C2D_DrawRectSolid(12.0f, 130.0f, 0.3f, 296.0f, 3.0f, kSurfaceRaised);
    if (lines.size() > kDetailsVisibleLines) {
        const float max_offset_f = static_cast<float>(std::max(1, max_offset));
        const float width = 70.0f;
        const float travel = 296.0f - width;
        C2D_DrawRectSolid(12.0f + travel * safe_offset / max_offset_f, 130.0f,
                          0.4f, width, 3.0f, kAccent);
    } else {
        C2D_DrawRectSolid(12.0f, 130.0f, 0.4f, 296.0f, 3.0f, kAccent);
    }

    draw_details_button(6.0f, 96.0f, "B", "Back", true, false);
    draw_details_button(112.0f, 96.0f, "X", "Save", true, false);
    draw_details_button(218.0f, 96.0f, "A", retry_label, allow_retry, true);

    end_frame();
}

int details_action_at(const touchPosition &touch) {
    if (touch.py < 177) {
        return -1;
    }
    if (touch.px < 106) {
        return 0;
    }
    if (touch.px >= 108 && touch.px < 212) {
        return 1;
    }
    if (touch.px >= 214) {
        return 2;
    }
    return -1;
}
} // namespace

bool n3ds_ui_init() {
    return n3ds_graphics_acquire_shell();
}

void n3ds_ui_shutdown() {
    n3ds_graphics_acquire_stream();
}

bool n3ds_ui_active() { return n3ds_graphics_shell_active(); }

UiMenuResult n3ds_ui_menu(const std::string &title,
                          const std::string &subtitle,
                          const std::vector<std::string> &items,
                          int selected_index,
                          const std::string &secondary_label,
                          bool allow_refresh,
                          u64 auto_refresh_ms) {
    UiMenuResult result{};
    if (!n3ds_graphics_shell_active()) {
        return result;
    }

    const u64 auto_refresh_ticks =
        auto_refresh_ms > 0
            ? static_cast<u64>(SYSCLOCK_ARM11) * auto_refresh_ms / 1000
            : 0;
    u64 last_auto_refresh = svcGetSystemTick();

    int selected = items.empty()
                       ? -1
                       : std::clamp(selected_index, 0,
                                    static_cast<int>(items.size()) - 1);
    TouchMenuState touch_state{};
    AnalogNavState analog_state{};
    bool dirty = true;

    while (aptMainLoop()) {
        if (auto_refresh_ticks > 0) {
            const u64 now = svcGetSystemTick();
            if (now - last_auto_refresh >= auto_refresh_ticks) {
                result.action = UiMenuAction::AutoRefresh;
                result.index = selected;
                return result;
            }
        }

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

        int analog_x = 0;
        int analog_y = 0;
        analog_navigation(analog_state, analog_x, analog_y);

        if (!items.empty()) {
            if ((down & KEY_DUP) || analog_y > 0) {
                selected = std::max(0, selected - 1);
            }
            if ((down & KEY_DDOWN) || analog_y < 0) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + 1);
            }
            if ((down & KEY_DLEFT) || analog_x < 0) {
                selected = std::max(0, selected - kTouchVisibleRows);
            }
            if ((down & KEY_DRIGHT) || analog_x > 0) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + kTouchVisibleRows);
            }
            if (down & KEY_L) {
                selected = std::max(0, selected - kTouchVisibleRows);
            }
            if (down & KEY_R) {
                selected = std::min(static_cast<int>(items.size()) - 1,
                                    selected + kTouchVisibleRows);
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
            touch_state.start_x = touch.px;
            touch_state.start_y = touch.py;
            touch_state.last_x = touch.px;
            touch_state.last_y = touch.py;
            touch_state.action_column = action_column_at(touch);
            touch_state.pressed_index = -1;

            if (touch_state.action_column < 0) {
                const int touched_row = touch_row_at(items, selected, touch);
                if (touched_row >= 0) {
                    touch_state.pressed_index = touched_row;
                    touch_state.start_selected = touched_row;
                    if (touched_row != selected) {
                        selected = touched_row;
                        dirty = true;
                    }
                } else {
                    touch_state.start_selected = selected;
                }
            } else {
                touch_state.start_selected = selected;
            }
        }

        if (touch_state.active && (held & KEY_TOUCH)) {
            touchPosition touch{};
            hidTouchRead(&touch);
            touch_state.last_x = touch.px;
            touch_state.last_y = touch.py;

            const int delta_x = touch.px - touch_state.start_x;
            const int delta_y = touch_state.start_y - touch.py;
            if (std::abs(delta_x) >= 18 || std::abs(delta_y) >= 12) {
                touch_state.moved = true;
            }

            if (touch_state.action_column < 0 && !items.empty() &&
                std::abs(delta_y) >= 12) {
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
                } else if (touch_state.action_column < 0 &&
                           touch_state.pressed_index >= 0 &&
                           release.py >= kTouchRowsY &&
                           release.py < kTouchRowsBottom && release.px >= 7 &&
                           release.px <= 313) {
                    result.action = UiMenuAction::Select;
                    result.index = touch_state.pressed_index;
                    return result;
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

UiDetailsAction n3ds_ui_details(const std::string &title,
                               const std::string &message,
                               const std::string &subtitle,
                               bool allow_retry,
                               const std::string &retry_label) {
    if (!n3ds_graphics_shell_active()) {
        return UiDetailsAction::Back;
    }

    const std::vector<std::string> lines = wrap_details_text(message);
    const int max_offset =
        std::max(0, static_cast<int>(lines.size()) - kDetailsVisibleLines);
    int offset = 0;
    bool dirty = true;
    std::string status;
    AnalogNavState analog_state{};
    DetailsTouchState touch_state{};

    while (aptMainLoop()) {
        if (dirty) {
            draw_details_frame(title, subtitle, lines, offset, allow_retry,
                               retry_label, status);
            dirty = false;
        }

        gspWaitForVBlank();
        hidScanInput();
        const u32 down = hidKeysDown();
        const u32 held = hidKeysHeld();
        const u32 up = hidKeysUp();

        int analog_x = 0;
        int analog_y = 0;
        analog_navigation(analog_state, analog_x, analog_y);
        const int previous_offset = offset;

        if ((down & KEY_DUP) || analog_y > 0) {
            offset = std::max(0, offset - 1);
        }
        if ((down & KEY_DDOWN) || analog_y < 0) {
            offset = std::min(max_offset, offset + 1);
        }
        if ((down & KEY_DLEFT) || (down & KEY_L) || analog_x < 0) {
            offset = std::max(0, offset - kDetailsVisibleLines);
        }
        if ((down & KEY_DRIGHT) || (down & KEY_R) || analog_x > 0) {
            offset = std::min(max_offset, offset + kDetailsVisibleLines);
        }
        if (offset != previous_offset) {
            dirty = true;
        }

        if (down & KEY_B) {
            return UiDetailsAction::Back;
        }
        if ((down & KEY_A) && allow_retry) {
            return UiDetailsAction::Retry;
        }
        if (down & KEY_X) {
            std::string path;
            const bool saved = save_diagnostic(title, message, path);
            status = saved ? "Saved: " + path : path;
            dirty = true;
        }

        if (down & KEY_TOUCH) {
            touchPosition touch{};
            hidTouchRead(&touch);
            touch_state.active = true;
            touch_state.moved = false;
            touch_state.start_y = touch.py;
            touch_state.last_x = touch.px;
            touch_state.last_y = touch.py;
            touch_state.start_offset = offset;
            touch_state.action = details_action_at(touch);
        }

        if (touch_state.active && (held & KEY_TOUCH)) {
            touchPosition touch{};
            hidTouchRead(&touch);
            touch_state.last_x = touch.px;
            touch_state.last_y = touch.py;
            const int delta_y = touch_state.start_y - touch.py;
            if (std::abs(delta_y) >= 10) {
                touch_state.moved = true;
            }
            if (touch_state.action < 0 && std::abs(delta_y) >= 10) {
                const int next_offset = std::clamp(
                    touch_state.start_offset + delta_y / 18, 0, max_offset);
                if (next_offset != offset) {
                    offset = next_offset;
                    dirty = true;
                }
            }
        }

        if (touch_state.active && (up & KEY_TOUCH)) {
            touchPosition release{};
            release.px = touch_state.last_x;
            release.py = touch_state.last_y;
            if (!touch_state.moved) {
                const int release_action = details_action_at(release);
                if (release_action == touch_state.action) {
                    if (release_action == 0) {
                        return UiDetailsAction::Back;
                    }
                    if (release_action == 1) {
                        std::string path;
                        const bool saved =
                            save_diagnostic(title, message, path);
                        status = saved ? "Saved: " + path : path;
                        dirty = true;
                    }
                    if (release_action == 2 && allow_retry) {
                        return UiDetailsAction::Retry;
                    }
                }
            }
            touch_state = {};
        }
    }

    return UiDetailsAction::Back;
}

void n3ds_ui_message(const std::string &title, const std::string &message,
                     const std::string &hint) {
    (void)n3ds_ui_details(title, message, hint, false, "Retry");
}

void n3ds_ui_status(const std::string &title, const std::string &subtitle,
                    const std::vector<std::string> &lines,
                    const std::string &hint) {
    if (!n3ds_graphics_shell_active()) {
        return;
    }

    begin_frame();
    C2D_SceneBegin(n3ds_graphics_top_target());
    draw_header(title, subtitle);
    float y = 88.0f;
    for (const auto &line : lines) {
        C2D_DrawRectSolid(18.0f, y, 0.3f, 364.0f, 27.0f, kSurface);
        C2D_DrawRectSolid(18.0f, y, 0.45f, 4.0f, 27.0f, kAccent);
        draw_text(ellipsize(line, 40), 30.0f, y + 5.0f, kFontSmall, kText);
        y += 31.0f;
        if (y > 220.0f) {
            break;
        }
    }

    C2D_SceneBegin(n3ds_graphics_bottom_target());
    draw_text("Artemis 3DS", 14.0f, 12.0f, kFontBody, kText);
    draw_pill("WORKING", 14.0f, 41.0f, 72.0f, kAccentSoft, kAccent);
    draw_text(ellipsize(hint, 42), 14.0f, 83.0f, kFontSmall, kMuted);
    draw_text(ellipsize("Top: progress  Bottom: controls", 42), 14.0f, 108.0f,
              kFontMicro, kMuted);
    C2D_DrawRectSolid(14.0f, 140.0f, 0.3f, 292.0f, 3.0f, kSurfaceRaised);
    C2D_DrawRectSolid(14.0f, 140.0f, 0.4f, 92.0f, 3.0f, kAccent);
    end_frame();
}
