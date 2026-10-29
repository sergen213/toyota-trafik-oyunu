#include "render/renderer.hpp"

#include <algorithm>
#include <glm/gtc/matrix_inverse.hpp>

#include "core/log.hpp"

namespace tto {

std::vector<std::uint8_t> flip_rows(const Image& img) {
    std::vector<std::uint8_t> out(img.rgba.size());
    const std::size_t row = static_cast<std::size_t>(img.w) * 4;
    for (int y = 0; y < img.h; ++y)
        std::copy_n(img.rgba.data() + static_cast<std::size_t>(img.h - 1 - y) * row, row, out.data() + static_cast<std::size_t>(y) * row);
    return out;
}

const Texture* Renderer::texture(const std::filesystem::path& p) {
    const std::string key = p.string();
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second;
    try {
        return &(cache_[key] = backend_texture(load_image(p), true));
    } catch (const std::exception& e) {
        log_warn("{}", e.what());
        return &(cache_[key] = white_);
    }
}

const Texture* Renderer::texture_image(const std::string& key, const Image& img) {
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second;
    return &(cache_[key] = backend_texture(img, true));
}

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Enhanced graphics: parallax depth, in texels of the texture, for surfaces whose texture reads as relief (the
// map textures are stretched over very different world sizes, so metres would vanish on some and explode on
// others). Height comes from luminance; negative = dark is high (bricks: the light mortar sits back).
float relief_depth(const std::string& tex) {
    static const std::pair<const char*, float> kRelief[] = {
        {"asphalt.jpg", 1.5f}, {"asphalt2.jpg", 1.5f}, {"asphalt3.jpg", 1.5f}, {"yaya.jpg", 1.0f},
        {"kal2.jpg", 1.5f},    {"kal_tas.jpg", 1.5f},  {"tren_yol.jpg", 2.0f}, {"dirt.jpg", 2.0f},
        {"tas.jpg", 2.0f},     {"kaya.jpg", 3.0f},     {"kiremit.jpg", 2.0f},  {"tugla.jpg", -2.0f},
        {"tugla2.jpg", -2.0f},
    };
    for (const auto& [n, d] : kRelief)
        if (tex == n) return d;
    return 0.0f;
}
}  // namespace

GpuMesh Renderer::upload(const Mesh& m, const glm::mat4& bake, const TextureResolver& res) { return upload_frames({m}, bake, res); }

GpuMesh Renderer::upload_frames(const std::vector<Mesh>& frames, const glm::mat4& bake, const TextureResolver& res) {
    GpuMesh g;
    std::vector<float> v;
    const glm::mat3 nb = glm::inverseTranspose(glm::mat3(bake));
    bool first = true;
    for (std::size_t f = 0; f < frames.size(); ++f) {
        const Mesh& m = frames[f];
        const std::size_t frameStart = v.size() / 8;
        for (const auto& o : m.objects) {
            for (const auto& grp : o.groups) {
                GpuMesh::Draw d;
                d.first = static_cast<std::uint32_t>(v.size() / 8 - frameStart);
                if (grp.material >= 0) {
                    const auto& mat = m.materials[static_cast<std::size_t>(grp.material)];
                    d.color = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]};
                    d.alpha = 1.0f - mat.transparency;
                    d.texName = lower(mat.texture);
                    d.water = d.texName == "deniz.jpg";
                    if (d.texName == "null.bmp") {
                        d.tintable = true;
                    } else if (!mat.texture.empty() && f == 0) {
                        if (auto p = res(mat.texture)) {
                            d.tex = texture(*p);
                            d.color = glm::vec3(1.0f);  // GLScene modulates texture with white
                            if (!d.tex->hasAlpha && d.alpha >= 0.999f) d.relief = relief_depth(d.texName);
                        } else {
                            log_warn("texture not found: {}", mat.texture);
                        }
                    }
                }
                for (auto idx : grp.indices) {
                    const glm::vec3 p = glm::vec3(bake * glm::vec4(o.positions[idx][0], o.positions[idx][1], o.positions[idx][2], 1));
                    const glm::vec3 n = glm::normalize(nb * glm::vec3(o.normals[idx][0], o.normals[idx][1], o.normals[idx][2]) + glm::vec3(1e-9f));
                    const Vec2 uv = o.uvs.empty() ? Vec2{0, 0} : o.uvs[idx];
                    v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, uv[0], uv[1]});
                    if (first) {
                        g.bmin = g.bmax = p;
                        first = false;
                    }
                    g.bmin = glm::min(g.bmin, p);
                    g.bmax = glm::max(g.bmax, p);
                }
                d.count = static_cast<std::uint32_t>(v.size() / 8 - frameStart) - d.first;
                if (f == 0 && d.count) g.draws.push_back(d);
            }
        }
        if (f == 0) g.frameVerts = static_cast<std::uint32_t>(v.size() / 8);
        else if (v.size() / 8 - frameStart != g.frameVerts) {
            log_warn("upload_frames: frame {} vertex count differs", f);
            v.resize(frameStart * 8);
            break;
        }
        g.frames = static_cast<int>(f + 1);
    }
    if (!v.empty()) g.handle = backend_vertices(v, g);
    return g;
}

}  // namespace tto
