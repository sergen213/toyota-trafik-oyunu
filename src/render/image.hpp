#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace tto {

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;  // top-down rows
};

// JPEG/BMP/PNG via stb_image. Throws std::runtime_error on failure.
Image load_image(const std::filesystem::path& p);
Image load_image_mem(const std::uint8_t* data, std::size_t size);
// Delphi glyph convention: pixels equal to the bottom-left pixel become transparent.
void key_bottom_left(Image& img);
// Write RGBA (top-down) to PNG.
void save_png(const std::filesystem::path& p, const Image& img);

}  // namespace tto
