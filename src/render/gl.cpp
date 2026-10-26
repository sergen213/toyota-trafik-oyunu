#include "render/gl.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/log.hpp"

namespace tto {
namespace {

const char* kVs3d = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUv;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNrm;
out vec3 vNrm; out vec2 vUv; out float vDist;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vec4 e = uView * w;
  vDist = length(e.xyz);
  vNrm = normalize(uNrm * aNrm);
  vUv = aUv;
  gl_Position = uProj * e;
})";

const char* kFs3d = R"(#version 330 core
in vec3 vNrm; in vec2 vUv; in float vDist;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform vec3 uLightDir, uAmb, uDif;
uniform int uUnlit;
uniform int uFog; uniform vec3 uFogColor; uniform vec2 uFogRange;
out vec4 oColor;
void main() {
  vec4 t = texture(uTex, vUv);
  if (t.a < 0.5) discard;
  vec3 c = t.rgb * uColor.rgb;
  if (uUnlit == 0) {
    float d = abs(dot(normalize(vNrm), -uLightDir));  // GLScene materials light both faces
    c *= uAmb + uDif * d;
  }
  if (uFog != 0) {
    float f = clamp((uFogRange.y - vDist) / (uFogRange.y - uFogRange.x), 0.0, 1.0);
    c = mix(uFogColor, c, f);
  }
  oColor = vec4(c, uColor.a);
})";

const char* kVs2d = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUv;
uniform mat4 uProj; uniform vec4 uRect; uniform vec4 uUvRect;
out vec2 vUv;
void main() {
  vUv = uUvRect.xy + aUv * (uUvRect.zw - uUvRect.xy);
  gl_Position = uProj * vec4(uRect.xy + aPos * uRect.zw, 0.0, 1.0);
})";

const char* kFs2d = R"(#version 330 core
in vec2 vUv; uniform sampler2D uTex; uniform vec4 uColor; out vec4 oColor;
void main() { oColor = texture(uTex, vUv) * uColor; })";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetShaderInfoLog(s, sizeof buf, nullptr, buf);
        log_error("shader compile: {}", buf);
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[2048];
        glGetProgramInfoLog(p, sizeof buf, nullptr, buf);
        log_error("program link: {}", buf);
        return 0;
    }
    return p;
}

GLint U(GLuint p, const char* n) { return glGetUniformLocation(p, n); }

Texture make_texture(const Image& img) {
    Texture t;
    t.w = img.w;
    t.h = img.h;
    for (std::size_t i = 3; i < img.rgba.size(); i += 4)
        if (img.rgba[i] < 255) { t.hasAlpha = true; break; }
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    // Images are top-down; GL expects bottom-up. 3DS/SMD UVs use v=0 at the
    // bottom of the image, so flip rows on upload.
    std::vector<std::uint8_t> flipped(img.rgba.size());
    const std::size_t row = static_cast<std::size_t>(img.w) * 4;
    for (int y = 0; y < img.h; ++y)
        std::copy_n(img.rgba.data() + static_cast<std::size_t>(img.h - 1 - y) * row, row, flipped.data() + y * row);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

}  // namespace

bool Renderer::init() {
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        log_error("glewInit failed");
        return false;
    }
    glGetError();  // GLEW on core profiles leaves GL_INVALID_ENUM
    prog3d_ = link(kVs3d, kFs3d);
    prog2d_ = link(kVs2d, kFs2d);
    if (!prog3d_ || !prog2d_) return false;
    const float q[] = {0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1};
    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void*>(8));
    Image w;
    w.w = w.h = 1;
    w.rgba = {255, 255, 255, 255};
    white_ = make_texture(w);

    // Unit cube for the skybox: 6 faces x 2 triangles, pos + uv; face order as draw_sky().
    const float S = 1.0f;
    struct F { glm::vec3 o, u, v; };
    const F faces[6] = {
        {{S, -S, S}, {0, 0, -2 * S}, {0, 2 * S, 0}},   // +X
        {{-S, -S, -S}, {0, 0, 2 * S}, {0, 2 * S, 0}},  // -X
        {{-S, S, S}, {2 * S, 0, 0}, {0, 0, -2 * S}},   // +Y
        {{-S, -S, -S}, {2 * S, 0, 0}, {0, 0, 2 * S}},  // -Y
        {{-S, -S, S}, {2 * S, 0, 0}, {0, 2 * S, 0}},   // +Z
        {{S, -S, -S}, {-2 * S, 0, 0}, {0, 2 * S, 0}},  // -Z
    };
    std::vector<float> sv;
    for (const auto& f : faces) {
        const glm::vec3 c[4] = {f.o, f.o + f.u, f.o + f.u + f.v, f.o + f.v};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int i : {0, 1, 2, 0, 2, 3}) {
            sv.insert(sv.end(), {c[i].x, c[i].y, c[i].z, 0, 0, 0, uv[i][0], uv[i][1]});
        }
    }
    glGenVertexArrays(1, &skyVao_);
    glGenBuffers(1, &skyVbo_);
    glBindVertexArray(skyVao_);
    glBindBuffer(GL_ARRAY_BUFFER, skyVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sv.size() * 4), sv.data(), GL_STATIC_DRAW);
    for (int a = 0; a < 3; ++a) glEnableVertexAttribArray(static_cast<GLuint>(a));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(12));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(24));
    glBindVertexArray(0);
    return true;
}

