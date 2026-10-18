#include "presentation_state.hpp"

#include <algorithm>
#include <cstring>

namespace {
PresentationState g_presentation_state{};
}

PresentationState text_readability_presentation(bool magnify) {
    PresentationState state;
    state.mode = magnify ? PresentationMode::Magnify : PresentationMode::Fit;
    state.zoom = magnify ? 2.0f : 1.0f;
    state.linear_filtering = false;
    state.stereo_depth = StereoDepth::Off;
    return state;
}

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

bool presentation_mode_from_name(const char *name, PresentationMode &mode) {
    if (name == nullptr) {
        return false;
    }
    if (strcmp(name, "Fit") == 0) {
        mode = PresentationMode::Fit;
        return true;
    }
    if (strcmp(name, "Fill") == 0) {
        mode = PresentationMode::Fill;
        return true;
    }
    if (strcmp(name, "Stretch") == 0) {
        mode = PresentationMode::Stretch;
        return true;
    }
    if (strcmp(name, "Magnify") == 0) {
        mode = PresentationMode::Magnify;
        return true;
    }
    if (strcmp(name, "Stereo SBS") == 0 || strcmp(name, "StereoSideBySide") == 0) {
        mode = PresentationMode::StereoSideBySide;
        return true;
    }
    return false;
}

const char *stereo_depth_name(StereoDepth depth) {
    switch (depth) {
    case StereoDepth::Off:
        return "Off";
    case StereoDepth::Flat:
        return "Flat";
    case StereoDepth::Soft:
        return "Soft";
    case StereoDepth::Full:
        return "Full";
    }
    return "Soft";
}

bool stereo_depth_from_name(const char *name, StereoDepth &depth) {
    if (name == nullptr) {
        return false;
    }
    if (strcmp(name, "Off") == 0) {
        depth = StereoDepth::Off;
        return true;
    }
    if (strcmp(name, "Flat") == 0) {
        depth = StereoDepth::Flat;
        return true;
    }
    if (strcmp(name, "Soft") == 0) {
        depth = StereoDepth::Soft;
        return true;
    }
    if (strcmp(name, "Full") == 0) {
        depth = StereoDepth::Full;
        return true;
    }
    return false;
}

StereoDepth next_stereo_depth(StereoDepth depth) {
    switch (depth) {
    case StereoDepth::Off:
        return StereoDepth::Flat;
    case StereoDepth::Flat:
        return StereoDepth::Soft;
    case StereoDepth::Soft:
        return StereoDepth::Full;
    case StereoDepth::Full:
        return StereoDepth::Off;
    }
    return StereoDepth::Soft;
}

float stereo_eye_offset(StereoDepth depth, float slider_state) {
    // Fractions of the visible source window, per eye. On a 400-column eye
    // Full is about two pixels: enough to read as depth, small enough that the
    // eyes never have to diverge.
    float scale = 0.0f;
    switch (depth) {
    case StereoDepth::Off:
        return 0.0f;
    case StereoDepth::Flat:
        scale = 0.0015f;
        break;
    case StereoDepth::Soft:
        scale = 0.0030f;
        break;
    case StereoDepth::Full:
        scale = 0.0050f;
        break;
    }

    if (!(slider_state > 0.0f)) {
        return 0.0f; // slider down (or NaN): the console is showing one eye
    }
    return scale * std::clamp(slider_state, 0.0f, 1.0f);
}

void normalize_presentation_state(PresentationState &state) {
    state.zoom = std::clamp(state.zoom, 1.0f, 4.0f);
    state.pan_x = std::clamp(state.pan_x, -1.0f, 1.0f);
    state.pan_y = std::clamp(state.pan_y, -1.0f, 1.0f);
    if (state.stereo_depth != StereoDepth::Off &&
        state.stereo_depth != StereoDepth::Flat &&
        state.stereo_depth != StereoDepth::Soft &&
        state.stereo_depth != StereoDepth::Full) {
        state.stereo_depth = StereoDepth::Soft;
    }
}

PresentationGeometry compute_presentation_geometry(int source_width,
                                                   int source_height,
                                                   int destination_width,
                                                   int destination_height,
                                                   PresentationState state) {
    normalize_presentation_state(state);
    PresentationGeometry geometry{};

    if (source_width <= 0 || source_height <= 0 || destination_width <= 0 ||
        destination_height <= 0) {
        return geometry;
    }

    const float source_aspect =
        static_cast<float>(source_width) / static_cast<float>(source_height);
    const float destination_aspect = static_cast<float>(destination_width) /
                                     static_cast<float>(destination_height);

    switch (state.mode) {
    case PresentationMode::Fit:
        if (source_aspect > destination_aspect) {
            geometry.destination_scale_y = destination_aspect / source_aspect;
        } else if (source_aspect < destination_aspect) {
            geometry.destination_scale_x = source_aspect / destination_aspect;
        }
        break;

    case PresentationMode::Fill:
        if (source_aspect > destination_aspect) {
            const float visible = destination_aspect / source_aspect;
            geometry.source_u_min = (1.0f - visible) * 0.5f;
            geometry.source_u_max = 1.0f - geometry.source_u_min;
        } else if (source_aspect < destination_aspect) {
            const float visible = source_aspect / destination_aspect;
            geometry.source_v_min = (1.0f - visible) * 0.5f;
            geometry.source_v_max = 1.0f - geometry.source_v_min;
        }
        break;

    case PresentationMode::Magnify: {
        const float visible_x = 1.0f / state.zoom;
        const float visible_y = 1.0f / state.zoom;
        const float max_offset_x = (1.0f - visible_x) * 0.5f;
        const float max_offset_y = (1.0f - visible_y) * 0.5f;
        const float center_x = 0.5f + state.pan_x * max_offset_x;
        const float center_y = 0.5f + state.pan_y * max_offset_y;
        geometry.source_u_min = center_x - visible_x * 0.5f;
        geometry.source_u_max = center_x + visible_x * 0.5f;
        geometry.source_v_min = center_y - visible_y * 0.5f;
        geometry.source_v_max = center_y + visible_y * 0.5f;
        break;
    }

    case PresentationMode::Stretch:
    case PresentationMode::StereoSideBySide:
        break;
    }

    return geometry;
}

PresentationState &global_presentation_state() { return g_presentation_state; }

void set_global_presentation_state(PresentationState state) {
    normalize_presentation_state(state);
    g_presentation_state = state;
}
