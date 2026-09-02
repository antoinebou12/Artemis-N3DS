#include "hardware_capabilities.hpp"

#include <3ds.h>

namespace {

HardwareCapabilities g_caps{};
bool g_detected = false;

void detect_once() {
    if (g_detected) {
        return;
    }

    bool is_new_3ds = false;
    APT_CheckNew3DS(&is_new_3ds);

    g_caps.new_3ds = is_new_3ds;
    g_caps.hardware_decoder = is_new_3ds;
    g_caps.wide_screen_supported = is_new_3ds;
    g_caps.cpu_count = is_new_3ds ? 4 : 2;
    g_detected = true;
}

} // namespace

const HardwareCapabilities &moonlight_hardware_caps() {
    detect_once();
    return g_caps;
}