void Renderer::shutdown() {
    for (auto& [k, t] : cache_) glDeleteTextures(1, &t.id);
    cache_.clear();
    glDeleteTextures(1, &white_.id);
    glDeleteProgram(prog3d_);
    glDeleteProgram(prog2d_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteBuffers(1, &skyVbo_);
    glDeleteVertexArrays(1, &skyVao_);
}

const Texture* Renderer::texture(const std::filesystem::path& p) {
    const std::string key = p.string();
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second;
    try {
        return &(cache_[key] = make_texture(load_image(p)));
    } catch (const std::exception& e) {
        log_warn("{}", e.what());
        return &(cache_[key] = white_);
    }
}

const Texture* Renderer::texture_image(const std::string& key, const Image& img) {
    if (auto it = cache_.find(key); it != cache_.end()) return &it->second;
    return &(cache_[key] = make_texture(img));
}

GpuMesh Renderer::upload(const Mesh& m, const glm::mat4& bake, const TextureResolver& res) {
    GpuMesh g;
    std::vector<float> v;
    const glm::mat3 nb = glm::inverseTranspose(glm::mat3(bake));
    bool first = true;
    for (const auto& o : m.objects) {
        for (const auto& grp : o.groups) {
            GpuMesh::Draw d;
            d.first = static_cast<GLsizei>(v.size() / 8);
            if (grp.material >= 0) {
                const auto& mat = m.materials[static_cast<std::size_t>(grp.material)];
                d.color = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]};
                d.alpha = 1.0f - mat.transparency;
                const std::string lower = [&] {
                    std::string s = mat.texture;
                    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    return s;
                }();
                if (lower == "null.bmp") {
                    d.tintable = true;
                } else if (!mat.texture.empty()) {
                    if (auto p = res(mat.texture)) {
                        d.tex = texture(*p);
                        d.color = glm::vec3(1.0f);  // GLScene modulates texture with white
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
                if (first) { g.bmin = g.bmax = p; first = false; }
                g.bmin = glm::min(g.bmin, p);
                g.bmax = glm::max(g.bmax, p);
            }
            d.count = static_cast<GLsizei>(v.size() / 8) - d.first;
            if (d.count) g.draws.push_back(d);
        }
    }
    glGenVertexArrays(1, &g.vao);
    glGenBuffers(1, &g.vbo);
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    for (int a = 0; a < 3; ++a) glEnableVertexAttribArray(static_cast<GLuint>(a));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, nullptr);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(12));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(24));
    glBindVertexArray(0);
    return g;
}

void Renderer::destroy(GpuMesh& m) {
    glDeleteBuffers(1, &m.vbo);
    glDeleteVertexArrays(1, &m.vao);
    m = {};
}

