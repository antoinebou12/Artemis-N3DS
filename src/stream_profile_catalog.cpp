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

const char *stream_profile_hint(const StreamProfilePreset &profile) {
    if (std::strcmp(profile.name, "Low Latency") == 0) {
        return "Best compatibility";
    }
    if (std::strcmp(profile.name, "Balanced") == 0) {
        return "Reliable 3D quality";
    }
    if (std::strcmp(profile.name, "Quality") == 0) {
        return "Smooth New 3DS";
    }
    if (std::strcmp(profile.name, "Desktop") == 0) {
        return "Sharp New 3DS";
    }
    return "Stereoscopic source";
}

bool stream_profile_matches(const StreamProfilePreset &profile, int width,
                            int height, int fps, int bitrate_kbps,
                            PresentationMode presentation_mode) {
    return profile.width == width && profile.height == height &&
           profile.fps == fps && profile.bitrate_kbps == bitrate_kbps &&
           profile.presentation_mode == presentation_mode;
}
