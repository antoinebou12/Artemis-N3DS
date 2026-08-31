#include "stream_profile.hpp"

#include "config.hpp"

void apply_stream_profile(PCONFIGURATION config,
                          const StreamProfilePreset &profile) {
    if (config == nullptr) {
        return;
    }

    config->stream.width = profile.width;
    config->stream.height = profile.height;
    config->stream.fps = profile.fps;
    config->stream.bitrate = profile.bitrate_kbps;

    PresentationState presentation = global_presentation_state();
    presentation.mode = profile.presentation_mode;
    presentation.zoom = 1.0f;
    presentation.pan_x = 0.0f;
    presentation.pan_y = 0.0f;
    set_global_presentation_state(presentation);
}
