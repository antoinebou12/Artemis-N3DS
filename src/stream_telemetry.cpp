#include "stream_telemetry.hpp"

#include <algorithm>

void StreamTelemetry::reset() {
    samples_.fill({});
    next_index_ = 0;
    count_ = 0;
}

void StreamTelemetry::push(const StreamTelemetrySample &sample) {
    samples_[next_index_] = sample;
    next_index_ = (next_index_ + 1) % kCapacity;
    count_ = std::min(count_ + 1, kCapacity);
}

StreamTelemetrySummary StreamTelemetry::summary() const {
    StreamTelemetrySummary result{};
    result.sample_count = count_;
    if (count_ == 0) {
        return result;
    }

    const std::size_t first = count_ == kCapacity ? next_index_ : 0;
    for (std::size_t i = 0; i < count_; ++i) {
        const auto &sample = samples_[(first + i) % kCapacity];
        result.avg_decode_ms += sample.decode_ms;
        result.avg_render_ms += sample.render_ms;
        result.avg_frame_ms += sample.frame_ms;
        result.avg_fps += sample.fps;
        result.max_frame_ms = std::max(result.max_frame_ms, sample.frame_ms);
        result.bitrate_kbps = sample.bitrate_kbps;
        result.dropped_frames = sample.dropped_frames;
    }

    const float divisor = static_cast<float>(count_);
    result.avg_decode_ms /= divisor;
    result.avg_render_ms /= divisor;
    result.avg_frame_ms /= divisor;
    result.avg_fps /= divisor;
    return result;
}

std::size_t StreamTelemetry::size() const { return count_; }
