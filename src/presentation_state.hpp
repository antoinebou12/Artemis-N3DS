#pragma once

#include <cstdint>

enum class PresentationMode : std::uint8_t {
    Fit = 0,
    Fill,
    Stretch,
    Magnify,
    StereoSideBySide,
};

// Stereo depth built from an ordinary flat stream: the same frame is drawn to
// both eyes with a small opposite horizontal offset, so the scene sits just
// behind the screen. A flat source cannot carry per-object depth, so this is
// deliberately subtle — well under a percent of image width.
enum class StereoDepth : std::uint8_t {
    Off = 0, // both eyes identical
    Flat,    // barely there
    Soft,    // default
    Full,    // as much as stays comfortable
};

struct PresentationState {
    PresentationMode mode = PresentationMode::Stretch;
    float zoom = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    bool linear_filtering = true;
    StereoDepth stereo_depth = StereoDepth::Soft;
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
bool presentation_mode_from_name(const char *name, PresentationMode &mode);
const char *stereo_depth_name(StereoDepth depth);
bool stereo_depth_from_name(const char *name, StereoDepth &depth);
StereoDepth next_stereo_depth(StereoDepth depth);
// How far *each* eye's source window slides, as a fraction of that window's
// width; the eyes move in opposite directions, so total parallax is twice
// this. Scales with the physical 3D slider (0..1) and is 0 when the slider is
// down or depth is Off.
float stereo_eye_offset(StereoDepth depth, float slider_state);
void normalize_presentation_state(PresentationState &state);
PresentationGeometry compute_presentation_geometry(int source_width,
                                                   int source_height,
                                                   int destination_width,
                                                   int destination_height,
                                                   PresentationState state);

PresentationState &global_presentation_state();
void set_global_presentation_state(PresentationState state);

// Flat, unfiltered text; Magnify makes an 800x480 source readable at 2x.
PresentationState text_readability_presentation(bool magnify);
