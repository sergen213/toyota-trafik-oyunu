#include "render/image.hpp"

#include <stdexcept>

#include "core/fs.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_PNG
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace tto {

Image load_image(const std::filesystem::path& p) {
    auto bytes = read_file(p);
    if (bytes.size() > 2 && bytes[0] == 'B' && bytes[1] == 'M') return load_image_mem(bytes.data(), bytes.size());
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!px) throw std::runtime_error("image decode failed: " + p.string() + ": " + stbi_failure_reason());
    Image img;
    img.w = w;
    img.h = h;
    img.rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    return img;
}

Image load_image_mem(const std::uint8_t* data, std::size_t size) {
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    if (!px) throw std::runtime_error(std::string("image decode failed: ") + stbi_failure_reason());
    Image img;
    img.w = w;
    img.h = h;
    img.rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    return img;
}

void key_bottom_left(Image& img) {
    if (img.w <= 0 || img.h <= 0) return;
    const std::size_t bl = static_cast<std::size_t>(img.h - 1) * img.w * 4;
    const std::uint8_t r = img.rgba[bl], g = img.rgba[bl + 1], b = img.rgba[bl + 2];
    for (std::size_t i = 0; i < img.rgba.size(); i += 4)
        if (img.rgba[i] == r && img.rgba[i + 1] == g && img.rgba[i + 2] == b) img.rgba[i + 3] = 0;
}

void save_png(const std::filesystem::path& p, const Image& img) {
    if (!stbi_write_png(p.string().c_str(), img.w, img.h, 4, img.rgba.data(), img.w * 4))
        throw std::runtime_error("cannot write " + p.string());
}

}  // namespace tto
