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
    PresentationMode mode = PresentationMode::Stretch;
    float zoom = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    bool linear_filtering = true;
};

struct PresentationGeometry {
    float destination_scale_x = 1.0f;
    float destination_scale_y = 1.0f;
    float source_u_min = 0.0f;
    float source_v_min = 0.0f;
    float source_u_max = 1.0f;
    float source_v_max = 1.0f;
};

const char *presentation_mode_name(PresentationMode mode);
void normalize_presentation_state(PresentationState &state);
PresentationGeometry compute_presentation_geometry(int source_width,
                                                   int source_height,
                                                   int destination_width,
                                                   int destination_height,
                                                   PresentationState state);

PresentationState &global_presentation_state();
void set_global_presentation_state(PresentationState state);
