#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "render/renderer.hpp"

struct TTF_Font;

namespace tto {

// UTF-8 text via SDL3_ttf, cached per (string, font). The original used
// GLScene TGLWindowsBitmapFont "MS Sans Serif" bold 16 px for the HUD and VCL
// "MS Sans Serif"/"Verdana" for menus; fontconfig picks the closest system font.
class TextRenderer {
public:
    bool init(Renderer& r);
    void shutdown();
    enum class Font { Hud, Ui, UiBold, Big, Huge, Medium };
    // Draw at pixel position; align: 0 left, 1 right (x is right edge), 2 centre.
    void draw(const std::string& utf8, float x, float y, Font f, glm::vec4 color, int align = 0);
    // Same with a 1-px dark shadow offset (the HUD draws "...Golge" shadow texts).
    void draw_shadowed(const std::string& utf8, float x, float y, Font f, glm::vec4 color, int align = 0);
    float width(const std::string& utf8, Font f);
    // Greedy word wrap to maxWidth pixels; honours explicit \n.
    std::vector<std::string> wrap(const std::string& utf8, Font f, float maxWidth);
    // Multi-line draw (lines separated by \n, line height lh or font line skip).
    void draw_lines(const std::string& utf8, float x, float y, Font f, glm::vec4 color, int align = 0, float lh = 0, bool shadow = false);
    float line_height(Font f);
    void flush_cache();

private:
    struct Entry { Texture tex; };
    TTF_Font* font(Font f);
    const Texture* get(const std::string& s, Font f);
    Renderer* r_ = nullptr;
    TTF_Font* fonts_[6] = {};
    std::unordered_map<std::string, Entry> cache_;
};

}  // namespace tto
