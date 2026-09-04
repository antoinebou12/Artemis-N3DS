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

#include "../../system/dispatcher.hpp"
#include "TouchHandler.hpp"
#include "stream_bottom_ui.hpp"
#include <Limelight.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr int kMenuStripH = 18;

const char *default_labels[48] = {
    "ESC", "CTRL", "ALT", "TAB", "DEL", "1", "2", "3", "4", "5",
    "6",   "7",    "8",   "9",   "0",   "Q", "W", "E", "R", "T",
    "Y",   "U",    "I",   "O",   "P",   "A", "S", "D", "F", "G",
    "H",   "J",    "K",   "L",   "SHF", "Z", "X", "C", "V", "B",
    "N",   "M",    "BSP", "SYM", "WIN", "SPC", ".", "ENT",
};

const char *alt_labels[48] = {
    "ESC", "CTRL", "ALT", "TAB", "DEL", "!",  "@",  "#",  "$",  "%",
    "^",   "&",    "*",   "(",   ")",   "+",  "-",  "=",  "_",  "`",
    "'",   "\"",   "|",   "F11", "F12", "[",  "]",  "{",  "}",  "<",
    "/",   "UP",   "\\",  ">",   "SHF", "?",  ":",  ";",  "~",  "LT",
    "DN",  "RT",   "BSP", "ABC", "WIN", "SPC", ",", "ENT",
};

int key_min_x(const row_boundary &row, std::size_t index) {
    return index == 0 ? 0 : row.keys[index - 1].max_x;
}

int key_min_y(std::size_t row_index) {
    return row_index == 0 ? 0 : key_boundaries[row_index - 1].max_y;
}

void paint_keyboard(bool alt_active, KeyState shift_state, KeyState ctrl_state,
                    KeyState alt_state, int pressed_idx) {
    using namespace StreamUi;
    const BottomCanvas canvas = lock_bottom_canvas();
    if (!canvas.ready()) {
        return;
    }
    canvas.clear(kColBg);
    draw_header(canvas, "KEYS", alt_active ? "SYMBOLS" : "LETTERS");

    const char **labels = alt_active ? alt_labels : default_labels;
    for (std::size_t ri = 0; ri < key_boundaries.size(); ++ri) {
        const row_boundary &row = key_boundaries[ri];
        const int y0 = key_min_y(ri);
        const int y1 = row.max_y;
        // Leave room for the SELECT menu strip at the bottom.
        if (y0 >= GSP_SCREEN_WIDTH - kMenuStripH) {
            break;
        }
        const int draw_y1 = std::min(y1, GSP_SCREEN_WIDTH - kMenuStripH);
        for (std::size_t ki = 0; ki < row.keys.size(); ++ki) {
            const int idx = row.keys[ki].key_idx;
            if (idx < 0) {
                continue;
            }
            const int x0 = key_min_x(row, ki);
            const int x1 = row.keys[ki].max_x;
            const int w = x1 - x0 - 1;
            const int h = draw_y1 - y0 - 1;
            if (w <= 2 || h <= 2) {
                continue;
            }

            bool live = false;
            bool pressed = pressed_idx == idx;
            if (idx == 34) {
                live = shift_state != KEY_DISABLED;
            } else if (idx == 1) {
                live = ctrl_state != KEY_DISABLED;
            } else if (idx == 2) {
                live = alt_state != KEY_DISABLED;
            }

            u32 fill = kColSurface;
            u32 fg = kColMuted;
            if (pressed) {
                fill = kColAccent;
                fg = kColDark;
            } else if (live) {
                fill = kColSelected;
                fg = kColText;
            }
            canvas.round_fill(x0 + 1, y0 + 1, w, h, fill);
            const char *label = labels[idx];
            canvas.text_centered(label, x0 + 1, y0 + (h - 7) / 2 + 1, w, fg, 1);
        }
    }

    canvas.round_fill(0, GSP_SCREEN_WIDTH - kMenuStripH, GSP_SCREEN_HEIGHT_BOTTOM,
                      kMenuStripH, kColAccent);
    canvas.text_centered("MENU", 0, GSP_SCREEN_WIDTH - kMenuStripH + 5,
                         GSP_SCREEN_HEIGHT_BOTTOM, kColDark, 1);
    canvas.present();
}

void go_menu() {
    MessageDispatcher::get_instance()->post(
        std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH));
}
} // namespace

KeyboardTouchHandler::KeyboardTouchHandler()
    : selected_keycodes(&default_keycodes) {
    key_px_size = 2;
    handle_default();
}

void KeyboardTouchHandler::set_screen(const uint8_t *bgr_buffer, int bgr_size) {
    (void)bgr_buffer;
    (void)bgr_size;
    paint_keyboard(alt_keyboard_active, shift_info.state, ctrl_info.state,
                   alt_info.state, -1);
}

void KeyboardTouchHandler::set_screen_key(KeyInfo &key_info) {
    (void)key_info;
    paint_keyboard(alt_keyboard_active, shift_info.state, ctrl_info.state,
                   alt_info.state, -1);
}

