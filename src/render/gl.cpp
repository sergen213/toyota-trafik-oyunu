#include <GL/glew.h>

#include "render/renderer.hpp"

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
uniform vec4 uUvX;
out vec3 vNrm; out vec2 vUv; out float vDist;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vec4 e = uView * w;
  vDist = length(e.xyz);
  vNrm = normalize(uNrm * aNrm);
  vUv = uUvX.xy + aUv * uUvX.zw;
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

Texture make_texture(const Image& img, bool mipRepeat) {
    Texture t;
    t.w = img.w;
    t.h = img.h;
    for (std::size_t i = 3; i < img.rgba.size(); i += 4)
        if (img.rgba[i] < 255) { t.hasAlpha = true; break; }
    GLuint id = 0;
    glGenTextures(1, &id);
    t.id = id;
    glBindTexture(GL_TEXTURE_2D, id);
    const auto flipped = flip_rows(img);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
    if (mipRepeat) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipRepeat ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mipRepeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mipRepeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    return t;
}

class GlRenderer final : public Renderer {
public:
    bool init();
    const char* name() const override { return "OpenGL 3.3"; }
    std::string device() const override { return reinterpret_cast<const char*>(glGetString(GL_RENDERER)); }
    void shutdown() override;
    void update_texture(const Texture& t, const Image& img) override;
    void destroy_texture(Texture& t) override;
    void destroy(GpuMesh& m) override;
    void begin_window(int w, int h) override;
    void end_window() override {}
    void begin_frame(int w, int h, const glm::vec3& clear) override;
    void draw_sky(const std::array<const Texture*, 6>& faces) override;
    void draw(const GpuMesh& m, const glm::mat4& model, const DrawOpts& o) override;
    using Renderer::draw;
    void begin_2d(int w, int h) override;
    void quad(float x, float y, float w, float h, const Texture* t, glm::vec4 color, glm::vec4 uv) override;
    Image read_pixels(int w, int h) override;

protected:
    Texture backend_texture(const Image& img, bool mipRepeat) override { return make_texture(img, mipRepeat); }
    std::uint32_t backend_vertices(const std::vector<float>& v, const GpuMesh& g) override;

private:
    struct Vao { GLuint vao = 0, vbo = 0; };
    std::unordered_map<std::uint32_t, Vao> vaos_;
    std::uint32_t nextVao_ = 1;
    GLuint prog3d_ = 0, prog2d_ = 0, quadVao_ = 0, quadVbo_ = 0, skyVao_ = 0, skyVbo_ = 0;
    glm::mat4 ortho_{1};
};

}  // namespace

