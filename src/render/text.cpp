#include "render/text.hpp"

#include <cstring>
#include <SDL3_ttf/SDL_ttf.h>
#include <fontconfig/fontconfig.h>

#include "core/log.hpp"

namespace tto {
namespace {

std::string match_font(const char* pattern) {
    std::string out;
    FcConfig* cfg = FcInitLoadConfigAndFonts();
    FcPattern* pat = FcNameParse(reinterpret_cast<const FcChar8*>(pattern));
    FcConfigSubstitute(cfg, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult res;
    if (FcPattern* m = FcFontMatch(cfg, pat, &res)) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch) out = reinterpret_cast<char*>(file);
        FcPatternDestroy(m);
    }
    FcPatternDestroy(pat);
    FcConfigDestroy(cfg);
    return out;
}

}  // namespace

bool TextRenderer::init(Renderer& r) {
    r_ = &r;
    if (!TTF_Init()) {
        log_error("TTF_Init: {}", SDL_GetError());
        return false;
    }
    const std::string sans = match_font("sans-serif");
    const std::string bold = match_font("sans-serif:bold");
    struct { const std::string* file; float size; } spec[6] = {{&bold, 15}, {&sans, 13}, {&bold, 12}, {&bold, 17}, {&bold, 28}, {&bold, 14}};
    for (int i = 0; i < 6; ++i) {
        fonts_[i] = TTF_OpenFont(spec[i].file->c_str(), spec[i].size);
        if (!fonts_[i]) {
            log_error("TTF_OpenFont {}: {}", *spec[i].file, SDL_GetError());
            return false;
        }
    }
    log_info("fonts: {} / {}", sans, bold);
    return true;
}

void TextRenderer::shutdown() {
    flush_cache();
    for (auto*& f : fonts_) {
        if (f) TTF_CloseFont(f);
        f = nullptr;
    }
    TTF_Quit();
}

void TextRenderer::flush_cache() {
    for (auto& [k, e] : cache_) glDeleteTextures(1, &e.tex.id);
    cache_.clear();
}

TTF_Font* TextRenderer::font(Font f) { return fonts_[static_cast<int>(f)]; }

const Texture* TextRenderer::get(const std::string& s, Font f) {
    if (s.empty()) return nullptr;
    std::string key = std::to_string(static_cast<int>(f)) + '\x1f' + s;
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second.tex;
    if (cache_.size() > 512) flush_cache();  // HUD timers create many strings
    SDL_Surface* surf = TTF_RenderText_Blended(font(f), s.c_str(), s.size(), SDL_Color{255, 255, 255, 255});
    if (!surf) return nullptr;
    SDL_Surface* rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(surf);
    Image img;
    img.w = rgba->w;
    img.h = rgba->h;
    img.rgba.resize(static_cast<std::size_t>(img.w) * img.h * 4);
    for (int y = 0; y < img.h; ++y)
        std::memcpy(img.rgba.data() + static_cast<std::size_t>(y) * img.w * 4,
                    static_cast<std::uint8_t*>(rgba->pixels) + static_cast<std::size_t>(y) * rgba->pitch,
                    static_cast<std::size_t>(img.w) * 4);
    SDL_DestroySurface(rgba);
    Texture t;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    // Upload bottom-up so Renderer::quad's top-down UV flip shows it upright.
    std::vector<std::uint8_t> fl(img.rgba.size());
    const std::size_t row = static_cast<std::size_t>(img.w) * 4;
    for (int y = 0; y < img.h; ++y) std::memcpy(fl.data() + y * row, img.rgba.data() + (img.h - 1 - y) * row, row);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, fl.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t.w = img.w;
    t.h = img.h;
    t.hasAlpha = true;
    return &(cache_[key] = Entry{t}).tex;
}

void TextRenderer::draw(const std::string& s, float x, float y, Font f, glm::vec4 color, int align) {
    const Texture* t = get(s, f);
    if (!t) return;
    float w = static_cast<float>(t->w);
    if (align == 1) x -= w;
    else if (align == 2) x -= w / 2;
    r_->quad(x, y, w, static_cast<float>(t->h), t, color);
}

void TextRenderer::draw_shadowed(const std::string& s, float x, float y, Font f, glm::vec4 color, int align) {
    draw(s, x - 2, y - 2, f, {0.3f, 0.3f, 0.3f, color.a}, align);
    draw(s, x, y, f, color, align);
}

float TextRenderer::width(const std::string& s, Font f) {
    const Texture* t = get(s, f);
    return t ? static_cast<float>(t->w) : 0.0f;
}

std::vector<std::string> TextRenderer::wrap(const std::string& s, Font f, float maxW) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= s.size()) {
        std::size_t e = s.find('\n', p);
        std::string para = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (!para.empty() && para.back() == '\r') para.pop_back();
        std::string line;
        std::size_t q = 0;
        while (q <= para.size()) {
            std::size_t sp = para.find(' ', q);
            const std::string word = para.substr(q, sp == std::string::npos ? std::string::npos : sp - q);
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(cand, f) > maxW) {
                out.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            if (sp == std::string::npos) break;
            q = sp + 1;
        }
        out.push_back(line);
        if (e == std::string::npos) break;
        p = e + 1;
    }
    return out;
}

void TextRenderer::draw_lines(const std::string& s, float x, float y, Font f, glm::vec4 c, int align, float lh, bool shadow) {
    if (lh <= 0) lh = line_height(f);
    std::size_t p = 0;
    while (p <= s.size()) {
        std::size_t e = s.find('\n', p);
        std::string line = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (shadow) draw_shadowed(line, x, y, f, c, align);
        else draw(line, x, y, f, c, align);
        y += lh;
        if (e == std::string::npos) break;
        p = e + 1;
    }
}

float TextRenderer::line_height(Font f) { return static_cast<float>(TTF_GetFontLineSkip(font(f))); }

}  // namespace tto
