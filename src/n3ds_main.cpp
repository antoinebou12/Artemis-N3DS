/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2019 Iwan Timmer
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
 */

#include "audio/audio.h"
#include "config.hpp"
#include "host_discovery.hpp"
#include "input/n3ds_input.hpp"
#include "n3ds_ui.hpp"
#include "presentation_state.hpp"
#include "stream_profile.hpp"
#include "stream_telemetry_store.hpp"
#include "system/dispatcher.hpp"
#include "system/n3ds_connection.hpp"
#include "system/pair_record.hpp"
#include "video/video.hpp"

#include <3ds.h>
#include <Limelight.h>

#include <client.h>
#include <http.h>

#include <algorithm>
#include <exception>
#include <malloc.h>
#include <openssl/rand.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define MAX_INPUT_CHAR 96

namespace {
u32 *SOC_buffer = nullptr;

struct SelectedHost {
    std::string address;
    uint16_t port = 47989;
};

struct RemoteApp {
    int id = 0;
    std::string name;
};

const char *bool_text(bool value) { return value ? "On" : "Off"; }

std::string current_profile_name(PCONFIGURATION config) {
    return config->profile != nullptr ? config->profile : "Custom";
}

void fallback_wait_for_button(const std::string &message) {
    consoleSelect(&DebugTouchHandler::topScreen);
    consoleClear();
    printf("%s\n\nPress A or B to continue\n", message.c_str());
    while (aptMainLoop()) {
        gfxFlushBuffers();
        gspWaitForVBlank();
        hidScanInput();
        if (hidKeysDown() & (KEY_A | KEY_B | KEY_START)) {
            return;
        }
    }
}

void show_message(const std::string &title, const std::string &message) {
    if (n3ds_ui_active()) {
        n3ds_ui_message(title, message);
    } else {
        fallback_wait_for_button(title + "\n\n" + message);
    }
}

UiMenuResult show_menu(const std::string &title, const std::string &subtitle,
                       const std::vector<std::string> &items, int selected = 0,
                       const std::string &secondary = "",
                       bool allow_refresh = false) {
    if (n3ds_ui_active()) {
        return n3ds_ui_menu(title, subtitle, items, selected, secondary,
                            allow_refresh);
    }

    int index = items.empty() ? -1 : std::clamp(selected, 0,
                                                (int)items.size() - 1);
    while (aptMainLoop()) {
        consoleSelect(&DebugTouchHandler::topScreen);
        consoleClear();
        printf("%s\n%s\n\n", title.c_str(), subtitle.c_str());
        for (int i = 0; i < (int)items.size(); ++i) {
            printf("%c %s\n", i == index ? '>' : ' ', items[i].c_str());
        }
        printf("\nA Select  B Back%s%s\n",
               allow_refresh ? "  X Refresh" : "",
               secondary.empty() ? "" : "  Y More");
        gfxFlushBuffers();
        gspWaitForVBlank();
        hidScanInput();
        const u32 down = hidKeysDown();
        if (!items.empty() && (down & KEY_DUP)) {
            index = std::max(0, index - 1);
        }
        if (!items.empty() && (down & KEY_DDOWN)) {
            index = std::min((int)items.size() - 1, index + 1);
        }
        if (!items.empty() && (down & KEY_A)) {
            return {UiMenuAction::Select, index};
        }
        if (down & KEY_B) {
            return {UiMenuAction::Back, index};
        }
        if (allow_refresh && (down & KEY_X)) {
            return {UiMenuAction::Refresh, index};
        }
        if (!secondary.empty() && (down & KEY_Y)) {
            return {UiMenuAction::Secondary, index};
        }
    }
    return {UiMenuAction::Back, index};
}

std::string prompt_text(const char *hint, const std::string &initial = "",
                        bool numeric = false) {
    SwkbdState keyboard;
    swkbdInit(&keyboard, numeric ? SWKBD_TYPE_NUMPAD : SWKBD_TYPE_NORMAL, 2,
              -1);
    swkbdSetHintText(&keyboard, hint);
    if (!initial.empty()) {
        swkbdSetInitialText(&keyboard, initial.c_str());
    }

    char buffer[MAX_INPUT_CHAR] = {0};
    const SwkbdButton button =
        swkbdInputText(&keyboard, buffer, sizeof(buffer));
    if (button == SWKBD_BUTTON_NONE) {
        return "";
    }

    std::string result = buffer;
    trim(result);
    return result;
}

int prompt_int(const char *hint, int current) {
    const std::string value = prompt_text(hint, std::to_string(current), true);
    if (value.empty()) {
        return current;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return current;
    }
}

bool parse_host_string(const std::string &input, SelectedHost &host) {
    if (input.empty()) {
        return false;
    }

    host.address = input;
    host.port = 47989;
    const auto colon = input.rfind(':');
    if (colon != std::string::npos && input.find(':') == colon) {
        const std::string port_text = input.substr(colon + 1);
        try {
            const int port = std::stoi(port_text);
            if (port > 0 && port <= 65535) {
                host.address = input.substr(0, colon);
                host.port = (uint16_t)port;
            }
        } catch (...) {
            host.address = input;
            host.port = 47989;
        }
    }
    return !host.address.empty();
}

void apply_host_profile(PCONFIGURATION config, const SelectedHost &host,
                        const STREAM_CONFIGURATION &default_stream,
                        const std::string &default_profile) {
    const std::string saved = get_host_profile(host.address, host.port);
    if (!saved.empty()) {
        if (const auto *profile = find_stream_profile(saved.c_str())) {
            config->profile = const_cast<char *>(profile->name);
            apply_stream_profile(config, *profile);
            return;
        }
    }

    config->stream = default_stream;
    if (!default_profile.empty()) {
        if (const auto *profile = find_stream_profile(default_profile.c_str())) {
            config->profile = const_cast<char *>(profile->name);
        }
    }
}

SelectedHost select_host(PCONFIGURATION config) {
    int selected = 0;
    std::vector<DiscoveredHost> hosts;

    auto refresh = [&]() {
        if (n3ds_ui_active()) {
            n3ds_ui_status("Hosts", "Searching the local network...",
                           {"Scanning GameStream port 47989",
                            "Saved paired hosts are included automatically"},
                           "This normally takes about a second");
        }
        hosts = discover_moonlight_hosts();
        selected = std::min(selected, std::max(0, (int)hosts.size() - 1));
    };

    refresh();

    while (aptMainLoop()) {
        std::vector<std::string> items;
        for (const auto &host : hosts) {
            std::string row = host.saved ? "Saved   " : "Found   ";
            row += host.address + ":" + std::to_string(host.port);
            const std::string profile = get_host_profile(host.address, host.port);
            if (!profile.empty()) {
                row += "   [" + profile + "]";
            }
            items.push_back(row);
        }

        const auto result = show_menu(
            "Hosts", "Sunshine, Apollo and Vibepollo", items, selected,
            "Add Host", true);
        selected = result.index;

        if (result.action == UiMenuAction::Back) {
            return {};
        }
        if (result.action == UiMenuAction::Refresh) {
            refresh();
            continue;
        }
        if (result.action == UiMenuAction::Secondary) {
            const std::string manual =
                prompt_text("PC address, hostname, or address:port");
            SelectedHost host;
            if (parse_host_string(manual, host)) {
                return host;
            }
            continue;
        }
        if (result.action == UiMenuAction::Select && result.index >= 0 &&
            result.index < (int)hosts.size()) {
            return {hosts[result.index].address, hosts[result.index].port};
        }
    }

    return {};
}

void free_app_list(PAPP_LIST list) {
    while (list != nullptr) {
        PAPP_LIST next = list->next;
        free(list->name);
        free(list);
        list = next;
    }
}

bool load_apps_once(PSERVER_DATA server, std::vector<RemoteApp> &apps,
                    std::string &error) {
    PAPP_LIST list = nullptr;
    const int result = gs_applist(server, &list);
    if (result != GS_OK) {
        error = gs_error != nullptr ? gs_error : "GameStream app-list request failed";
        free_app_list(list);
        return false;
    }

    for (PAPP_LIST entry = list; entry != nullptr; entry = entry->next) {
        if (entry->id <= 0 || entry->name == nullptr || entry->name[0] == '\0') {
            continue;
        }
        apps.push_back({entry->id, entry->name});
    }
    free_app_list(list);

    if (apps.empty()) {
        error = "The host returned no valid applications";
        return false;
    }
    return true;
}

bool reload_server(PCONFIGURATION config, PSERVER_DATA server,
                   std::string &error) {
    gs_cleanup();
    const int result = gs_init(server, config->address, config->port,
                               config->key_dir, 0, config->unsupported);
    if (result != GS_OK) {
        error = gs_error != nullptr ? gs_error : "Unable to refresh host session";
        return false;
    }
    return true;
}

bool load_apps_with_retry(PCONFIGURATION config, PSERVER_DATA server,
                          std::vector<RemoteApp> &apps,
                          std::string &error) {
    apps.clear();
    if (load_apps_once(server, apps, error)) {
        return true;
    }

    // Vibepollo/Sunshine can briefly invalidate a reused HTTPS session after
    // pairing or host-side display changes. Refresh serverinfo/cert state once
    // and retry instead of immediately collapsing to "Can't get app list".
    svcSleepThread(250000000LL);
    if (!reload_server(config, server, error)) {
        return false;
    }

    apps.clear();
    return load_apps_once(server, apps, error);
}

void choose_profile(PCONFIGURATION config, const SelectedHost &host) {
    const auto &profiles = stream_profile_presets();
    std::vector<std::string> items;
    int selected = 0;
    for (int i = 0; i < (int)profiles.size(); ++i) {
        const auto &profile = profiles[i];
        std::string row = profile.name;
        row += "   " + std::to_string(profile.width) + "x" +
               std::to_string(profile.height);
        row += "   " + std::to_string(profile.fps) + " FPS";
        row += "   " + std::to_string(profile.bitrate_kbps) + " kbps";
        items.push_back(row);
        if (config->profile != nullptr &&
            strcmp(config->profile, profile.name) == 0) {
            selected = i;
        }
    }

    const auto result = show_menu("Stream Profile", "Optimized 3DS presets",
                                  items, selected);
    if (result.action != UiMenuAction::Select || result.index < 0) {
        return;
    }

    const auto &profile = profiles[(size_t)result.index];
    config->profile = const_cast<char *>(profile.name);
    apply_stream_profile(config, profile);
    set_host_profile(host.address, host.port, profile.name);
}

void choose_presentation(PCONFIGURATION config, const SelectedHost &host) {
    const std::vector<PresentationMode> modes = {
        PresentationMode::Fit,
        PresentationMode::Fill,
        PresentationMode::Stretch,
        PresentationMode::Magnify,
        PresentationMode::StereoSideBySide,
    };
    std::vector<std::string> items;
    int selected = 0;
    for (int i = 0; i < (int)modes.size(); ++i) {
        items.emplace_back(presentation_mode_name(modes[i]));
        if (global_presentation_state().mode == modes[i]) {
            selected = i;
        }
    }

    const auto result = show_menu(
        "Display Mode",
        "Fit preserves aspect; Fill crops; Stereo expects SBS input", items,
        selected);
    if (result.action != UiMenuAction::Select || result.index < 0) {
        return;
    }

    const PresentationMode mode = modes[(size_t)result.index];
    if (mode == PresentationMode::StereoSideBySide) {
        if (const auto *profile = find_stream_profile("Stereo SBS")) {
            config->profile = const_cast<char *>(profile->name);
            apply_stream_profile(config, *profile);
            set_host_profile(host.address, host.port, profile->name);
        }
        return;
    }

    PresentationState state = global_presentation_state();
    state.mode = mode;
    if (mode == PresentationMode::Magnify) {
        state.zoom = std::max(state.zoom, 2.0f);
    } else {
        state.zoom = 1.0f;
        state.pan_x = 0.0f;
        state.pan_y = 0.0f;
    }
    set_global_presentation_state(state);
}

void choose_resolution(PCONFIGURATION config) {
    const std::vector<std::string> items = {
        "400x240   Native logical resolution",
        "800x480   High-detail 2D stream",
        "800x240   Side-by-side stereo source",
        "Custom...",
    };
    const auto result = show_menu("Resolution", "3DS-friendly modes", items);
    if (result.action != UiMenuAction::Select) {
        return;
    }

    switch (result.index) {
    case 0:
        config->stream.width = 400;
        config->stream.height = 240;
        break;
    case 1:
        config->stream.width = 800;
        config->stream.height = 480;
        break;
    case 2:
        config->stream.width = 800;
        config->stream.height = 240;
        set_global_presentation_state(
            {PresentationMode::StereoSideBySide, 1.0f, 0.0f, 0.0f, true});
        break;
    case 3:
        config->stream.width = prompt_int("Stream width", config->stream.width);
        config->stream.height =
            prompt_int("Stream height", config->stream.height);
        break;
    default:
        break;
    }
}

void choose_fps(PCONFIGURATION config) {
    const std::vector<std::string> items = {"30 FPS", "40 FPS", "60 FPS",
                                             "Custom..."};
    const auto result = show_menu("Frame Rate", "Choose stream frame rate",
                                  items);
    if (result.action != UiMenuAction::Select) {
        return;
    }
    if (result.index == 3) {
        config->stream.fps = prompt_int("Frames per second", config->stream.fps);
    } else if (result.index >= 0 && result.index <= 2) {
        const int values[] = {30, 40, 60};
        config->stream.fps = values[result.index];
    }
}

void choose_bitrate(PCONFIGURATION config) {
    const int values[] = {1000, 1500, 2000, 2500, 3000, 4000};
    std::vector<std::string> items;
    for (int value : values) {
        items.push_back(std::to_string(value) + " kbps");
    }
    items.push_back("Custom...");

    const auto result = show_menu("Bitrate", "Higher needs a stronger Wi-Fi link",
                                  items);
    if (result.action != UiMenuAction::Select) {
        return;
    }
    if (result.index == (int)items.size() - 1) {
        config->stream.bitrate =
            prompt_int("Bitrate in kbps", config->stream.bitrate);
    } else if (result.index >= 0 && result.index < 6) {
        config->stream.bitrate = values[result.index];
    }
}

void choose_decoder(PCONFIGURATION config) {
    const std::vector<std::string> items = {
        "Hardware MVD   New 3DS recommended",
        "Software       Compatibility fallback",
        "Video disabled",
    };
    const auto result = show_menu("Video Decoder", "Hardware MVD is fastest",
                                  items, (int)config->video_decoder);
    if (result.action == UiMenuAction::Select && result.index >= 0) {
        config->video_decoder = (VIDEO_DECODER_TYPE)result.index;
    }
}

void video_settings(PCONFIGURATION config) {
    int selected = 0;
    while (aptMainLoop()) {
        std::vector<std::string> items = {
            "Resolution        " + std::to_string(config->stream.width) + "x" +
                std::to_string(config->stream.height),
            "Frame rate        " + std::to_string(config->stream.fps) + " FPS",
            "Bitrate           " + std::to_string(config->stream.bitrate) +
                " kbps",
            std::string("Decoder           ") +
                (config->video_decoder == HARDWARE_VIDEO_DECODER
                     ? "Hardware MVD"
                     : config->video_decoder == SOFTWARE_VIDEO_DECODER
                           ? "Software"
                           : "Disabled"),
        };
        const auto result =
            show_menu("Video", "Resolution, frame rate and decoder", items,
                      selected);
        selected = result.index;
        if (result.action == UiMenuAction::Back) {
            return;
        }
        if (result.action != UiMenuAction::Select) {
            continue;
        }
        switch (result.index) {
        case 0:
            choose_resolution(config);
            break;
        case 1:
            choose_fps(config);
            break;
        case 2:
            choose_bitrate(config);
            break;
        case 3:
            choose_decoder(config);
            break;
        default:
            break;
        }
    }
}

void control_settings(PCONFIGURATION config) {
    int selected = 0;
    while (aptMainLoop()) {
        std::vector<std::string> items = {
            std::string("Motion controls       ") + bool_text(config->motion_controls),
            std::string("Xbox face layout       ") + bool_text(config->swap_face_buttons),
            std::string("Swap L/ZL and R/ZR     ") +
                bool_text(config->swap_triggers_and_shoulders),
            std::string("ZL/ZR as mouse         ") +
                bool_text(config->use_triggers_for_mouse),
            std::string("View only              ") + bool_text(config->viewonly),
        };
        const auto result =
            show_menu("Controls", "Controller and input behavior", items,
                      selected);
        selected = result.index;
        if (result.action == UiMenuAction::Back) {
            return;
        }
        if (result.action != UiMenuAction::Select) {
            continue;
        }
        switch (result.index) {
        case 0:
            config->motion_controls = !config->motion_controls;
            break;
        case 1:
            config->swap_face_buttons = !config->swap_face_buttons;
            break;
        case 2:
            config->swap_triggers_and_shoulders =
                !config->swap_triggers_and_shoulders;
            break;
        case 3:
            config->use_triggers_for_mouse =
                !config->use_triggers_for_mouse;
            break;
        case 4:
            config->viewonly = !config->viewonly;
            break;
        default:
            break;
        }
    }
}

void audio_host_settings(PCONFIGURATION config) {
    int selected = 0;
    while (aptMainLoop()) {
        std::vector<std::string> items = {
            std::string("Play audio on host     ") + bool_text(config->localaudio),
            std::string("Optimize host settings ") + bool_text(config->sops),
            std::string("Quit app after stream  ") + bool_text(config->quitappafter),
        };
        const auto result = show_menu("Audio & Host", "Host-side behavior",
                                      items, selected);
        selected = result.index;
        if (result.action == UiMenuAction::Back) {
            return;
        }
        if (result.action != UiMenuAction::Select) {
            continue;
        }
        if (result.index == 0) {
            config->localaudio = !config->localaudio;
        } else if (result.index == 1) {
            config->sops = !config->sops;
        } else if (result.index == 2) {
            config->quitappafter = !config->quitappafter;
        }
    }
}

void advanced_settings(PCONFIGURATION config) {
    int selected = 0;
    while (aptMainLoop()) {
        const auto state = global_presentation_state();
        std::vector<std::string> items = {
            "Packet size       " + std::to_string(config->stream.packetSize),
            std::string("Linear filtering  ") + bool_text(state.linear_filtering),
        };
        const auto result = show_menu("Advanced", "Low-level stream options",
                                      items, selected);
        selected = result.index;
        if (result.action == UiMenuAction::Back) {
            return;
        }
        if (result.action != UiMenuAction::Select) {
            continue;
        }
        if (result.index == 0) {
            config->stream.packetSize =
                prompt_int("Network packet size", config->stream.packetSize);
        } else if (result.index == 1) {
            PresentationState next = state;
            next.linear_filtering = !next.linear_filtering;
            set_global_presentation_state(next);
        }
    }
}

void stream_settings(PCONFIGURATION config, const SelectedHost &host) {
    int selected = 0;
    while (aptMainLoop()) {
        std::vector<std::string> items = {
            "Stream Profile    " + current_profile_name(config),
            std::string("Display Mode      ") +
                presentation_mode_name(global_presentation_state().mode),
            "Video",
            "Controls",
            "Audio & Host",
            "Advanced",
        };

        const auto result = show_menu("Settings", host.address, items, selected);
        selected = result.index;
        if (result.action == UiMenuAction::Back) {
            char *path = (char *)MOONLIGHT_3DS_PATH "/moonlight.conf";
            config_save(path, config);
            return;
        }
        if (result.action != UiMenuAction::Select) {
            continue;
        }

        switch (result.index) {
        case 0:
            choose_profile(config, host);
            break;
        case 1:
            choose_presentation(config, host);
            break;
        case 2:
            video_settings(config);
            break;
        case 3:
            control_settings(config);
            break;
        case 4:
            audio_host_settings(config);
            break;
        case 5:
            advanced_settings(config);
            break;
        default:
            break;
        }
    }
}

void show_host_info(PCONFIGURATION config, PSERVER_DATA server,
                    const SelectedHost &host) {
    std::string message;
    message += "Address: " + host.address + ":" + std::to_string(host.port) + "\n";
    message += "Pairing: ";
    message += server->paired ? "Paired\n" : "Not paired\n";
    message += "Profile: " + current_profile_name(config) + "\n";
    message += "GPU: ";
    message += server->gpuType != nullptr ? server->gpuType : "Unknown";
    message += "\nGameStream: ";
    message += server->gsVersion != nullptr ? server->gsVersion : "Unknown";
    message += "\nHost app: ";
    message += server->serverInfo.serverInfoAppVersion != nullptr
                   ? server->serverInfo.serverInfoAppVersion
                   : "Unknown";
    show_message("Host Details", message);
}

static inline void dispatch_loop(void *_unused_) {
    auto dispatcher = MessageDispatcher::get_instance();
    auto connection_listener = N3dsConnectionListener::get_instance();
    while (!connection_listener->is_connection_closed()) {
        gspWaitForAnyEvent();
        dispatcher->dispatch_all();
    }
}

static inline void input_loop(void *input_handler_in) {
    N3dsInput *input_handler = static_cast<N3dsInput *>(input_handler_in);
    auto connection_listener = N3dsConnectionListener::get_instance();
    input_handler->force_touchscreen_menu();
    while (!connection_listener->is_connection_closed()) {
        gspWaitForAnyEvent();
        input_handler->n3dsinput_handle_event();
    }
}

static inline void stream_loop(PCONFIGURATION config,
                               N3dsConnectionListener *connection_listener,
                               std::shared_ptr<N3dsInput> input_handler) {
    size_t stack_size = 0x20000;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (!config->viewonly && input_handler != nullptr) {
        threadCreate(input_loop, input_handler.get(), stack_size, priority, -1,
                     true);
    }
    threadCreate(dispatch_loop, nullptr, stack_size, priority, -1, true);

    while (!connection_listener->is_connection_closed() && aptMainLoop()) {
        gspWaitForAnyEvent();
        if (aptShouldClose()) {
            connection_listener->connection_terminated(0);
        }
    }
}

bool start_stream(PSERVER_DATA server, PCONFIGURATION config,
                  const RemoteApp &app) {
    n3ds_ui_status("Starting Stream", app.name,
                   {std::to_string(config->stream.width) + "x" +
                        std::to_string(config->stream.height) + " at " +
                        std::to_string(config->stream.fps) + " FPS",
                    std::to_string(config->stream.bitrate) + " kbps",
                    presentation_mode_name(global_presentation_state().mode)},
                   "Preparing host session...");

    const int gamepad_mask = 1;
    const int launch_result =
        gs_start_app(server, &config->stream, app.id, config->sops,
                     config->localaudio, gamepad_mask);
    if (launch_result < 0) {
        std::string error = gs_error != nullptr ? gs_error : "Unable to launch app";
        show_message("Launch Failed", error);
        return false;
    }

    AUDIO_RENDERER_CALLBACKS *audio_callbacks =
        config->localaudio ? &audio_callbacks_mock : &audio_callbacks_n3ds;

    PDECODER_RENDERER_CALLBACKS video_callbacks = &decoder_callbacks_mock;
    switch (config->video_decoder) {
    case VIDEO_DECODER_TYPE::HARDWARE_VIDEO_DECODER:
        video_callbacks = &decoder_callbacks_n3ds_mvd;
        break;
    case VIDEO_DECODER_TYPE::SOFTWARE_VIDEO_DECODER:
        video_callbacks = &decoder_callbacks_n3ds;
        break;
    default:
        break;
    }

    config->stream.supportedVideoFormats = VIDEO_FORMAT_H264;
    set_global_stream_bitrate((uint32_t)std::max(0, config->stream.bitrate));

    std::shared_ptr<N3dsInput> input_handler = nullptr;
    if (!config->viewonly) {
        input_handler = std::make_shared<N3dsInput>(
            config->stream.width, config->stream.height,
            config->swap_face_buttons, config->swap_triggers_and_shoulders,
            config->use_triggers_for_mouse);
    }

    // Citro2D owns the GPU only for the shell. Hand it back before the legacy
    // PICA200 video renderer starts, then restore the shell after streaming.
    n3ds_ui_shutdown();
    consoleSelect(&DebugTouchHandler::topScreen);
    consoleClear();

    auto connection_listener =
        N3dsConnectionListener::create_instance(config->motion_controls);
    const int status =
        LiStartConnection(&server->serverInfo, &config->stream,
                          &n3ds_connection_callbacks, video_callbacks,
                          audio_callbacks, nullptr, DISPLAY_FULLSCREEN,
                          config->audio_device, 0);

    if (status != 0) {
        n3ds_connection_callbacks.connectionTerminated(status);
        N3dsConnectionListener::destroy_instance();
        n3ds_ui_init();
        show_message("Connection Failed",
                     "Moonlight connection error " + std::to_string(status));
        return false;
    }

    stream_loop(config, connection_listener, input_handler);

    LiStopConnection();
    N3dsConnectionListener::destroy_instance();
    n3ds_ui_init();

    if (config->quitappafter) {
        gs_quit_app(server);
    }
    return true;
}

void app_browser(PCONFIGURATION config, PSERVER_DATA server,
                 const SelectedHost &host) {
    int selected = 0;
    bool reload = true;
    std::vector<RemoteApp> apps;

    while (aptMainLoop()) {
        if (reload) {
            n3ds_ui_status("Applications", host.address,
                           {"Loading applications from the host..."},
                           "Sunshine / Apollo / Vibepollo applist");
            std::string error;
            if (!load_apps_with_retry(config, server, apps, error)) {
                show_message(
                    "Application List Failed",
                    error +
                        "\n\nThe client retried once with a fresh host session. "
                        "Modern UUID/ArtVersion fields are supported. Check that "
                        "Vibepollo has at least one named application and restart "
                        "the host service if its /applist cache is stale.");
                return;
            }
            reload = false;
            selected = std::min(selected, std::max(0, (int)apps.size() - 1));
        }

        std::vector<std::string> items;
        for (const auto &app : apps) {
            items.push_back(app.name);
        }

        std::string subtitle = host.address + "   " + current_profile_name(config);
        const auto result =
            show_menu("Applications", subtitle, items, selected, "Settings", true);
        selected = result.index;

        if (result.action == UiMenuAction::Back) {
            return;
        }
        if (result.action == UiMenuAction::Refresh) {
            reload = true;
            continue;
        }
        if (result.action == UiMenuAction::Secondary) {
            stream_settings(config, host);
            continue;
        }
        if (result.action == UiMenuAction::Select && result.index >= 0 &&
            result.index < (int)apps.size()) {
            start_stream(server, config, apps[(size_t)result.index]);
            reload = true;
        }
    }
}

void pair_host(PCONFIGURATION config, PSERVER_DATA server,
               const SelectedHost &host) {
    http_set_timeout_s(5 * 60);

    char pin[5];
    snprintf(pin, sizeof(pin), "%d%d%d%d", (unsigned)random() % 10,
             (unsigned)random() % 10, (unsigned)random() % 10,
             (unsigned)random() % 10);

    n3ds_ui_status("Pair Host", host.address,
                   {std::string("PIN: ") + pin,
                    "Enter this PIN in the host web interface"},
                   "Waiting for pairing approval...");

    const int result = gs_pair(server, pin);
    http_set_timeout_s(60);

    if (result == GS_OK) {
        add_pair_address(host.address, host.port);
        show_message("Pairing Complete", "The host is now paired with Artemis 3DS.");
    } else {
        show_message("Pairing Failed",
                     gs_error != nullptr ? gs_error : "Unknown pairing error");
    }
}

bool connect_host(PCONFIGURATION config, PSERVER_DATA server,
                  const SelectedHost &host) {
    n3ds_ui_status("Connecting", host.address,
                   {"Opening GameStream session..."}, "Please wait");

    gs_cleanup();
    config->address = const_cast<char *>(host.address.c_str());
    config->port = host.port;

    const int status = gs_init(server, config->address, config->port,
                               config->key_dir, 0, config->unsupported);
    if (status != GS_OK) {
        show_message("Host Unavailable",
                     gs_error != nullptr ? gs_error
                                         : "Unable to initialize GameStream host");
        return false;
    }

    if (server->paired) {
        add_pair_address(host.address, host.port);
    }
    return true;
}

void host_screen(PCONFIGURATION config, PSERVER_DATA server,
                 const SelectedHost &host) {
    int selected = 0;
    while (aptMainLoop()) {
        std::vector<std::string> items;
        if (server->paired) {
            items = {
                "Applications",
                "Settings",
                "Host Details",
                "Quit Active Host App",
                "Unpair Host",
            };
        } else {
            items = {
                "Pair Host",
                "Settings",
                "Host Details",
            };
        }

        std::string subtitle = host.address;
        subtitle += server->paired ? "   Paired" : "   Not paired";
        subtitle += "   " + current_profile_name(config);
        const auto result = show_menu("Host", subtitle, items, selected,
                                      "Refresh", false);
        selected = result.index;

        if (result.action == UiMenuAction::Back) {
            return;
        }
        if (result.action == UiMenuAction::Secondary) {
            std::string error;
            if (!reload_server(config, server, error)) {
                show_message("Refresh Failed", error);
            }
            continue;
        }
        if (result.action != UiMenuAction::Select) {
            continue;
        }

        if (server->paired) {
            switch (result.index) {
            case 0:
                app_browser(config, server, host);
                break;
            case 1:
                stream_settings(config, host);
                break;
            case 2:
                show_host_info(config, server, host);
                break;
            case 3:
                if (gs_quit_app(server) == GS_OK) {
                    show_message("Host", "Quit request sent successfully.");
                } else {
                    show_message("Quit Failed",
                                 gs_error != nullptr ? gs_error
                                                     : "Host rejected quit request");
                }
                break;
            case 4:
                if (gs_unpair(server) == GS_OK) {
                    remove_pair_address(host.address, host.port);
                    server->paired = false;
                    show_message("Host", "Host unpaired successfully.");
                } else {
                    show_message("Unpair Failed",
                                 gs_error != nullptr ? gs_error
                                                     : "Host rejected unpair request");
                }
                break;
            default:
                break;
            }
        } else {
            switch (result.index) {
            case 0:
                pair_host(config, server, host);
                if (server->paired) {
                    selected = 0;
                }
                break;
            case 1:
                stream_settings(config, host);
                break;
            case 2:
                show_host_info(config, server, host);
                break;
            default:
                break;
            }
        }
    }
}

void init_3ds() {
    Result status = 0;
    acInit();
    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    consoleInit(GFX_TOP, &DebugTouchHandler::topScreen);
    consoleInit(GFX_BOTTOM, &DebugTouchHandler::bottomScreen);
    consoleSelect(&DebugTouchHandler::topScreen);

    osSetSpeedupEnable(true);
    aptSetSleepAllowed(false);
    aptInit();

    SOC_buffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    status = socInit(SOC_buffer, SOC_BUFFERSIZE);
    if (R_FAILED(status)) {
        fallback_wait_for_button("Unable to initialize 3DS networking");
        exit(1);
    }

    status = ndmuInit();
    status |= NDMU_EnterExclusiveState(NDM_EXCLUSIVE_STATE_INFRASTRUCTURE);
    status |= NDMU_LockState();
    if (R_FAILED(status)) {
        fallback_wait_for_button("Warning: failed to lock Wi-Fi infrastructure state");
    }

    n3ds_ui_init();
}

void n3ds_exit_handler() {
    n3ds_ui_shutdown();
    gs_cleanup();
    NDMU_UnlockState();
    NDMU_LeaveExclusiveState();
    ndmuExit();
    irrstExit();
    SOCU_ShutdownSockets();
    SOCU_CloseSockets();
    socExit();
    free(SOC_buffer);
    romfsExit();
    aptExit();
    gfxExit();
    acExit();
}
} // namespace

