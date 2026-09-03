#include "TouchHandler.hpp"

#include "../../presentation_state.hpp"
#include "../../stream_benchmark.hpp"
#include "../../stream_telemetry_store.hpp"
#include "../../system/dispatcher.hpp"
#include "stream_bottom_ui.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
unsigned clamp_u(unsigned value, unsigned lo, unsigned hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

// Avoid %f / %zu on the 3DS newlib path — both have crashed homebrew here.
void format_ms(char *out, std::size_t out_size, float ms) {
    if (out == nullptr || out_size == 0) {
        return;
    }
    if (!std::isfinite(ms) || ms < 0.0f) {
        ms = 0.0f;
    }
    if (ms > 9999.0f) {
        ms = 9999.0f;
    }
    const unsigned whole = static_cast<unsigned>(ms);
    const unsigned frac =
        static_cast<unsigned>((ms - static_cast<float>(whole)) * 100.0f + 0.5f) %
        100u;
    std::snprintf(out, out_size, "%u.%02u ms", whole, frac);
}

void format_fps(char *out, std::size_t out_size, float fps) {
    if (out == nullptr || out_size == 0) {
        return;
    }
    if (!std::isfinite(fps) || fps < 0.0f) {
        fps = 0.0f;
    }
    if (fps > 999.0f) {
        fps = 999.0f;
    }
    const unsigned whole = static_cast<unsigned>(fps);
    const unsigned frac =
        static_cast<unsigned>((fps - static_cast<float>(whole)) * 10.0f + 0.5f) %
        10u;
    std::snprintf(out, out_size, "%u.%u", whole, frac);
}
} // namespace

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

    const char *mode = presentation_mode_name(presentation.mode);
    draw_header(canvas, "PERF", mode != nullptr ? mode : "Artemis");

    char line[32];
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

    format_ms(line, sizeof(line), summary.avg_decode_ms);
    metric("DECODE AVG", line);
    format_ms(line, sizeof(line), summary.avg_render_ms);
    metric("RENDER AVG", line);
    format_ms(line, sizeof(line), summary.avg_frame_ms);
    metric("FRAME AVG", line);
    format_fps(line, sizeof(line), summary.avg_fps);
    metric("FPS", line);

    format_ms(line, sizeof(line), summary.max_frame_ms);
    metric_right("FRAME MAX", line, 0);
    std::snprintf(line, sizeof(line), "%u kbps",
                  static_cast<unsigned>(summary.bitrate_kbps));
    metric_right("BITRATE", line, 1);
    std::snprintf(line, sizeof(line), "%u",
                  static_cast<unsigned>(summary.dropped_frames));
    metric_right("DROPPED", line, 2);
    std::snprintf(line, sizeof(line), "%u / 120",
                  static_cast<unsigned>(clamp_u(
                      static_cast<unsigned>(summary.sample_count), 0u, 120u)));
    metric_right("SAMPLES", line, 3);

    canvas.round_fill(6, 168, 150, 28, kColAccent);
    canvas.text_centered("SAVE CSV", 6, 176, 150, kColDark, 1);
    canvas.round_fill(164, 168, 150, 28, kColRaised);
    canvas.text_centered("MENU", 164, 176, 150, kColText, 1);

    if (status_text[0] != '\0') {
        // Keep status on one short line — long SD paths used to overrun paint.
        char short_status[40];
        std::snprintf(short_status, sizeof(short_status), "%.39s", status_text);
        canvas.text(short_status, 8, 202, kColMuted, 1);
    } else {
        canvas.text("A SAVE   B MENU", 8, 202, kColMuted, 1);
    }

    canvas.present();
}

void PerformanceTouchHandler::save_csv() {
    char path[96] = {0};
    const bool saved = export_stream_benchmark_csv(path, sizeof(path));
    if (saved) {
        // Prefer filename only for the on-screen status.
        const char *name = std::strrchr(path, '/');
        name = name != nullptr ? name + 1 : path;
        std::snprintf(status_text, sizeof(status_text), "Saved %s", name);
    } else if (path[0] != '\0') {
        std::snprintf(status_text, sizeof(status_text), "%.90s", path);
    } else {
        std::snprintf(status_text, sizeof(status_text), "Save failed");
    }
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
        return;
    }
    // Keep metrics alive without requiring continuous touch.
    redraw(false);
}

void PerformanceTouchHandler::_handle_touch_down(touchPosition touch) {
    (void)touch;
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
    redraw(false);
}
