#include "stream_telemetry_store.hpp"

#include "system/ThreadLock.hpp"

namespace {
ThreadLock g_telemetry_lock;
}

void reset_global_stream_telemetry() {
    g_telemetry_lock.lock();
    global_stream_telemetry().reset();
    g_telemetry_lock.unlock();
}

void push_global_stream_telemetry(const StreamTelemetrySample &sample) {
    g_telemetry_lock.lock();
    global_stream_telemetry().push(sample);
    g_telemetry_lock.unlock();
}

StreamTelemetrySummary global_stream_telemetry_summary() {
    g_telemetry_lock.lock();
    const auto summary = global_stream_telemetry().summary();
    g_telemetry_lock.unlock();
    return summary;
}

std::size_t copy_global_stream_telemetry(StreamTelemetrySample *destination,
                                         std::size_t capacity) {
    g_telemetry_lock.lock();
    const auto copied =
        global_stream_telemetry().copy_samples(destination, capacity);
    g_telemetry_lock.unlock();
    return copied;
}
