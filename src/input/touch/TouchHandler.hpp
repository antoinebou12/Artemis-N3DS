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
#pragma once

#include "../../system/message.hpp"
#include "keycode_map.hpp"
#include <3ds.h>
#include <memory>

typedef struct _GAMEPAD_STATE {
    unsigned char leftTrigger = 0;
    unsigned char rightTrigger = 0;
    short leftStickX = 0;
    short leftStickY = 0;
    short rightStickX = 0;
    short rightStickY = 0;
    int buttons = 0;
    float accel_vector_x = 0;
    float accel_vector_y = 0;
    float accel_vector_z = 0;
    float gyro_rate_x = 0;
    float gyro_rate_y = 0;
    float gyro_rate_z = 0;
} GAMEPAD_STATE;

class TouchHandlerBase {
  public:
    virtual ~TouchHandlerBase() = default;
    void handle_touch_down(touchPosition touch);
    void handle_touch_up(touchPosition touch);
    void handle_touch_hold(touchPosition touch);

    // Quick/diagnostic surfaces can capture local controls so menu navigation
    // never leaks A/B/D-pad/analog input into the remote game.
    virtual bool captures_gamepad_input() const { return false; }
    virtual void handle_navigation(u32 keys_down, const circlePosition &cpad,
                                   const circlePosition &cstick) {
        (void)keys_down;
        (void)cpad;
        (void)cstick;
    }

  private:
    virtual void _handle_touch_down(touchPosition touch) = 0;
    virtual void _handle_touch_up(touchPosition touch) = 0;
    virtual void _handle_touch_hold(touchPosition touch) = 0;

  private:
    bool isActive = false;
    touchPosition lastTouch{};
};

class DebugTouchHandler : public TouchHandlerBase {
  public:
    DebugTouchHandler();
    ~DebugTouchHandler();

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;

  public:
    static PrintConsole topScreen;
    static PrintConsole bottomScreen;
};

class MenuTouchHandler : public TouchHandlerBase {
  public:
    MenuTouchHandler();
    ~MenuTouchHandler();

    bool captures_gamepad_input() const override { return true; }
    void handle_navigation(u32 keys_down, const circlePosition &cpad,
                           const circlePosition &cstick) override;

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;
    void paint_page();
    void update_touch_target(touchPosition touch);
    void activate_selected();
    void move_selection(int dx, int dy);
    void change_page(int delta);

  private:
    std::shared_ptr<IMessage> message = nullptr;
    u64 next_nav_repeat_ticks = 0;
    int active_row = -1;
    int active_col = -1;
    int selected_row = 0;
    int selected_col = 0;
    int last_nav_x = 0;
    int last_nav_y = 0;
    int page = 0;
    int touch_start_x = 0;
    bool touch_page_swipe = false;
};

class PerformanceTouchHandler : public TouchHandlerBase {
  public:
    PerformanceTouchHandler();
    ~PerformanceTouchHandler();

    bool captures_gamepad_input() const override { return true; }
    void handle_navigation(u32 keys_down, const circlePosition &cpad,
                           const circlePosition &cstick) override;

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;
    void redraw(bool force = false);
    void save_csv();
    void go_back();

  private:
    u64 last_redraw_ticks = 0;
    char status_text[96] = {0};
};

class GamepadTouchHandler : public TouchHandlerBase {
  public:
    GamepadTouchHandler(GAMEPAD_STATE *gamepad_in);
    ~GamepadTouchHandler();

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;
    void apply_host_buttons(touchPosition touch);

  private:
    GAMEPAD_STATE *gamepad_state;
};

class MouseTouchHandler : public TouchHandlerBase {
  public:
    MouseTouchHandler();
    ~MouseTouchHandler() = default;

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;

  private:
    int mouse_button = -1;
    int previous_x = 0;
    int previous_y = 0;
    bool v_scroll = false;
    bool h_scroll = false;
};

enum KeyState { KEY_DISABLED, KEY_TEMPORARY, KEY_LOCKED, KEY_SHIFT };
struct KeyInfo {
    KeyState state = KEY_DISABLED;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
};

class KeyboardTouchHandler : public TouchHandlerBase {
  public:
    KeyboardTouchHandler();
    ~KeyboardTouchHandler() = default;

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;

    keycode_info get_keycode(touchPosition touch);
    void set_screen(const uint8_t *bgr_buffer, int bgr_size);
    void set_screen_key(KeyInfo &key_info);
    void set_shift_keys();
    void handle_default();
    void cycle_key_state(KeyInfo &key_info);
    void handle_alt_keyboard();
    int get_key_mod();

  private:
    int key_px_size;
    keycode_info active_keycode{-1, false};
    KeyInfo shift_info = {KEY_DISABLED, 0, 48, 136, 169};
    KeyInfo ctrl_info = {KEY_DISABLED, 64, 127, 0, 37};
    KeyInfo alt_info = {KEY_DISABLED, 128, 192, 0, 37};
    KeyInfo shift_keys = {KEY_SHIFT, 0, GSP_SCREEN_HEIGHT_BOTTOM, 37, 70};
    bool alt_keyboard_active = false;
    std::map<int, keycode_info> *selected_keycodes = nullptr;
};

class MirrorTouchHandler : public TouchHandlerBase {
  public:
    MirrorTouchHandler();
    ~MirrorTouchHandler() = default;

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;
};

class StretchTouchHandler : public TouchHandlerBase {
  public:
    StretchTouchHandler();
    ~StretchTouchHandler() = default;

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;
};

class MagnifyTouchHandler : public TouchHandlerBase {
  public:
    MagnifyTouchHandler(GAMEPAD_STATE *gamepad_state, int image_width,
                        int image_height);
    ~MagnifyTouchHandler() = default;

    bool captures_gamepad_input() const override { return true; }
    void handle_navigation(u32 keys_down, const circlePosition &cpad,
                           const circlePosition &cstick) override;

  private:
    void _handle_touch_down(touchPosition touch) override;
    void _handle_touch_up(touchPosition touch) override;
    void _handle_touch_hold(touchPosition touch) override;
    void _set_touch_offsets(int center_x, int center_y);
    bool _lock_view();

  private:
    GAMEPAD_STATE *gamepad_state;
    int image_width;
    int image_height;
    int x_touch_offset = 0;
    int y_touch_offset = 0;
    int previous_x = -1;
    int previous_y = -1;
    bool mouse_pressed = false;
};
