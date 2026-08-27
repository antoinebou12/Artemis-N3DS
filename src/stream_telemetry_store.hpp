#pragma once

#include "stream_telemetry.hpp"

#include <cstddef>

void reset_global_stream_telemetry();
void push_global_stream_telemetry(const StreamTelemetrySample &sample);
StreamTelemetrySummary global_stream_telemetry_summary();
std::size_t copy_global_stream_telemetry(StreamTelemetrySample *destination,
                                         std::size_t capacity);
