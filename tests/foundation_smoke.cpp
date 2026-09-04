#include "graphics_lifecycle_state.hpp"
#include "presentation_state.hpp"
#include "stream_profile.hpp"
#include "stream_telemetry.hpp"
#include "video/video_layout.hpp"
#include "connection_status.hpp"
#include "host_discovery_scan.hpp"
#include "input/touch/select_menu_layout.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

static bool nearly_equal(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) <= epsilon;
}

int main() {
    GraphicsLifecycleState graphics;
    assert(graphics.mode() == GraphicsMode::Dormant);
    assert(graphics.acquire_shell());
    assert(graphics.shell_active());
    assert(!graphics.acquire_shell());
    assert(graphics.acquire_stream());
    assert(graphics.mode() == GraphicsMode::Stream);
    assert(!graphics.acquire_stream());
    assert(graphics.acquire_shell());
    assert(graphics.shutdown());
    assert(graphics.mode() == GraphicsMode::Dormant);
    assert(!graphics.shutdown());

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

    const auto *balanced = find_stream_profile("Balanced");
    assert(balanced != nullptr);
    assert(std::strcmp(stream_profile_hint(*balanced), "Reliable 3D quality") ==
           0);
    assert(stream_profile_matches(*balanced, 800, 480, 30, 1500,
                                  PresentationMode::Stretch));
    assert(!stream_profile_matches(*balanced, 800, 480, 60, 1500,
                                   PresentationMode::Stretch));
    assert(find_stream_profile("Missing") == nullptr);

    PresentationState fit;
    fit.mode = PresentationMode::Fit;
    const auto fit_geometry =
        compute_presentation_geometry(1280, 720, 400, 240, fit);
    assert(nearly_equal(fit_geometry.destination_scale_x, 1.0f));
    assert(nearly_equal(fit_geometry.destination_scale_y, 0.9375f));
    assert(nearly_equal(fit_geometry.source_u_min, 0.0f));
    assert(nearly_equal(fit_geometry.source_u_max, 1.0f));

    PresentationState fill;
    fill.mode = PresentationMode::Fill;
    const auto fill_geometry =
        compute_presentation_geometry(1280, 720, 400, 240, fill);
    assert(nearly_equal(fill_geometry.destination_scale_x, 1.0f));
    assert(nearly_equal(fill_geometry.destination_scale_y, 1.0f));
    assert(nearly_equal(fill_geometry.source_u_min, 0.03125f));
    assert(nearly_equal(fill_geometry.source_u_max, 0.96875f));

    PresentationState magnify;
    magnify.mode = PresentationMode::Magnify;
    magnify.zoom = 2.0f;
    const auto magnify_geometry =
        compute_presentation_geometry(800, 480, 400, 240, magnify);
    assert(nearly_equal(magnify_geometry.source_u_min, 0.25f));
    assert(nearly_equal(magnify_geometry.source_u_max, 0.75f));
    assert(nearly_equal(magnify_geometry.source_v_min, 0.25f));
    assert(nearly_equal(magnify_geometry.source_v_max, 0.75f));

    // Adaptive video backing surfaces keep PICA's power-of-two requirement but
    // stop moving a 1024x512 texture for every small 3DS stream.
    assert(moon_video_texture_width(400) == 512);
    assert(moon_video_resolution_is_supported(400, 240));
    assert(moon_video_resolution_is_supported(800, 480));
    assert(moon_video_resolution_is_supported(1024, 512));
    assert(!moon_video_resolution_is_supported(0, 240));
    assert(!moon_video_resolution_is_supported(1025, 512));
    assert(!moon_video_resolution_is_supported(1024, 513));
    assert(moon_video_texture_height(240) == 256);
    assert(moon_video_texture_width(800) == 1024);
    assert(moon_video_texture_height(240) == 256);
    assert(moon_video_texture_height(480) == 512);
    assert(moon_video_texture_bytes(400, 240, 2) == 512 * 256 * 2);
    assert(moon_video_texture_bytes(800, 240, 2) == 1024 * 256 * 2);
    assert(moon_video_texture_bytes(800, 480, 2) == 1024 * 512 * 2);

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

    StreamTelemetrySample copied[2]{};
    assert(telemetry.copy_samples(copied, 2) == 2);
    assert(nearly_equal(copied[0].decode_ms, 4.0f));
    assert(nearly_equal(copied[1].decode_ms, 6.0f));

    telemetry.reset();
    assert(telemetry.size() == 0);

    assert(connection_termination_user_message(ML_ERROR_GRACEFUL_TERMINATION)
               .empty());
    assert(connection_termination_user_message(ML_ERROR_NO_VIDEO_TRAFFIC)
               .find("No video") != std::string::npos);
    assert(connection_termination_user_message(ML_ERROR_NO_VIDEO_FRAME)
               .find("bitrate") != std::string::npos);
    assert(connection_termination_user_message(ML_ERROR_PROTECTED_CONTENT)
               .find("DRM") != std::string::npos);

    // Scan stays on the 3DS LAN /24 plus the preferred Moonlight subnet.
    std::vector<std::uint32_t> local_nets;
    moonlight_collect_scan_networks(0xC0A84464u, local_nets, true);
    assert(local_nets.size() == 1);
    assert(local_nets.front() == 0xC0A84400u);

    std::vector<std::uint32_t> other_lan;
    moonlight_collect_scan_networks(0xC0A8010Au, other_lan, true);
    assert(other_lan.size() == 2);
    assert(other_lan.front() == 0xC0A80100u);
    assert(std::find(other_lan.begin(), other_lan.end(), 0xC0A84400u) !=
           other_lan.end());
    assert(std::find(other_lan.begin(), other_lan.end(), 0x0A000000u) ==
           other_lan.end());
    assert(std::find(other_lan.begin(), other_lan.end(), 0xC0A8FE00u) ==
           other_lan.end());

    std::vector<std::uint32_t> ten_net;
    moonlight_collect_scan_networks(0x0A00000Au, ten_net, true);
    assert(ten_net.size() == 2);
    assert(ten_net.front() == 0x0A000000u);
    assert(std::find(ten_net.begin(), ten_net.end(), 0xC0A84400u) !=
           ten_net.end());
    assert(kMoonlightPreferredHostIp == 0xC0A84437u);

    // Remote discovery must never surface loopback/invalid IPv4 literals.
    assert(moonlight_is_usable_remote_ipv4(0xC0A84437u));
    assert(moonlight_is_usable_remote_ipv4(0x0A00002Au));
    assert(!moonlight_is_usable_remote_ipv4(0x7F000001u));
    assert(!moonlight_is_usable_remote_ipv4(0x7FFFFFFFu));
    assert(!moonlight_is_usable_remote_ipv4(0x00000000u));
    assert(!moonlight_is_usable_remote_ipv4(0xE0000001u));
    assert(!moonlight_is_usable_remote_ipv4(0xFFFFFFFFu));

    int hit_row = -1;
    int hit_col = -1;
    assert(SelectMenuLayout::hit(40, 8, hit_row, hit_col) ==
           SelectMenuLayout::Hit::None);
    assert(SelectMenuLayout::hit(20, 36, hit_row, hit_col) ==
           SelectMenuLayout::Hit::Tab0);
    assert(SelectMenuLayout::hit(120, 36, hit_row, hit_col) ==
           SelectMenuLayout::Hit::Tab1);
    assert(SelectMenuLayout::hit(240, 36, hit_row, hit_col) ==
           SelectMenuLayout::Hit::Tab2);
    assert(SelectMenuLayout::hit(20, SelectMenuLayout::tile_y(1) + 4, hit_row,
                                 hit_col) == SelectMenuLayout::Hit::Tile);
    assert(hit_row == 1);
    assert(hit_col == 0);
    assert(SelectMenuLayout::hit(20, 230, hit_row, hit_col) ==
           SelectMenuLayout::Hit::FooterBack);
    assert(SelectMenuLayout::hit(160, 230, hit_row, hit_col) ==
           SelectMenuLayout::Hit::FooterPage);
    assert(SelectMenuLayout::hit(300, 230, hit_row, hit_col) ==
           SelectMenuLayout::Hit::FooterOpen);
    assert(SelectMenuLayout::tabs == 3);

    return 0;
}
