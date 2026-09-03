#pragma once

// Bottom-screen SELECT / GameStream helper layout. Coordinates are in 320x240
// screen space (touchPosition), not the rotated framebuffer.

struct SelectMenuLayout {
    static constexpr int screen_w = 320;
    static constexpr int screen_h = 240;
    static constexpr int header_h = 28;
    static constexpr int tab_h = 22;
    static constexpr int footer_h = 28;
    static constexpr int rows = 4;
    static constexpr int cols = 2;
    static constexpr int tabs = 3;
    static constexpr int margin = 6;
    static constexpr int gap = 4;
    static constexpr int grid_top = header_h + tab_h;
    static constexpr int grid_bottom = screen_h - footer_h;

    static constexpr int tile_width() {
        return (screen_w - margin * 2 - gap) / cols;
    }

    static constexpr int tile_height() {
        return (grid_bottom - grid_top - gap * (rows - 1)) / rows;
    }

    static constexpr int tile_x(int col) {
        return margin + col * (tile_width() + gap);
    }

    static constexpr int tile_y(int row) {
        return grid_top + row * (tile_height() + gap);
    }

    static constexpr int tab_width() {
        return (screen_w - margin * 2 - gap * (tabs - 1)) / tabs;
    }

    static constexpr int tab_x(int tab) {
        return margin + tab * (tab_width() + gap);
    }

    enum class Hit {
        None,
        Tab0,
        Tab1,
        Tab2,
        Tile,
        FooterBack,
        FooterPage,
        FooterOpen,
    };

    static Hit hit(int px, int py, int &row, int &col) {
        row = -1;
        col = -1;
        if (px < 0 || px >= screen_w || py < 0 || py >= screen_h) {
            return Hit::None;
        }
        if (py < header_h) {
            return Hit::None;
        }
        if (py < grid_top) {
            const int local = px - margin;
            if (local < 0) {
                return Hit::None;
            }
            const int stride = tab_width() + gap;
            const int tab = local / stride;
            if (tab < 0 || tab >= tabs) {
                return Hit::None;
            }
            if (local - tab * stride >= tab_width()) {
                return Hit::None;
            }
            if (tab == 0) {
                return Hit::Tab0;
            }
            if (tab == 1) {
                return Hit::Tab1;
            }
            return Hit::Tab2;
        }
        if (py >= grid_bottom) {
            if (px < screen_w / 3) {
                return Hit::FooterBack;
            }
            if (px < (screen_w * 2) / 3) {
                return Hit::FooterPage;
            }
            return Hit::FooterOpen;
        }

        const int local_x = px - margin;
        const int local_y = py - grid_top;
        if (local_x < 0 || local_y < 0) {
            return Hit::None;
        }

        const int stride_x = tile_width() + gap;
        const int stride_y = tile_height() + gap;
        col = local_x / stride_x;
        row = local_y / stride_y;
        if (col < 0 || col >= cols || row < 0 || row >= rows) {
            return Hit::None;
        }
        if (local_x - col * stride_x >= tile_width() ||
            local_y - row * stride_y >= tile_height()) {
            row = -1;
            col = -1;
            return Hit::None;
        }
        return Hit::Tile;
    }
};
