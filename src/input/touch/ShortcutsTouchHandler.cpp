#include "../../system/dispatcher.hpp"
#include "TouchHandler.hpp"
#include "stream_bottom_ui.hpp"

#include <Limelight.h>
#include <algorithm>
#include <memory>

namespace {
struct ShortcutDef {
    const char *label;
    short key;
    char modifiers;
};

constexpr int kCols = 2;
constexpr int kRows = 4;
constexpr int kCardX = 6;
constexpr int kCardY = 38;
constexpr int kCardGap = 6;
constexpr int kCardW = 151;
constexpr int kCardH = 36;
constexpr int kFooterY = 210;

const ShortcutDef kWindowsShortcuts[kRows * kCols] = {
    {"ALT TAB", 0x09, MODIFIER_ALT},
    {"WIN TAB", 0x09, MODIFIER_META},
    {"SHOW DESKTOP", 0x44, MODIFIER_META},
    {"EXPLORER", 0x45, MODIFIER_META},
    {"RUN", 0x52, MODIFIER_META},
    {"LOCK", 0x4C, MODIFIER_META},
    {"START", 0x1B, MODIFIER_CTRL},
    {"TASK MGR", 0x1B, MODIFIER_CTRL | MODIFIER_SHIFT},
};

const ShortcutDef kMacShortcuts[kRows * kCols] = {
    {"CMD TAB", 0x09, MODIFIER_META},
    {"SPOTLIGHT", 0x20, MODIFIER_META},
    {"COPY", 0x43, MODIFIER_META},
    {"PASTE", 0x56, MODIFIER_META},
    {"CLOSE", 0x57, MODIFIER_META},
    {"QUIT", 0x51, MODIFIER_META},
    {"SCREENSHOT", 0x34, MODIFIER_META | MODIFIER_SHIFT},
    {"FORCE QUIT", 0x1B, MODIFIER_META | MODIFIER_ALT},
};

const ShortcutDef &shortcut_at(int page, int index) {
    index = std::clamp(index, 0, kRows * kCols - 1);
    return page == 0 ? kWindowsShortcuts[index] : kMacShortcuts[index];
}

void send_shortcut(const ShortcutDef &shortcut) {
    LiSendKeyboardEvent(shortcut.key, KEY_ACTION_DOWN, shortcut.modifiers);
    LiSendKeyboardEvent(shortcut.key, KEY_ACTION_UP, shortcut.modifiers);
}

void go_menu() {
    MessageDispatcher::get_instance()->post(
        std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH));
}
} // namespace

ShortcutsTouchHandler::ShortcutsTouchHandler() { paint_page(); }

void ShortcutsTouchHandler::paint_page() {
    using namespace StreamUi;
    const BottomCanvas canvas = lock_bottom_canvas();
    if (!canvas.ready()) {
        return;
    }

    canvas.clear();
    draw_header(canvas, "SHORTCUTS", page == 0 ? "WINDOWS" : "MAC");

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const int index = row * kCols + col;
            const int x = kCardX + col * (kCardW + kCardGap);
            const int y = kCardY + row * (kCardH + kCardGap);
            draw_card(canvas, x, y, kCardW, kCardH,
                      shortcut_at(page, index).label, index == selected,
                      index == pressed, false, false);
        }
    }

    draw_footer_three(canvas, "B HUB", "L/R OS", "A SEND");
    canvas.present();
}

void ShortcutsTouchHandler::activate(int index) {
    selected = std::clamp(index, 0, kRows * kCols - 1);
    send_shortcut(shortcut_at(page, selected));
    paint_page();
}

void ShortcutsTouchHandler::change_page(int delta) {
    page = (page + delta + 2) % 2;
    selected = 0;
    pressed = -1;
    paint_page();
}

int ShortcutsTouchHandler::hit_shortcut(touchPosition touch) const {
    if (touch.py < kCardY || touch.py >= kFooterY) {
        return -1;
    }
    for (int row = 0; row < kRows; ++row) {
        const int y = kCardY + row * (kCardH + kCardGap);
        if (touch.py < y || touch.py >= y + kCardH) {
            continue;
        }
        for (int col = 0; col < kCols; ++col) {
            const int x = kCardX + col * (kCardW + kCardGap);
            if (touch.px >= x && touch.px < x + kCardW) {
                return row * kCols + col;
            }
        }
    }
    return -1;
}

void ShortcutsTouchHandler::handle_navigation(
    u32 keys_down, const circlePosition &cpad, const circlePosition &cstick) {
    (void)cpad;
    (void)cstick;

    if (keys_down & KEY_B) {
        go_menu();
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
    if (keys_down & KEY_X) {
        change_page(1);
        return;
    }
    if (keys_down & KEY_A) {
        activate(selected);
        return;
    }

    int row = selected / kCols;
    int col = selected % kCols;
    if (keys_down & KEY_DUP) {
        row = std::max(0, row - 1);
    } else if (keys_down & KEY_DDOWN) {
        row = std::min(kRows - 1, row + 1);
    } else if (keys_down & KEY_DLEFT) {
        col = std::max(0, col - 1);
    } else if (keys_down & KEY_DRIGHT) {
        col = std::min(kCols - 1, col + 1);
    } else {
        return;
    }
    selected = row * kCols + col;
    paint_page();
}

void ShortcutsTouchHandler::_handle_touch_down(touchPosition touch) {
    pressed = hit_shortcut(touch);
    if (pressed >= 0) {
        selected = pressed;
    }
    paint_page();
}

void ShortcutsTouchHandler::_handle_touch_up(touchPosition touch) {
    if (touch.py >= kFooterY) {
        pressed = -1;
        if (touch.px < 106) {
            go_menu();
        } else if (touch.px < 214) {
            change_page(1);
        } else {
            activate(selected);
        }
        return;
    }

    const int released = hit_shortcut(touch);
    const int originally_pressed = pressed;
    pressed = -1;
    if (released >= 0 && released == originally_pressed) {
        activate(released);
    } else {
        paint_page();
    }
}

void ShortcutsTouchHandler::_handle_touch_hold(touchPosition touch) {
    const int index = hit_shortcut(touch);
    if (index != pressed) {
        pressed = index;
        if (index >= 0) {
            selected = index;
        }
        paint_page();
    }
}
