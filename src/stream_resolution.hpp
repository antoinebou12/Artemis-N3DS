#pragma once

#include "presentation_state.hpp"

#include <array>

struct StreamResolutionPreset {
    int width;
    int height;
    const char *label;
    PresentationMode mode;
};

// These are stream input sizes, not additional physical screen pixels.
// Keep every preset inside the existing 1024x512 decoder/texture budget.
inline constexpr std::array<StreamResolutionPreset, 11> kStreamResolutions = {{
    {400, 240, "400x240   Native 3DS / 3DS XL", PresentationMode::Fit},
    {480, 288, "480x288   Light detail boost", PresentationMode::Fit},
    {640, 384, "640x384   Balanced 5:3", PresentationMode::Fit},
    {800, 480, "800x480   High detail 5:3", PresentationMode::Fit},
    {320, 240, "320x240   Low bandwidth 4:3", PresentationMode::Fit},
    {640, 480, "640x480   Classic 4:3", PresentationMode::Fit},
    {640, 360, "640x360   Widescreen 16:9", PresentationMode::Fit},
    {768, 432, "768x432   Widescreen detail", PresentationMode::Fit},
    {896, 504, "896x504   Highest 16:9 detail", PresentationMode::Fit},
    {1024, 512, "1024x512  Maximum input (2:1)", PresentationMode::Fit},
    {800, 240, "800x240   Stereo SBS source", PresentationMode::StereoSideBySide},
}};
