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

void save_png(const std::filesystem::path& p, const Image& img) {
    if (!stbi_write_png(p.string().c_str(), img.w, img.h, 4, img.rgba.data(), img.w * 4))
        throw std::runtime_error("cannot write " + p.string());
}

}  // namespace tto
