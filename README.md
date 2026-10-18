# Artemis for Nintendo 3DS

**Artemis** is a GameStream / [Sunshine](https://github.com/LizardByte/Sunshine) client for the Nintendo 3DS family. It streams games and desktop apps from your PC to the console over Wi‑Fi.

It is a fork of [Moonlight Embedded](https://github.com/moonlight-stream/moonlight-embedded), built for New 3DS hardware first, with a modern in-stream helper UI on the bottom screen while video stays on the top screen.

| Package | Use |
|---------|-----|
| [`dist/artemis.cia`](dist/artemis.cia) | HOME Menu install (recommended) |
| [`dist/artemis.3dsx`](dist/artemis.3dsx) | Homebrew Launcher |

Custom firmware is required. See [3ds.hacks.guide](https://3ds.hacks.guide/).

## Requirements

- New 3DS / New 3DS XL / New 2DS XL recommended (hardware H.264 decode)
- Original 3DS / 2DS works with software decode only — expect lower FPS
- PC running [Sunshine](https://github.com/LizardByte/Sunshine) (or compatible GameStream host) on the same network
- CFW + [FBI](https://github.com/Steveice10/FBI) for CIA install

## Install

Full FBI steps: [`dist/INSTALL.md`](dist/INSTALL.md).

**CIA (HOME Menu):** copy `dist/artemis.cia` to the SD card → open FBI → **Install and delete CIA**.

**3DSX:** copy `dist/artemis.3dsx` to `sd:/3ds/artemis/artemis.3dsx` and launch from the Homebrew Launcher.

## First-time use

### 1. Pair with Sunshine

1. Open **Artemis** from the HOME Menu.
2. Add a host (`A`) and enter your PC’s LAN IP.
   - Windows: `ipconfig` → IPv4 Address  
   - macOS: System Settings → Network  
3. Choose **pair**, then enter the PIN shown on the 3DS into Sunshine’s pin page.
4. After pairing, select the host from the list and connect.

### 2. Stream

1. On the host menu, choose **stream**, then pick an app or desktop.
2. Video plays on the **top screen**. The **bottom screen** is the SELECT helper hub:
   - **Input** — Gamepad, Mouse, Keyboard, Magnify, Mirror
   - **Display** — Fit / Fill / Stretch / SBS, zoom
   - **Session** — Performance overlay, save CSV, quit
3. Press **SELECT** (or HOME while streaming) any time to return to that hub.
4. Closing Artemis ends the stream. Leaving Artemis via HOME keeps the stream running until you quit the app.

### Magnify tips

- Drag on the pad to pan; use Zoom+ / Zoom− / Reset on the bottom UI.
- Hold **L** or **R** while touching to send mouse clicks in the magnified region.

## Configuration

Settings can be changed in-app or in:

```
sd:/3ds/moonlight/moonlight.conf
```

That path is legacy (kept so existing pairing and configs keep working). See [`dist/SD-DATA.md`](dist/SD-DATA.md) for the full SD layout (keys, paired hosts, diagnostics).

Default settings are aimed at New 3DS.

### Resolution and readable text

Open **Settings → Video → Resolution** for native **400×240 (3DS / 3DS XL)**,
480×288, 640×384, 800×480, 320×240, 640×480, 640×360, 768×432,
896×504, 1024×512, or 800×240 stereo SBS input. Match PC and custom sizes
remain available. Ordinary 2D presets use Fit to preserve proportions.

XL screens are physically larger, with the same 400×240 pixels per eye as
the smaller 3DS. Higher input resolutions can improve source detail, but
cannot increase the panel's physical resolution. The current video path
accepts up to **1024×512**, so 720p and 1080p inputs are not supported.
Start with 400×240 at 30 FPS on original 3DS/XL; larger inputs are intended
primarily for the New 3DS family and may need a lower frame rate.

In **Settings → Video → Text clarity**, choose **Crisp text** for an unfiltered
full view, or **Larger text** for an unfiltered 2× view with simulated depth
disabled. With an 800×480 stream, 2× shows a native-sized 400×240 crop per
eye. Use the in-stream **Magnify** pad to pan around the desktop. Smooth video
re-enables linear filtering and preserves your current view. Display changes
are saved in the existing configuration file. Resolution changes apply to
the next stream; the host must support the requested input size.

## Build

### Docker (easiest)

```bash
docker build --network=host -t moonlight-n3ds .
docker run --rm -it -v .:/moonlight-N3DS -w /moonlight-N3DS moonlight-n3ds:latest
make
```

Outputs: `artemis.3dsx`, `artemis.cia`, `artemis.elf` in the repo root. Copy the `.3dsx` / `.cia` into `dist/` for release packages.

VS Code: use the **Build Docker** and **Run Docker** tasks if you prefer.

### Local (devkitPro)

Needs [devkitPro](https://devkitpro.org/) with `3ds-dev`, plus `bannertool` and `makerom` on `PATH` (or pass them to `make`).

```bash
make
```

## Upstream / credits

- Protocol and shared client code: [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)
- Original embedded client: [moonlight-embedded](https://github.com/moonlight-stream/moonlight-embedded)
- Host software: [Sunshine](https://github.com/LizardByte/Sunshine)

## Contribute

1. Fork the repo  
2. Make your changes  
3. Open a pull request  
