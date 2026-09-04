#pragma once

#include <3ds.h>
#include <cstdint>

// Shared bottom-screen UI for in-stream helpers (SELECT hub, perf, magnify,
// gamepad, mouse, keyboard). Citro2D stays off during stream. Bottom LCD is
// kept BGR8 (shell-native); RGB565 write path is only a fallback.
//
// Keep this struct to {fb, px_size} only. A third field was overlapped on the
// stack by nearby locals (PresentationState) under -O2, corrupting stride and
// crashing in fill() (Luma Translation-Section).

namespace StreamUi {
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
// Portrait FB contiguous axis (libctru GSP_SCREEN_WIDTH).
constexpr int kFbStride = GSP_SCREEN_WIDTH;

constexpr u32 kColBg = 0x0D1117;
constexpr u32 kColSurface = 0x1D232C;
constexpr u32 kColSelected = 0x2B3644;
constexpr u32 kColRaised = 0x232A34;
constexpr u32 kColAccent = 0x48ABFF;
constexpr u32 kColSuccess = 0x4FC97E;
constexpr u32 kColDanger = 0xE05050;
constexpr u32 kColText = 0xFAFCFF;
constexpr u32 kColMuted = 0xB8C3D0;
constexpr u32 kColDark = 0x0A1622;

struct BottomCanvas {
    u8 *fb = nullptr;
    int px_size = 3;

    bool ready() const {
        return fb != nullptr && (px_size == 2 || px_size == 3);
    }

    void put(int x, int y, u32 rgb) const;
    void fill(int x, int y, int w, int h, u32 rgb) const;
    void clear(u32 rgb = kColBg) const;
    void round_fill(int x, int y, int w, int h, u32 rgb) const;
    void glyph(char ch, int x, int y, u32 rgb, int scale = 1) const;
    int text_width(const char *text, int scale = 1) const;
    void text(const char *value, int x, int y, u32 rgb, int scale = 1) const;
    void text_centered(const char *value, int x, int y, int w, u32 rgb,
                       int scale = 1) const;
    void present() const;
};

BottomCanvas lock_bottom_canvas();

void draw_header(const BottomCanvas &canvas, const char *title,
                 const char *status);
void draw_footer_three(const BottomCanvas &canvas, const char *left,
                       const char *mid, const char *right);
void draw_card(const BottomCanvas &canvas, int x, int y, int w, int h,
               const char *label, bool selected, bool pressed, bool live,
               bool danger = false);
} // namespace StreamUi
