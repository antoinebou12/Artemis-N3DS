#include "TouchHandler.hpp"

#include "../../presentation_state.hpp"
#include "../../stream_benchmark.hpp"
#include "../../stream_telemetry_store.hpp"
#include "../../system/dispatcher.hpp"
#include "stream_bottom_ui.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

PerformanceTouchHandler::PerformanceTouchHandler() { redraw(true); }

PerformanceTouchHandler::~PerformanceTouchHandler() = default;

void PerformanceTouchHandler::redraw(bool force) {
    const u64 now = svcGetSystemTick();
    if (!force && last_redraw_ticks != 0 &&
        now - last_redraw_ticks < (SYSCLOCK_ARM11 / 4)) {
        return;
    }
    last_redraw_ticks = now;

    using namespace StreamUi;
    const BottomCanvas canvas = lock_bottom_canvas();
    if (!canvas.ready()) {
        return;
    }

    const auto summary = global_stream_telemetry_summary();
    const auto &presentation = global_presentation_state();
    canvas.clear();

    char status[32];
    std::snprintf(status, sizeof(status), "%s",
                  presentation_mode_name(presentation.mode));
    draw_header(canvas, "PERF", status);

    char line[48];
    const int card_h = 26;
    int y = 32;

    auto metric = [&](const char *label, const char *value) {
        canvas.round_fill(6, y, 150, card_h, kColSurface);
        canvas.text(label, 14, y + 4, kColMuted, 1);
        canvas.text(value, 14, y + 14, kColText, 1);
        y += card_h + 4;
    };

    auto metric_right = [&](const char *label, const char *value, int row) {
        const int ry = 32 + row * (card_h + 4);
        canvas.round_fill(164, ry, 150, card_h, kColSurface);
        canvas.text(label, 172, ry + 4, kColMuted, 1);
        canvas.text(value, 172, ry + 14, kColText, 1);
    };

    std::snprintf(line, sizeof(line), "%.2f ms", summary.avg_decode_ms);
    metric("DECODE AVG", line);
    std::snprintf(line, sizeof(line), "%.2f ms", summary.avg_render_ms);
    metric("RENDER AVG", line);
    std::snprintf(line, sizeof(line), "%.2f ms", summary.avg_frame_ms);
    metric("FRAME AVG", line);
    std::snprintf(line, sizeof(line), "%.1f", summary.avg_fps);
    metric("FPS", line);

    y = 32;
    std::snprintf(line, sizeof(line), "%.2f ms", summary.max_frame_ms);
    metric_right("FRAME MAX", line, 0);
    std::snprintf(line, sizeof(line), "%lu kbps",
                  static_cast<unsigned long>(summary.bitrate_kbps));
    metric_right("BITRATE", line, 1);
    std::snprintf(line, sizeof(line), "%lu",
                  static_cast<unsigned long>(summary.dropped_frames));
    metric_right("DROPPED", line, 2);
    std::snprintf(line, sizeof(line), "%zu / 120", summary.sample_count);
    metric_right("SAMPLES", line, 3);

    canvas.round_fill(6, 168, 150, 28,
                      kColAccent);
    canvas.text_centered("SAVE CSV", 6, 176, 150, kColDark, 1);
    canvas.round_fill(164, 168, 150, 28, kColRaised);
    canvas.text_centered("MENU", 164, 176, 150, kColText, 1);

    if (status_text[0] != '\0') {
        canvas.text(status_text, 8, 202, kColMuted, 1);
    } else {
        canvas.text("A SAVE   B MENU", 8, 202, kColMuted, 1);
    }

    canvas.present();
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
    if (touch.py < 168 || touch.py > 196) {
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
