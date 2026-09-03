#pragma once

#include <3ds/types.h>

#include <string>
#include <vector>

enum class UiMenuAction {
    Select,
    Back,
    Refresh,
    AutoRefresh,
    Secondary,
};

struct UiMenuResult {
    UiMenuAction action = UiMenuAction::Back;
    int index = -1;
};

enum class UiDetailsAction {
    Back,
    Retry,
};

bool n3ds_ui_init();
void n3ds_ui_shutdown();
bool n3ds_ui_active();

UiMenuResult n3ds_ui_menu(const std::string &title,
                          const std::string &subtitle,
                          const std::vector<std::string> &items,
                          int selected_index,
                          const std::string &secondary_label = "",
                          bool allow_refresh = false,
                          u64 auto_refresh_ms = 0,
                          const std::string &refresh_label = "Scan");

// Scrollable details/error surface. The top screen owns the diagnostic text;
// the bottom screen owns touch/joystick scrolling and Back/Save/Retry actions.
UiDetailsAction n3ds_ui_details(const std::string &title,
                               const std::string &message,
                               const std::string &subtitle = "",
                               bool allow_retry = false,
                               const std::string &retry_label = "Retry");

void n3ds_ui_message(const std::string &title, const std::string &message,
                     const std::string &hint = "Press B to go back");

void n3ds_ui_status(const std::string &title, const std::string &subtitle,
                    const std::vector<std::string> &lines,
                    const std::string &hint);

// Pair Host wait screen: large PIN on top, Cancel on the bottom action bar.
void n3ds_ui_pair_wait(const std::string &title, const std::string &subtitle,
                       const std::string &pin,
                       const std::vector<std::string> &lines);

// Connecting wait screen: status on top, Disconnect on the bottom action bar.
void n3ds_ui_connect_wait(const std::string &title, const std::string &subtitle,
                          const std::vector<std::string> &lines);

// Poll B / Cancel|Disconnect touch while curl blocks. Redraws the wait UI.
bool n3ds_ui_wait_cancel_polled();

// Alias used by pairing; same as n3ds_ui_wait_cancel_polled.
inline bool n3ds_ui_pair_cancel_polled() {
    return n3ds_ui_wait_cancel_polled();
}
