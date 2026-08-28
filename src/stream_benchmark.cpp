#include "stream_benchmark.hpp"

#include "stream_telemetry.hpp"
#include "stream_telemetry_store.hpp"
#include "system/pair_record.hpp"

#include <array>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>

namespace {
const char *kBenchmarkDirectory = MOONLIGHT_3DS_PATH "/benchmarks";
}

bool export_stream_benchmark_csv(char *output_path, std::size_t output_size) {
    if (output_path == nullptr || output_size == 0) {
        return false;
    }

    std::array<StreamTelemetrySample, StreamTelemetry::kCapacity> samples{};
    const std::size_t sample_count =
        copy_global_stream_telemetry(samples.data(), samples.size());
    if (sample_count == 0) {
        std::snprintf(output_path, output_size, "No telemetry samples");
        return false;
    }

    mkdir(MOONLIGHT_3DS_PATH, 0777);
    mkdir(kBenchmarkDirectory, 0777);

    const std::time_t now = std::time(nullptr);
    std::snprintf(output_path, output_size, "%s/benchmark_%ld.csv",
                  kBenchmarkDirectory, static_cast<long>(now));

    FILE *fd = std::fopen(output_path, "w");
    if (fd == nullptr) {
        return false;
    }

    std::fprintf(fd,
                 "sample,time_ms,decode_ms,render_ms,frame_ms,fps,bitrate_kbps,"
                 "dropped_frames\n");

    float elapsed_ms = 0.0f;
    for (std::size_t i = 0; i < sample_count; ++i) {
        const auto &sample = samples[i];
        elapsed_ms += sample.frame_ms;
        std::fprintf(fd, "%zu,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u\n", i,
                     elapsed_ms, sample.decode_ms, sample.render_ms,
                     sample.frame_ms, sample.fps, sample.bitrate_kbps,
                     sample.dropped_frames);
    }

    std::fclose(fd);
    return true;
}
