# Artemis 3DS roadmap

This fork is evolving from a straight Moonlight Embedded port into a 3DS-focused streaming client inspired by the architecture and UX work in Artemis-Switch.

The goal is not to copy Switch-specific NVDEC/deko3d features. The goal is to reuse the ideas that fit New Nintendo 3DS hardware: profiles, unified presentation state, diagnostics, low-latency tuning, better bottom-screen controls, and native stereoscopic output.

## Principles

- Keep H.264 + MVD as the primary New 3DS video path.
- Preserve the software decoder fallback for original 3DS hardware.
- Measure before changing decode/presentation scheduling.
- Prefer fixed-size storage and low-allocation code in the streaming hot path.
- Keep host compatibility on standard Moonlight/GameStream first.
- Treat Apollo/Vibepollo integration and VPN support as optional layers after the core client is stable.

## Phase 0 - Foundation and CI/CD

Status: **in progress**

- [x] PR CI for host-side foundation tests.
- [x] Cross-build CIA and 3DSX artifacts in CI.
- [x] Tag-based GitHub Release workflow with SHA-256 checksums.
- [x] Add fixed-size telemetry model.
- [x] Add presentation state model.
- [x] Add built-in stream profile presets.
- [x] Support `profile = ...` in `moonlight.conf`.
- [ ] Add hardware smoke-test checklist for real New 3DS devices.

## Phase 1 - Stream profiles

Built-in presets:

| Profile | Resolution | FPS | Bitrate |
|---|---:|---:|---:|
| Low Latency | 400x240 | 60 | 1000 kbps |
| Balanced | 800x480 | 30 | 1500 kbps |
| Quality | 800x480 | 60 | 3000 kbps |
| Desktop | 800x480 | 60 | 4000 kbps |

Example `sd:/3ds/moonlight/moonlight.conf`:

```ini
profile = Balanced
width = 800
height = 480
fps = 30
bitrate = 1800
```

The profile is applied first. Explicit settings that appear after it remain overrides.

Next work:

- [ ] Add profile picker to stream settings.
- [ ] Store selected profile per host.
- [ ] Add custom named profiles on SD card.
- [ ] Add import/export without introducing a heavyweight JSON dependency.

## Phase 2 - Unified presentation

The renderer currently has separate normal, mirror, stretch, and magnify implementations. Move the geometry/crop policy into one `PresentationState` and progressively make renderers consume it.

Modes:

- Fit
- Fill
- Stretch
- Magnify
- Stereo side-by-side

Next work:

- [ ] Implement shared source UV calculation.
- [ ] Implement shared destination viewport calculation.
- [ ] Preserve current renderer behavior as fallback during migration.
- [ ] Add persistent zoom and pan.
- [ ] Make filtering choice explicit instead of implicit.

## Phase 3 - Bottom-screen Artemis UI

Replace the console-heavy stream settings experience with three lightweight tabs:

### Quick

- Keyboard
- Touchpad/mouse
- Gamepad
- Magnifier
- Mute
- Disconnect

### Display

- Fit / Fill / Stretch
- Magnify
- Stereo SBS
- Zoom / pan / reset

### Performance

- Decode time
- Render/copy time
- Frame time
- Display FPS
- Bitrate
- Dropped frames
- Memory
- Battery

Do not update expensive text every frame. Sample per frame and refresh the UI around 4 Hz.

## Phase 4 - Telemetry and benchmark

The renderer already measures decode and framebuffer-copy timing. Feed those values into `StreamTelemetry` and export benchmark sessions to:

```text
sd:/3ds/moonlight/benchmarks/
```

CSV should contain at least:

```text
time,decode_ms,render_ms,frame_ms,fps,bitrate_kbps,dropped_frames
```

Next work:

- [ ] Connect decoder timing to telemetry.
- [ ] Connect renderer timing to telemetry.
- [ ] Add frame/drop counters.
- [ ] Add CSV exporter.
- [ ] Add P50/P95 frame-time summaries once sufficient samples exist.

## Phase 5 - Low-latency mode

Only tune pacing after telemetry is available.

Modes:

- Smooth
- Balanced
- Low Latency

The 3DS implementation should be designed around MVD/PICA200 behavior, not copied from the Switch NVDEC/deko3d queue policy.

Targets:

- Avoid avoidable presentation backlog.
- Prefer current frames when the client falls behind.
- Minimize work performed by the bottom-screen UI during streaming.
- Keep audio/input stability ahead of marginal FPS improvements.

## Phase 6 - Native stereoscopic streaming

The existing renderer already supports left/right top-screen framebuffers when 3D is active. Add an explicit SBS stream mode:

```text
host frame: [ LEFT EYE | RIGHT EYE ]
                   |
                   +--> GFX_LEFT / GFX_RIGHT
```

Initial scope:

- Side-by-side only.
- No client-side depth reconstruction.
- Physical 3D slider remains the display control.
- Validate with known SBS video and emulator/game output on a real New 3DS.

## Phase 7 - Input improvements

- Circle Pad deadzone.
- C-Stick deadzone and sensitivity.
- Touch absolute mouse mode.
- Touch relative trackpad mode.
- Mouse sensitivity and acceleration toggle.
- Configurable shoulder/trigger mouse mappings.
- Motion sensitivity and axis inversion.
- Fast shortcuts for keyboard, mouse, magnifier, and performance pages.

## Phase 8 - Host experience

Improve the current IP-centric host list with:

- Friendly nickname.
- Online/offline state.
- Assigned stream profile.
- Server product/version.
- GPU information.
- Codec capability flags.

The client already retrieves much of this metadata during server initialization; surface it in the UI instead of leaving it only in debug output.

## Phase 9 - Apollo/Vibepollo awareness

Keep host integration conservative:

- Detect host identity/capabilities.
- Show the detected host type.
- Prefer 3DS-friendly virtual display modes when the host explicitly supports them.
- Do not make Apollo/Vibepollo APIs a dependency for normal Sunshine streaming.

Potential host modes:

- 400x240
- 800x480
- 800x240 SBS

## Phase 10 - Remote access

Remote access is deliberately last.

Start with standard networking improvements:

- LAN IP.
- DNS hostname.
- Manual remote address.
- Custom GameStream port.

WireGuard/NetBird should only be integrated after a separate 3DS networking layer can independently demonstrate stable TCP, UDP, DNS, routing, keepalives, and sustained traffic. The Artemis-Switch NetBird implementation cannot be copied directly because Horizon OS and libctru expose different networking environments.

## Out of scope for the 3DS target

Do not spend core development time on features that do not fit the hardware:

- AV1
- HEVC hardware decode
- HDR
- 90/120 FPS
- Switch FSR/NIS/SGSR post-processing stack
- Heavy frame-generation techniques

The primary quality target is a stable, low-latency H.264 stream at 400x240 or 800x480 with excellent controls and 3DS-native presentation features.
