#pragma once

#include <Limelight.h>
#include <array>

struct HostShortcut {
    const char *label;
    short key;
    char modifiers;
};

struct HostShortcutPage {
    const char *label;
    std::array<HostShortcut, 8> shortcuts;
};

inline constexpr std::array<HostShortcutPage, 3> kHostShortcutPages = {{
    {"WINDOWS 1/3", {{
        {"ALT TAB", 0x09, MODIFIER_ALT},
        {"WIN TAB", 0x09, MODIFIER_META},
        {"SHOW DESKTOP", 0x44, MODIFIER_META},
        {"EXPLORER", 0x45, MODIFIER_META},
        {"RUN", 0x52, MODIFIER_META},
        {"LOCK", 0x4C, MODIFIER_META},
        {"START", 0x1B, MODIFIER_CTRL},
        {"TASK MGR", 0x1B, MODIFIER_CTRL | MODIFIER_SHIFT},
    }}},
    {"DISPLAYS 2/3", {{
        {"MONITOR LEFT", 0x25, MODIFIER_META | MODIFIER_SHIFT},
        {"MONITOR RIGHT", 0x27, MODIFIER_META | MODIFIER_SHIFT},
        {"SNAP LEFT", 0x25, MODIFIER_META},
        {"SNAP RIGHT", 0x27, MODIFIER_META},
        {"MAXIMIZE", 0x26, MODIFIER_META},
        {"MINIMIZE", 0x28, MODIFIER_META},
        {"DESKTOP LEFT", 0x25, MODIFIER_META | MODIFIER_CTRL},
        {"DESKTOP RIGHT", 0x27, MODIFIER_META | MODIFIER_CTRL},
    }}},
    {"MAC 3/3", {{
        {"CMD TAB", 0x09, MODIFIER_META},
        {"SPOTLIGHT", 0x20, MODIFIER_META},
        {"COPY", 0x43, MODIFIER_META},
        {"PASTE", 0x56, MODIFIER_META},
        {"CLOSE", 0x57, MODIFIER_META},
        {"QUIT", 0x51, MODIFIER_META},
        {"SCREENSHOT", 0x34, MODIFIER_META | MODIFIER_SHIFT},
        {"FORCE QUIT", 0x1B, MODIFIER_META | MODIFIER_ALT},
    }}},
}};

// Send real modifier presses and release in reverse order. In particular,
// GameStream hosts can drop events that only carry the META modifier flag.
// Explicit key events also ensure the host never retains a shortcut modifier.
template <typename SendKey>
void send_host_shortcut(const HostShortcut &shortcut, SendKey send_key) {
    constexpr std::array<HostShortcut, 4> modifiers = {{
        {"CTRL", 0xA2, MODIFIER_CTRL},
        {"SHIFT", 0xA0, MODIFIER_SHIFT},
        {"ALT", 0xA4, MODIFIER_ALT},
        {"WIN/CMD", 0x5B, MODIFIER_META},
    }};
    for (const auto &modifier : modifiers) {
        if (shortcut.modifiers & modifier.modifiers) {
            send_key(modifier.key, KEY_ACTION_DOWN, 0);
        }
    }
    send_key(shortcut.key, KEY_ACTION_DOWN, 0);
    send_key(shortcut.key, KEY_ACTION_UP, 0);
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
        if (shortcut.modifiers & it->modifiers) {
            send_key(it->key, KEY_ACTION_UP, 0);
        }
    }
}
