#pragma once

#include "config.hpp"

#include <array>
#include <cstddef>

struct StreamProfilePreset {
    const char *name;
    int width;
    int height;
    int fps;
    int bitrate_kbps;
};

const std::array<StreamProfilePreset, 4> &stream_profile_presets();
const StreamProfilePreset *find_stream_profile(const char *name);
void apply_stream_profile(PCONFIGURATION config,
                          const StreamProfilePreset &profile);
