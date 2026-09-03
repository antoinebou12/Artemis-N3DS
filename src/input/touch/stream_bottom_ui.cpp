#include "stream_bottom_ui.hpp"

#include "../../graphics_lifecycle.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace StreamUi {
namespace {
u8 channel(u32 rgb, int shift) {
    return static_cast<u8>((rgb >> shift) & 0xFFu);
}

// 5x7 glyphs, 5-bit rows, bit 4 = leftmost pixel. Index = ch - 32.
const uint8_t kFont5x7[][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00}, // !
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x00, 0x00}, // #
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, // $
    {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}, // %
    {0x08, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00}, // &
    {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, // (
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, // )
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, // *
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, // ,
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00}, // .
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, // /
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
    {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00}, // :
    {0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x08}, // ;
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, // <
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, // =
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, // >
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, // ?
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}, // @
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, // C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}, // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, // I
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, // Z
};
} // namespace

void BottomCanvas::put(int x, int y, u32 rgb) const {
    if (!ready() || x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) {
        return;
    }
    const int index = x * kScreenH + (kScreenH - 1 - y);
    const u8 r = channel(rgb, 16);
    const u8 g = channel(rgb, 8);
    const u8 b = channel(rgb, 0);
    if (px_size == 2) {
        reinterpret_cast<u16 *>(fb)[index] = RGB8_to_565(r, g, b);
    } else {
        fb[index * px_size + 0] = b;
        fb[index * px_size + 1] = g;
        fb[index * px_size + 2] = r;
    }
}

void BottomCanvas::fill(int x, int y, int w, int h, u32 rgb) const {
    if (!ready() || w <= 0 || h <= 0) {
        return;
    }
    const int x1 = std::min(x + w, kScreenW);
    const int y1 = std::min(y + h, kScreenH);
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    const u8 r = channel(rgb, 16);
    const u8 g = channel(rgb, 8);
    const u8 b = channel(rgb, 0);

    if (px_size == 2) {
        const u16 pixel = RGB8_to_565(r, g, b);
        auto *dst = reinterpret_cast<u16 *>(fb);
        // Framebuffer is rotated: columns of screen Y are contiguous.
        for (int px = x0; px < x1; ++px) {
            const int base = px * kScreenH + (kScreenH - 1 - (y1 - 1));
            const int count = y1 - y0;
            for (int i = 0; i < count; ++i) {
                dst[base + i] = pixel;
            }
        }
        return;
    }

    for (int px = x0; px < x1; ++px) {
        for (int py = y0; py < y1; ++py) {
            put(px, py, rgb);
        }
    }
}

void BottomCanvas::clear(u32 rgb) const { fill(0, 0, kScreenW, kScreenH, rgb); }

void BottomCanvas::round_fill(int x, int y, int w, int h, u32 rgb) const {
    if (w < 4 || h < 4) {
        fill(x, y, w, h, rgb);
        return;
    }
    fill(x + 2, y, w - 4, h, rgb);
    fill(x, y + 2, w, h - 4, rgb);
    fill(x + 1, y + 1, w - 2, h - 2, rgb);
}

void BottomCanvas::glyph(char ch, int x, int y, u32 rgb, int scale) const {
    const unsigned char raw = static_cast<unsigned char>(ch);
    char mapped = static_cast<char>(std::toupper(raw));
    if (mapped < ' ' || mapped > 'Z') {
        mapped = ' ';
    }
    const uint8_t *rows = kFont5x7[mapped - ' '];
    for (int row = 0; row < 7; ++row) {
        const uint8_t bits = rows[row];
        for (int col = 0; col < 5; ++col) {
            if ((bits & (0x10 >> col)) == 0) {
                continue;
            }
            fill(x + col * scale, y + row * scale, scale, scale, rgb);
        }
    }
}

int BottomCanvas::text_width(const char *text, int scale) const {
    return static_cast<int>(std::strlen(text)) * 6 * scale;
}

void BottomCanvas::text(const char *value, int x, int y, u32 rgb,
                        int scale) const {
    int cursor = x;
    for (const char *p = value; *p != '\0'; ++p) {
        glyph(*p, cursor, y, rgb, scale);
        cursor += 6 * scale;
    }
}

void BottomCanvas::text_centered(const char *value, int x, int y, int w,
                                 u32 rgb, int scale) const {
    const int tw = text_width(value, scale);
    text(value, x + std::max(0, (w - tw) / 2), y, rgb, scale);
}

void BottomCanvas::present() const {
    if (!ready() || !n3ds_stream_render_active()) {
        return;
    }
    // Never call gfxFlushBuffers() / gfxScreenSwapBuffers() here.
    // The top-screen GPU path uses GX_DisplayTransfer + swap; a full flush
    // from the helper UI races that transfer and leaves a permanent black
    // stream after Gamepad/Mouse/SELECT transitions.
    const u32 bytes =
        static_cast<u32>(kScreenW) * static_cast<u32>(kScreenH) *
        static_cast<u32>(px_size);
    GSPGPU_FlushDataCache(fb, bytes);
}

BottomCanvas lock_bottom_canvas() {
    BottomCanvas canvas;
    if (!n3ds_stream_render_active()) {
        return canvas;
    }

    // Do not call gfxSetScreenFormat during stream — it reallocates LCD
    // buffers and breaks top-screen video until the next full reconnect.
    canvas.fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr, nullptr);
    canvas.px_size = gspGetBytesPerPixel(gfxGetScreenFormat(GFX_BOTTOM));
    if (canvas.fb == nullptr || canvas.px_size < 2) {
        canvas.fb = nullptr;
        canvas.px_size = 2;
    }
    return canvas;
}

void draw_header(const BottomCanvas &canvas, const char *title,
                 const char *status) {
    canvas.text(title, 8, 8, kColAccent, 1);
    if (status != nullptr && status[0] != '\0') {
        const int status_w = canvas.text_width(status, 1);
        canvas.text(status, kScreenW - 8 - status_w, 8, kColMuted, 1);
    }
}

void draw_footer_three(const BottomCanvas &canvas, const char *left,
                       const char *mid, const char *right) {
    constexpr int footer_y = kScreenH - 22;
    canvas.round_fill(6, footer_y, 96, 18, kColRaised);
    canvas.text_centered(left, 6, footer_y + 5, 96, kColText, 1);
    canvas.round_fill(112, footer_y, 96, 18, kColRaised);
    canvas.text_centered(mid, 112, footer_y + 5, 96, kColText, 1);
    canvas.round_fill(218, footer_y, 96, 18, kColAccent);
    canvas.text_centered(right, 218, footer_y + 5, 96, kColDark, 1);
}

void draw_card(const BottomCanvas &canvas, int x, int y, int w, int h,
               const char *label, bool selected, bool pressed, bool live,
               bool danger) {
    u32 fill = kColSurface;
    u32 accent = kColAccent;
    u32 label_color = kColMuted;
    if (danger) {
        fill = selected ? kColDanger : kColSurface;
        accent = kColDanger;
        label_color = selected ? kColText : kColMuted;
    } else if (pressed) {
        fill = kColAccent;
        label_color = kColDark;
    } else if (selected) {
        fill = kColSelected;
        label_color = kColText;
    } else if (live) {
        fill = kColRaised;
        accent = kColSuccess;
        label_color = kColText;
    }

    canvas.round_fill(x, y, w, h, fill);
    if (selected || live) {
        canvas.fill(x, y + 3, 3, h - 6, accent);
    }
    canvas.text_centered(label, x, y + (h - 7) / 2, w, label_color, 1);
}
} // namespace StreamUi
