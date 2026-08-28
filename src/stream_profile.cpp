#include "stream_profile.hpp"

#include <cstring>

namespace {
constexpr std::array<StreamProfilePreset, 5> kProfiles = {{
    {"Low Latency", 400, 240, 60, 1000, PresentationMode::Stretch},
    {"Balanced", 800, 480, 30, 1500, PresentationMode::Stretch},
    {"Quality", 800, 480, 60, 3000, PresentationMode::Stretch},
    {"Desktop", 800, 480, 60, 4000, PresentationMode::Stretch},
    {"Stereo SBS", 800, 240, 60, 2500,
     PresentationMode::StereoSideBySide},
}};
}

const std::array<StreamProfilePreset, 5> &stream_profile_presets() {
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

    PresentationState presentation = global_presentation_state();
    presentation.mode = profile.presentation_mode;
    presentation.zoom = 1.0f;
    presentation.pan_x = 0.0f;
    presentation.pan_y = 0.0f;
    set_global_presentation_state(presentation);
}
