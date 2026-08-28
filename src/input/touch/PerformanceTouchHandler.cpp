#include "TouchHandler.hpp"

#include "../../presentation_state.hpp"
#include "../../stream_benchmark.hpp"
#include "../../stream_telemetry_store.hpp"
#include "../../system/dispatcher.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

PerformanceTouchHandler::PerformanceTouchHandler() { redraw(true); }

PerformanceTouchHandler::~PerformanceTouchHandler() {
    consoleSelect(&DebugTouchHandler::topScreen);
}

void PerformanceTouchHandler::redraw(bool force) {
    const u64 now = svcGetSystemTick();
    if (!force && last_redraw_ticks != 0 &&
        now - last_redraw_ticks < (SYSCLOCK_ARM11 / 4)) {
        return;
    }
    last_redraw_ticks = now;

    const auto summary = global_stream_telemetry_summary();
    const auto &presentation = global_presentation_state();

    // The top screen remains the uninterrupted live stream. Performance is a
    // local bottom-screen dashboard only.
    consoleSelect(&DebugTouchHandler::bottomScreen);
    consoleClear();
    std::printf("ARTEMIS 3DS  |  PERFORMANCE\n");
    std::printf("TOP SCREEN: LIVE STREAM\n");
    std::printf("----------------------------------------\n");
    std::printf("Samples       %3zu / 120\n", summary.sample_count);
    std::printf("Decode avg    %7.2f ms\n", summary.avg_decode_ms);
    std::printf("Render avg    %7.2f ms\n", summary.avg_render_ms);
    std::printf("Frame avg     %7.2f ms\n", summary.avg_frame_ms);
    std::printf("Frame max     %7.2f ms\n", summary.max_frame_ms);
    std::printf("Observed FPS  %7.1f\n", summary.avg_fps);
    std::printf("Bitrate       %7u kbps\n", summary.bitrate_kbps);
    std::printf("Dropped       %7u\n", summary.dropped_frames);
    std::printf("Display       %s\n",
                presentation_mode_name(presentation.mode));
    std::printf("----------------------------------------\n");
    if (status_text[0] != '\0') {
        std::printf("%s\n", status_text);
    } else {
        std::printf("A / left touch: save benchmark CSV\n");
    }
    std::printf("B / right touch: back to Quick Actions\n");
    std::printf("Menu controls stay local to the 3DS.\n");
}

void PerformanceTouchHandler::save_csv() {
    char path[96] = {0};
    const bool saved = export_stream_benchmark_csv(path, sizeof(path));
    std::snprintf(status_text, sizeof(status_text), "%s: %.76s",
                  saved ? "Saved" : "Save failed", path);
    redraw(true);
}

void PerformanceTouchHandler::go_back() {
    MessageDispatcher::get_instance()->post(
        std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH));
}

void PerformanceTouchHandler::handle_navigation(
    u32 keys_down, const circlePosition &cpad, const circlePosition &cstick) {
    (void)cpad;
    (void)cstick;
    if (keys_down & KEY_B) {
        go_back();
        return;
    }
    if (keys_down & KEY_A) {
        save_csv();
    }
}

void PerformanceTouchHandler::_handle_touch_down(touchPosition touch) {
    (void)touch;
    redraw();
}

void PerformanceTouchHandler::_handle_touch_up(touchPosition touch) {
    if (touch.py < 170) {
        redraw(true);
        return;
    }

    if (touch.px < 160) {
        save_csv();
    } else {
        go_back();
    }
}

void PerformanceTouchHandler::_handle_touch_hold(touchPosition touch) {
    (void)touch;
    redraw();
}
