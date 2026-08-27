#include "stream_profile.hpp"

#include <cstring>

namespace {
constexpr std::array<StreamProfilePreset, 4> kProfiles = {{
    {"Low Latency", 400, 240, 60, 1000},
    {"Balanced", 800, 480, 30, 1500},
    {"Quality", 800, 480, 60, 3000},
    {"Desktop", 800, 480, 60, 4000},
}};
}

const std::array<StreamProfilePreset, 4> &stream_profile_presets() {
    return kProfiles;
}

const StreamProfilePreset *find_stream_profile(const char *name) {
    if (name == nullptr) {
        return nullptr;
    }

    for (const auto &profile : kProfiles) {
        if (std::strcmp(profile.name, name) == 0) {
            return &profile;
        }
    }
    return nullptr;
}

void apply_stream_profile(PCONFIGURATION config,
                          const StreamProfilePreset &profile) {
    if (config == nullptr) {
        return;
    }

    config->stream.width = profile.width;
    config->stream.height = profile.height;
    config->stream.fps = profile.fps;
    config->stream.bitrate = profile.bitrate_kbps;
}
