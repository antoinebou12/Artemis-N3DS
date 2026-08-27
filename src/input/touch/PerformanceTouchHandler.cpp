#include "TouchHandler.hpp"

#include "../../presentation_state.hpp"
#include "../../stream_benchmark.hpp"
#include "../../stream_telemetry_store.hpp"
#include "../../system/dispatcher.hpp"

#include <cstdio>
#include <cstring>

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

    consoleSelect(&DebugTouchHandler::bottomScreen);
    consoleClear();
    std::printf("Moonlight N3DS - Performance\n");
    std::printf("-----------------------------\n");
    std::printf("Samples      %zu / %zu\n", summary.sample_count,
                static_cast<std::size_t>(120));
    std::printf("Decode avg   %6.2f ms\n", summary.avg_decode_ms);
    std::printf("Render avg   %6.2f ms\n", summary.avg_render_ms);
    std::printf("Frame avg    %6.2f ms\n", summary.avg_frame_ms);
    std::printf("Frame max    %6.2f ms\n", summary.max_frame_ms);
    std::printf("Observed FPS %6.1f\n", summary.avg_fps);
    std::printf("Bitrate      %u kbps\n", summary.bitrate_kbps);
    std::printf("Dropped      %u\n", summary.dropped_frames);
    std::printf("Mode         %s\n",
                presentation_mode_name(presentation.mode));
    std::printf("\n");
    if (status_text[0] != '\0') {
        std::printf("%s\n", status_text);
    }
    std::printf("\n[Save CSV]             [Back]\n");
}

void PerformanceTouchHandler::_handle_touch_down(touchPosition touch) {
    redraw();
}

void PerformanceTouchHandler::_handle_touch_up(touchPosition touch) {
    if (touch.py < 180) {
        redraw(true);
        return;
    }

    if (touch.px < 160) {
        char path[96] = {0};
        const bool saved = export_stream_benchmark_csv(path, sizeof(path));
        std::snprintf(status_text, sizeof(status_text), "%s: %s",
                      saved ? "Saved" : "Save failed", path);
        redraw(true);
        return;
    }

    auto message =
        std::make_shared<TouchStateChangedMsg>(N3dsTouchType::MENU_TOUCH);
    MessageDispatcher::get_instance()->post(message);
}

void PerformanceTouchHandler::_handle_touch_hold(touchPosition touch) {
    (void)touch;
    redraw();
}