int main_loop(int argc, char *argv[]) {
    init_3ds();
    atexit(n3ds_exit_handler);

    CONFIGURATION config;
    config_parse(argc, argv, &config);
    const STREAM_CONFIGURATION default_stream = config.stream;
    const std::string default_profile =
        config.profile != nullptr ? config.profile : "";

    while (aptMainLoop()) {
        SelectedHost host = select_host(&config);
        if (host.address.empty()) {
            const auto result = show_menu(
                "Artemis 3DS", "No host selected",
                {"Return to host search", "Exit Artemis 3DS"}, 0);
            if (result.action == UiMenuAction::Select && result.index == 1) {
                break;
            }
            continue;
        }

        apply_host_profile(&config, host, default_stream, default_profile);

        SERVER_DATA server{};
        if (!connect_host(&config, &server, host)) {
            continue;
        }

        host_screen(&config, &server, host);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    try {
        return main_loop(argc, argv);
    } catch (const std::exception &ex) {
        if (n3ds_ui_active()) {
            show_message("Artemis 3DS Error", ex.what());
        } else {
            printf("Artemis 3DS crashed: %s\n", ex.what());
        }
        return 1;
    } catch (const std::string &ex) {
        if (n3ds_ui_active()) {
            show_message("Artemis 3DS Error", ex);
        } else {
            printf("Artemis 3DS crashed: %s\n", ex.c_str());
        }
        return 1;
    } catch (...) {
        if (n3ds_ui_active()) {
            show_message("Artemis 3DS Error", "Unknown fatal error");
        } else {
            printf("Artemis 3DS crashed with an unknown error\n");
        }
        return 1;
    }
}
