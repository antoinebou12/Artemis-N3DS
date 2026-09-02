#pragma once

// Central, single-detection snapshot of what this 3DS model can do. Detected
// once on first use. Feature decisions (resolutions, decoder, wide/stereo)
// should read from here instead of calling APT_CheckNew3DS() ad hoc all over
// the codebase.
//
// Note: regular 3DS/3DS XL and New 3DS/New 3DS XL share the same top-screen
// pixel dimensions (400x240). The XL suffix only means a physically larger
// screen, not more pixels.

struct HardwareCapabilities {
    bool new_3ds = false;
    bool hardware_decoder = false;          // MVD H.264 decoder (New 3DS family).
    bool wide_screen_supported = false;     // 800x240 wide top screen (New 3DS).
    bool stereoscopic_supported = true;     // 3D slider present (all 3DS units).
    int cpu_count = 2;
};

// Returns the cached, single-run capabilities.
const HardwareCapabilities &moonlight_hardware_caps();