void Renderer::begin_frame(int w, int h, const glm::vec3& c) {
    glViewport(vpx_, vpy_, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpx_, vpy_, w, h);
    glClearColor(c.r, c.g, c.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

void Renderer::set_camera(const glm::mat4& view, const glm::mat4& proj) {
    view_ = view;
    proj_ = proj;
}

void Renderer::draw(const GpuMesh& m, const glm::mat4& model, std::optional<glm::vec3> tint, bool unlit, float alpha,
                    bool forceTint) {
    glUseProgram(prog3d_);
    glUniformMatrix4fv(U(prog3d_, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(U(prog3d_, "uView"), 1, GL_FALSE, glm::value_ptr(view_));
    glUniformMatrix4fv(U(prog3d_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj_));
    const glm::mat3 nm = glm::inverseTranspose(glm::mat3(model));
    glUniformMatrix3fv(U(prog3d_, "uNrm"), 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3fv(U(prog3d_, "uLightDir"), 1, glm::value_ptr(glm::normalize(light_.dir)));
    glUniform3fv(U(prog3d_, "uAmb"), 1, glm::value_ptr(light_.ambient));
    glUniform3fv(U(prog3d_, "uDif"), 1, glm::value_ptr(light_.diffuse));
    glUniform1i(U(prog3d_, "uUnlit"), unlit ? 1 : 0);
    glUniform1i(U(prog3d_, "uFog"), fog_.enabled ? 1 : 0);
    glUniform3fv(U(prog3d_, "uFogColor"), 1, glm::value_ptr(fog_.color));
    glUniform2f(U(prog3d_, "uFogRange"), fog_.start, fog_.end);
    glUniform1i(U(prog3d_, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m.vao);
    for (const auto& d : m.draws) {
        const float a = d.alpha * alpha;
        if (a < 0.999f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        glBindTexture(GL_TEXTURE_2D, d.tex ? d.tex->id : white_.id);
        const glm::vec3 c = (d.tintable || forceTint) && tint ? *tint : d.color;
        glUniform4f(U(prog3d_, "uColor"), c.r, c.g, c.b, a);
        glDrawArrays(GL_TRIANGLES, d.first, d.count);
        if (a < 0.999f) {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
    }
    glBindVertexArray(0);
}

void Renderer::draw_sky(const std::array<const Texture*, 6>& faces) {
    glUseProgram(prog3d_);
    glm::mat4 v = glm::mat4(glm::mat3(view_));
    const glm::mat4 model = glm::scale(glm::mat4(1), glm::vec3(10.0f));
    glUniformMatrix4fv(U(prog3d_, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(U(prog3d_, "uView"), 1, GL_FALSE, glm::value_ptr(v));
    glUniformMatrix4fv(U(prog3d_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj_));
    glUniformMatrix3fv(U(prog3d_, "uNrm"), 1, GL_FALSE, glm::value_ptr(glm::mat3(1)));
    glUniform1i(U(prog3d_, "uUnlit"), 1);
    glUniform1i(U(prog3d_, "uFog"), 0);
    glUniform4f(U(prog3d_, "uColor"), 1, 1, 1, 1);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(skyVao_);
    for (int i = 0; i < 6; ++i) {
        glBindTexture(GL_TEXTURE_2D, faces[static_cast<std::size_t>(i)] ? faces[static_cast<std::size_t>(i)]->id : white_.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glDrawArrays(GL_TRIANGLES, i * 6, 6);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::begin_2d(int w, int h) {
    ortho_ = glm::ortho(0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f);
    glViewport(vpx_, vpy_, vpw_, vph_);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::quad(float x, float y, float w, float h, const Texture* t, glm::vec4 color, glm::vec4 uv) {
    glUseProgram(prog2d_);
    glUniformMatrix4fv(U(prog2d_, "uProj"), 1, GL_FALSE, glm::value_ptr(ortho_));
    glUniform4f(U(prog2d_, "uRect"), x, y, w, h);
    // Textures are stored bottom-up; 2D quads use top-down UVs.
    glUniform4f(U(prog2d_, "uUvRect"), uv.x, 1.0f - uv.y, uv.z, 1.0f - uv.w);
    glUniform4fv(U(prog2d_, "uColor"), 1, glm::value_ptr(color));
    glUniform1i(U(prog2d_, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t ? t->id : white_.id);
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

Image Renderer::read_pixels(int w, int h) const {
    Image img;
    img.w = w;
    img.h = h;
    img.rgba.resize(static_cast<std::size_t>(w) * h * 4);
    std::vector<std::uint8_t> tmp(img.rgba.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
    const std::size_t row = static_cast<std::size_t>(w) * 4;
    for (int y = 0; y < h; ++y) std::copy_n(tmp.data() + static_cast<std::size_t>(h - 1 - y) * row, row, img.rgba.data() + y * row);
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    return img;
}

}  // namespace tto
