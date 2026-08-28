#include "presentation_state.hpp"

#include <algorithm>

namespace {
PresentationState g_presentation_state{};
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

void normalize_presentation_state(PresentationState &state) {
    state.zoom = std::clamp(state.zoom, 1.0f, 4.0f);
    state.pan_x = std::clamp(state.pan_x, -1.0f, 1.0f);
    state.pan_y = std::clamp(state.pan_y, -1.0f, 1.0f);
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
