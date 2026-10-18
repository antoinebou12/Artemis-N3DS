#pragma once

// PICA200 textures use power-of-two backing surfaces. Select the smallest
// surface that can hold the 3DS streaming modes while keeping decoder and
// renderer row strides identical.
constexpr int MOON_CTR_VIDEO_TEX_W = 1024;
constexpr int MOON_CTR_VIDEO_TEX_H = 512;

inline bool moon_video_resolution_is_supported(int image_width,
                                               int image_height) {
    return image_width > 0 && image_height > 0 &&
           image_width <= MOON_CTR_VIDEO_TEX_W &&
           image_height <= MOON_CTR_VIDEO_TEX_H;
}

inline int moon_video_texture_width(int image_width) {
    return image_width <= 512 ? 512 : MOON_CTR_VIDEO_TEX_W;
}

inline int moon_video_texture_height(int image_height) {
    return image_height <= 256 ? 256 : MOON_CTR_VIDEO_TEX_H;
}

inline int moon_video_texture_bytes(int image_width, int image_height,
                                    int bytes_per_pixel) {
    return moon_video_texture_width(image_width) *
           moon_video_texture_height(image_height) * bytes_per_pixel;
}

// Largest stream size the console can decode that keeps the PC's aspect ratio.
// Width stops at 800 because that is the widest top-screen surface; both sides
// are rounded down to a multiple of 8, which every encoder wants. Returns false
// for a PC resolution we cannot make sense of.
constexpr int MOON_MATCH_PC_MAX_W = 800;
constexpr int MOON_MATCH_PC_MAX_H = MOON_CTR_VIDEO_TEX_H;

inline bool moon_match_pc_resolution(int pc_width, int pc_height, int &out_width,
                                     int &out_height) {
    if (pc_width <= 0 || pc_height <= 0) {
        return false;
    }

    // Start from the widest surface, then pull the height in if the aspect is
    // taller than the console can hold.
    long width = MOON_MATCH_PC_MAX_W;
    long height = (width * pc_height) / pc_width;
    if (height > MOON_MATCH_PC_MAX_H) {
        height = MOON_MATCH_PC_MAX_H;
        width = (height * pc_width) / pc_height;
    }

    width &= ~7;
    height &= ~7;
    if (width < 8 || height < 8) {
        return false;
    }

    out_width = static_cast<int>(width);
    out_height = static_cast<int>(height);
    return moon_video_resolution_is_supported(out_width, out_height);
}
