#include "presentation_state.hpp"

#include <algorithm>

const char *presentation_mode_name(PresentationMode mode) {
    switch (mode) {
    case PresentationMode::Fit:
        return "Fit";
    case PresentationMode::Fill:
        return "Fill";
    case PresentationMode::Stretch:
        return "Stretch";
    case PresentationMode::Magnify:
        return "Magnify";
    case PresentationMode::StereoSideBySide:
        return "Stereo SBS";
    }
    return "Unknown";
}

void normalize_presentation_state(PresentationState &state) {
    state.zoom = std::clamp(state.zoom, 1.0f, 4.0f);
    state.pan_x = std::clamp(state.pan_x, -1.0f, 1.0f);
    state.pan_y = std::clamp(state.pan_y, -1.0f, 1.0f);
}
