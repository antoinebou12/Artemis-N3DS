#pragma once

#include <string>
#include <vector>

enum class UiMenuAction {
    Select,
    Back,
    Refresh,
    Secondary,
};

struct UiMenuResult {
    UiMenuAction action = UiMenuAction::Back;
    int index = -1;
};

bool n3ds_ui_init();
void n3ds_ui_shutdown();
bool n3ds_ui_active();

UiMenuResult n3ds_ui_menu(const std::string &title,
                          const std::string &subtitle,
                          const std::vector<std::string> &items,
                          int selected_index,
                          const std::string &secondary_label = "",
                          bool allow_refresh = false);

void n3ds_ui_message(const std::string &title, const std::string &message,
                     const std::string &hint = "Press B to go back");

void n3ds_ui_status(const std::string &title, const std::string &subtitle,
                    const std::vector<std::string> &lines,
                    const std::string &hint);
