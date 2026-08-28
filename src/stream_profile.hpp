#pragma once

#include "config.hpp"
#include "presentation_state.hpp"

#include <array>
#include <cstddef>

struct StreamProfilePreset {
    const char *name;
    int width;
    int height;
    int fps;
    int bitrate_kbps;
    PresentationMode presentation_mode;
};

const std::array<StreamProfilePreset, 5> &stream_profile_presets();
const StreamProfilePreset *find_stream_profile(const char *name);
void apply_stream_profile(PCONFIGURATION config,
                          const StreamProfilePreset &profile);