void KeyboardTouchHandler::set_shift_keys() {
    shift_keys.state =
        !alt_keyboard_active && (shift_info.state != KEY_DISABLED)
            ? KEY_SHIFT
            : KEY_DISABLED;
    set_screen_key(shift_keys);
}

void KeyboardTouchHandler::handle_default() {
    selected_keycodes = &default_keycodes;
    alt_keyboard_active = false;
    paint_keyboard(false, shift_info.state, ctrl_info.state, alt_info.state, -1);
}

void KeyboardTouchHandler::cycle_key_state(KeyInfo &key_info) {
    switch (key_info.state) {
    case (KEY_TEMPORARY):
        key_info.state = KEY_LOCKED;
        break;
    case (KEY_LOCKED):
        key_info.state = KEY_DISABLED;
        break;
    default:
        key_info.state = KEY_TEMPORARY;
        break;
    }
    set_screen_key(key_info);
}

void KeyboardTouchHandler::handle_alt_keyboard() {
    if (alt_keyboard_active) {
        handle_default();
    } else {
        selected_keycodes = &alt_keycodes;
        alt_keyboard_active = true;
        paint_keyboard(true, shift_info.state, ctrl_info.state, alt_info.state, -1);
    }
}

keycode_info KeyboardTouchHandler::get_keycode(touchPosition touch) {
    for (row_boundary &r_bound : key_boundaries) {
        if (touch.py > r_bound.max_y) {
            continue;
        }
        for (key_boundary &k_bound : r_bound.keys) {
            if (touch.px > k_bound.max_x) {
                continue;
            }
            if (k_bound.key_idx >= 0) {
                return (*selected_keycodes)[k_bound.key_idx];
            }
            break;
        }
        break;
    }
    return {-1, false};
}

int KeyboardTouchHandler::get_key_mod() {
    int modifiers = 0;
    modifiers |=
        (shift_info.state != KEY_DISABLED || active_keycode.require_shift)
            ? MODIFIER_SHIFT
            : 0;
    modifiers |= (ctrl_info.state != KEY_DISABLED) ? MODIFIER_CTRL : 0;
    modifiers |= (alt_info.state != KEY_DISABLED) ? MODIFIER_ALT : 0;
    return modifiers;
}

void KeyboardTouchHandler::_handle_touch_down(touchPosition touch) {
    if (touch.py >= GSP_SCREEN_WIDTH - kMenuStripH) {
        go_menu();
        return;
    }

    active_keycode = get_keycode(touch);
    int pressed = -1;
    for (row_boundary &r_bound : key_boundaries) {
        if (touch.py > r_bound.max_y) {
            continue;
        }
        for (key_boundary &k_bound : r_bound.keys) {
            if (touch.px > k_bound.max_x) {
                continue;
            }
            pressed = k_bound.key_idx;
            break;
        }
        break;
    }
    paint_keyboard(alt_keyboard_active, shift_info.state, ctrl_info.state,
                   alt_info.state, pressed);

    if (active_keycode.code == KEYBOARD_SWITCH_KC) {
        handle_alt_keyboard();
    } else if (active_keycode.code > KEYBOARD_SWITCH_KC) {
        if (active_keycode.code == SHIFT_KC) {
            cycle_key_state(shift_info);
            set_shift_keys();
        } else if (active_keycode.code == CTRL_KC) {
            cycle_key_state(ctrl_info);
        } else if (active_keycode.code == ALT_KC) {
            cycle_key_state(alt_info);
        }
        int modifiers = get_key_mod();
        LiSendKeyboardEvent(active_keycode.code, KEY_ACTION_DOWN, modifiers);
    }
}

void KeyboardTouchHandler::_handle_touch_up(touchPosition touch) {
    (void)touch;
    if (active_keycode.code <= KEYBOARD_SWITCH_KC) {
        paint_keyboard(alt_keyboard_active, shift_info.state, ctrl_info.state,
                       alt_info.state, -1);
        return;
    }

    int modifiers = get_key_mod();
    LiSendKeyboardEvent(active_keycode.code, KEY_ACTION_UP, modifiers);

    if (active_keycode.code != SHIFT_KC && active_keycode.code != CTRL_KC &&
        active_keycode.code != ALT_KC) {
        if (shift_info.state == KEY_TEMPORARY) {
            shift_info.state = KEY_DISABLED;
            set_shift_keys();
        }
        if (ctrl_info.state == KEY_TEMPORARY) {
            ctrl_info.state = KEY_DISABLED;
        }
        if (alt_info.state == KEY_TEMPORARY) {
            alt_info.state = KEY_DISABLED;
        }
    }
    active_keycode = {-1, false};
    paint_keyboard(alt_keyboard_active, shift_info.state, ctrl_info.state,
                   alt_info.state, -1);
}

void KeyboardTouchHandler::_handle_touch_hold(touchPosition touch) {
    (void)touch;
}
