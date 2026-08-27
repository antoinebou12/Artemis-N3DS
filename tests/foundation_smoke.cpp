#include "presentation_state.hpp"
#include "stream_telemetry.hpp"

#include <cassert>
#include <cmath>
#include <cstring>

static bool nearly_equal(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) <= epsilon;
}

int main() {
    PresentationState presentation;
    presentation.zoom = 8.0f;
    presentation.pan_x = -2.0f;
    presentation.pan_y = 2.0f;
    normalize_presentation_state(presentation);

    assert(nearly_equal(presentation.zoom, 4.0f));
    assert(nearly_equal(presentation.pan_x, -1.0f));
    assert(nearly_equal(presentation.pan_y, 1.0f));
    assert(std::strcmp(presentation_mode_name(PresentationMode::StereoSideBySide),
                       "Stereo SBS") == 0);

    StreamTelemetry telemetry;
    telemetry.push({4.0f, 2.0f, 16.0f, 60.0f, 1500, 0});
    telemetry.push({6.0f, 4.0f, 20.0f, 50.0f, 1500, 1});

    const auto summary = telemetry.summary();
    assert(summary.sample_count == 2);
    assert(nearly_equal(summary.avg_decode_ms, 5.0f));
    assert(nearly_equal(summary.avg_render_ms, 3.0f));
    assert(nearly_equal(summary.avg_frame_ms, 18.0f));
    assert(nearly_equal(summary.avg_fps, 55.0f));
    assert(nearly_equal(summary.max_frame_ms, 20.0f));
    assert(summary.bitrate_kbps == 1500);
    assert(summary.dropped_frames == 1);

    telemetry.reset();
    assert(telemetry.size() == 0);

    return 0;
}
