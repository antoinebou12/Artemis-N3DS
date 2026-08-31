#pragma once

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
const char *stream_profile_hint(const StreamProfilePreset &profile);
bool stream_profile_matches(const StreamProfilePreset &profile, int width,
                            int height, int fps, int bitrate_kbps,
                            PresentationMode presentation_mode);

struct _CONFIGURATION;
typedef struct _CONFIGURATION *PCONFIGURATION;

void apply_stream_profile(PCONFIGURATION config,
                          const StreamProfilePreset &profile);
