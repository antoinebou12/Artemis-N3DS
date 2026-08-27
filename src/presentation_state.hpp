#pragma once

#include <cstdint>

enum class PresentationMode : std::uint8_t {
    Fit = 0,
    Fill,
    Stretch,
    Magnify,
    StereoSideBySide,
};

struct PresentationState {
    PresentationMode mode = PresentationMode::Fit;
    float zoom = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    bool linear_filtering = true;
};

const char *presentation_mode_name(PresentationMode mode);
void normalize_presentation_state(PresentationState &state);
