#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct StreamTelemetrySample {
    float decode_ms = 0.0f;
    float render_ms = 0.0f;
    float frame_ms = 0.0f;
    float fps = 0.0f;
    std::uint32_t bitrate_kbps = 0;
    std::uint32_t dropped_frames = 0;
};

struct StreamTelemetrySummary {
    float avg_decode_ms = 0.0f;
    float avg_render_ms = 0.0f;
    float avg_frame_ms = 0.0f;
    float avg_fps = 0.0f;
    float max_frame_ms = 0.0f;
    std::uint32_t bitrate_kbps = 0;
    std::uint32_t dropped_frames = 0;
    std::size_t sample_count = 0;
};

class StreamTelemetry {
  public:
    static constexpr std::size_t kCapacity = 120;

    void reset();
    void push(const StreamTelemetrySample &sample);
    StreamTelemetrySummary summary() const;
    std::size_t size() const;

  private:
    std::array<StreamTelemetrySample, kCapacity> samples_{};
    std::size_t next_index_ = 0;
    std::size_t count_ = 0;
};