bool GlRenderer::init() {
    glewExperimental = GL_TRUE;
    const GLenum ge = glewInit();
    if (ge != GLEW_OK && ge != GLEW_ERROR_NO_GLX_DISPLAY) {  // EGL contexts (Wayland) report NO_GLX_DISPLAY but load fine
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
    white_ = make_texture(w, true);

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

void GlRenderer::shutdown() {
    for (auto& [k, t] : cache_) {
        GLuint id = t.id;
        if (id != white_.id) glDeleteTextures(1, &id);
    }
    cache_.clear();
    for (auto& [h, v] : vaos_) {
        glDeleteBuffers(1, &v.vbo);
        glDeleteVertexArrays(1, &v.vao);
    }
    vaos_.clear();
    GLuint w = white_.id;
    glDeleteTextures(1, &w);
    glDeleteProgram(prog3d_);
    glDeleteProgram(prog2d_);
    glDeleteBuffers(1, &quadVbo_);
    glDeleteVertexArrays(1, &quadVao_);
    glDeleteBuffers(1, &skyVbo_);
    glDeleteVertexArrays(1, &skyVao_);
}

void GlRenderer::update_texture(const Texture& t, const Image& img) {
    glBindTexture(GL_TEXTURE_2D, t.id);
    const auto flipped = flip_rows(img);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.w, img.h, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
}

void GlRenderer::destroy_texture(Texture& t) {
    GLuint id = t.id;
    if (id) glDeleteTextures(1, &id);
    t = {};
}

std::uint32_t GlRenderer::backend_vertices(const std::vector<float>& v, const GpuMesh&) {
    Vao g;
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
    const std::uint32_t h = nextVao_++;
    vaos_[h] = g;
    return h;
}

void GlRenderer::destroy(GpuMesh& m) {
    if (auto it = vaos_.find(m.handle); it != vaos_.end()) {
        glDeleteBuffers(1, &it->second.vbo);
        glDeleteVertexArrays(1, &it->second.vao);
        vaos_.erase(it);
    }
    m = {};
}

void GlRenderer::begin_window(int w, int h) {
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GlRenderer::begin_frame(int w, int h, const glm::vec3& c) {
    glViewport(vpx_, vpy_, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpx_, vpy_, w, h);
    glClearColor(c.r, c.g, c.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

void GlRenderer::draw(const GpuMesh& m, const glm::mat4& model, const DrawOpts& o) {
    const auto it = vaos_.find(m.handle);
    if (it == vaos_.end()) return;
    glUseProgram(prog3d_);
    glUniformMatrix4fv(U(prog3d_, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(U(prog3d_, "uView"), 1, GL_FALSE, glm::value_ptr(view_));
    glUniformMatrix4fv(U(prog3d_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj_));
    const glm::mat3 nm = glm::inverseTranspose(glm::mat3(model));
    glUniformMatrix3fv(U(prog3d_, "uNrm"), 1, GL_FALSE, glm::value_ptr(nm));
    glUniform3fv(U(prog3d_, "uLightDir"), 1, glm::value_ptr(glm::normalize(light_.dir)));
    glUniform3fv(U(prog3d_, "uAmb"), 1, glm::value_ptr(light_.ambient));
    glUniform3fv(U(prog3d_, "uDif"), 1, glm::value_ptr(light_.diffuse));
    glUniform1i(U(prog3d_, "uUnlit"), o.unlit ? 1 : 0);
    glUniform1i(U(prog3d_, "uFog"), fog_.enabled ? 1 : 0);
    glUniform3fv(U(prog3d_, "uFogColor"), 1, glm::value_ptr(fog_.color));
    glUniform2f(U(prog3d_, "uFogRange"), fog_.start, fog_.end);
    glUniform4fv(U(prog3d_, "uUvX"), 1, glm::value_ptr(o.uv));
    glUniform1i(U(prog3d_, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(it->second.vao);
    const GLint base = static_cast<GLint>(m.frameVerts) * std::clamp(o.frame, 0, m.frames - 1);
    for (const auto& d : m.draws) {
        const float a = d.alpha * o.alpha;
        if (a < 0.999f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        glBindTexture(GL_TEXTURE_2D, d.tex ? d.tex->id : white_.id);
        const glm::vec3 c = (d.tintable || o.forceTint) && o.tint ? *o.tint : d.color;
        glUniform4f(U(prog3d_, "uColor"), c.r, c.g, c.b, a);
        glDrawArrays(GL_TRIANGLES, base + static_cast<GLint>(d.first), static_cast<GLsizei>(d.count));
        if (a < 0.999f) {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
    }
    glBindVertexArray(0);
}

void GlRenderer::draw_sky(const std::array<const Texture*, 6>& faces) {
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
    glUniform4f(U(prog3d_, "uUvX"), 0, 0, 1, 1);
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

void GlRenderer::begin_2d(int w, int h) {
    ortho_ = glm::ortho(0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f);
    glViewport(vpx_, vpy_, vpw_, vph_);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GlRenderer::quad(float x, float y, float w, float h, const Texture* t, glm::vec4 color, glm::vec4 uv) {
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

Image GlRenderer::read_pixels(int w, int h) {
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

std::unique_ptr<Renderer> make_gl_renderer() {
    auto r = std::make_unique<GlRenderer>();
    if (!r->init()) return nullptr;
    return r;
}

}  // namespace tto
