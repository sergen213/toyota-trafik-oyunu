#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/mesh.hpp"
#include "render/image.hpp"

struct SDL_Window;

namespace tto {

// Backend-neutral handles. `id` is a GL texture name or a Vulkan descriptor index.
struct Texture {
    std::uint32_t id = 0;
    int w = 0, h = 0;
    bool hasAlpha = false;
};

// A mesh uploaded to the GPU, drawn per material group. A mesh may hold several
// animation frames of the same topology back to back (frameVerts vertices each).
struct GpuMesh {
    struct Draw {
        std::uint32_t first = 0, count = 0;
        const Texture* tex = nullptr;  // nullptr = untextured
        glm::vec3 color{1.0f};
        float alpha = 1.0f;
        bool tintable = false;  // true for SMD "null.bmp" body faces (vehicle paint) or chosen cloth textures
        bool water = false;     // "deniz.jpg" sea surface: animated water shading
        std::string texName;    // lower-case texture file name from the source mesh
    };
    std::uint32_t handle = 0;  // 0 = empty
    std::vector<Draw> draws;
    std::uint32_t frameVerts = 0;
    int frames = 1;
    glm::vec3 bmin{0}, bmax{0};
};

struct Light {
    glm::vec3 dir{0.3f, -1.0f, 0.4f};  // direction the light travels (world)
    glm::vec3 ambient{0.45f};
    glm::vec3 diffuse{0.75f};
};

struct Fog {
    bool enabled = false;
    glm::vec3 color{0.7f, 0.8f, 0.9f};
    float start = 100.0f, end = 600.0f;
};

struct DrawOpts {
    std::optional<glm::vec3> tint;  // replaces the colour of tintable draws (or all draws with forceTint)
    bool unlit = false;
    float alpha = 1.0f;
    bool forceTint = false;
    int frame = 0;                       // animation frame of a multi-frame mesh
    glm::vec4 uv{0, 0, 1, 1};            // texture coordinate offset (xy) and scale (zw)
    bool castShadow = true;              // ray-traced backends: include in the shadow TLAS
    std::uint64_t id = 0;                // stable id of a moving object (FSR motion vectors); 0 = static
};

// Immediate-mode renderer used by the game: OpenGL 3.3 or Vulkan (with optional
// ray-query shadows). The API mirrors what the original GLScene scene needed.
class Renderer {
public:
    using TextureResolver = std::function<std::optional<std::filesystem::path>(const std::string&)>;
    virtual ~Renderer() = default;
    virtual const char* name() const = 0;
    virtual std::string device() const = 0;
    virtual void shutdown() = 0;

    // ---- resources
    const Texture* texture(const std::filesystem::path& p);  // cached by path
    const Texture* texture_image(const std::string& key, const Image& img);
    Texture create_texture(const Image& img, bool mipRepeat = true) { return backend_texture(img, mipRepeat); }
    virtual void update_texture(const Texture& t, const Image& img) = 0;  // same size as created
    virtual void destroy_texture(Texture& t) = 0;
    GpuMesh upload(const Mesh& m, const glm::mat4& bake, const TextureResolver& res);
    // Several poses of one mesh (identical topology/materials) in one buffer.
    GpuMesh upload_frames(const std::vector<Mesh>& frames, const glm::mat4& bake, const TextureResolver& res);
    virtual void destroy(GpuMesh& m) = 0;

    // ---- frame
    virtual void begin_window(int w, int h) = 0;  // clears the whole window to black
    virtual void end_window() = 0;                // presents
    // Letterbox: begin_frame/begin_2d draw into a w x h viewport at this window offset.
    void set_viewport(int x, int y, int w, int h) { vpx_ = x; vpy_ = y; vpw_ = w; vph_ = h; }
    virtual void begin_frame(int w, int h, const glm::vec3& clear) = 0;
    void set_camera(const glm::mat4& view, const glm::mat4& proj) { view_ = view; proj_ = proj; }
    void set_light(const Light& l) { light_ = l; }
    void set_fog(const Fog& f) { fog_ = f; }
    void set_time(float seconds) { time_ = seconds; }  // animation clock for water
    // Skybox: six textures in order +X(Dogu) -X(Bati) +Y(Ust) -Y(Alt) +Z(Guney) -Z(Kuzey).
    virtual void draw_sky(const std::array<const Texture*, 6>& faces) = 0;
    virtual void draw(const GpuMesh& m, const glm::mat4& model, const DrawOpts& o) = 0;
    void draw(const GpuMesh& m, const glm::mat4& model, std::optional<glm::vec3> tint = std::nullopt, bool unlit = false,
              float alpha = 1.0f, bool forceTint = false) {
        DrawOpts o;
        o.tint = tint;
        o.unlit = unlit;
        o.alpha = alpha;
        o.forceTint = forceTint;
        draw(m, model, o);
    }
    // 2D (pixel coordinates, origin top-left).
    virtual void begin_2d(int w, int h) = 0;
    virtual void quad(float x, float y, float w, float h, const Texture* t, glm::vec4 color = glm::vec4(1),
                      glm::vec4 uv = {0, 0, 1, 1}) = 0;
    // Whole window, RGBA top-down (call after drawing, before end_window).
    virtual Image read_pixels(int w, int h) = 0;

    // Ray tracing (Vulkan only): shadows from ray queries against the scene.
    virtual bool ray_tracing_supported() const { return false; }
    virtual void set_ray_tracing(bool) {}
    virtual bool ray_tracing() const { return false; }
    // AMD FSR 3.1.4 (Vulkan only): -1 off, else FsrQuality (0 native AA, 1 quality, 2 balanced, 3 performance,
    // 4 ultra performance). Frame generation needs the upscaler; it presents an interpolated frame between rendered ones.
    virtual bool upscaler_supported() const { return false; }
    virtual void set_upscaler(int) {}
    virtual int upscaler() const { return -1; }
    virtual bool frame_generation_supported() const { return false; }
    virtual void set_frame_generation(bool) {}
    virtual bool frame_generation() const { return false; }

protected:
    virtual Texture backend_texture(const Image& img, bool mipRepeat) = 0;
    // 8 floats per vertex; `g` (draws filled, handle unset) lets ray-traced backends build a BLAS.
    virtual std::uint32_t backend_vertices(const std::vector<float>& v, const GpuMesh& g) = 0;
    Texture white_;
    int vpx_ = 0, vpy_ = 0, vpw_ = 1, vph_ = 1;
    glm::mat4 view_{1}, proj_{1};
    Light light_;
    Fog fog_;
    float time_ = 0.0f;
    std::unordered_map<std::string, Texture> cache_;
};

// Rows are flipped to bottom-up on upload by every backend, so 3DS/SMD UVs (v = 0 at the
// image bottom) and the 2D top-down UV flip behave identically.
std::vector<std::uint8_t> flip_rows(const Image& img);

// The GL backend needs a current GL 3.3 core context; the Vulkan backend creates its
// own device/swapchain on a window created with SDL_WINDOW_VULKAN.
std::unique_ptr<Renderer> make_gl_renderer();
std::unique_ptr<Renderer> make_vk_renderer(SDL_Window* win, bool vsync, std::string* error);

}  // namespace tto
