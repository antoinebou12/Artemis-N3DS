#pragma once

// PICA200 textures use power-of-two backing surfaces. Select the smallest
// surface that can hold the 3DS streaming modes while keeping decoder and
// renderer row strides identical.
constexpr int MOON_CTR_VIDEO_TEX_W = 1024;
constexpr int MOON_CTR_VIDEO_TEX_H = 512;

// All 3DS-family models use the same 400x240 top-screen pixel grid (800x240
// when 3D is enabled). The stream may be larger for downscaling, but it must
// fit the PICA200 backing surface used by both decoder implementations.
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
