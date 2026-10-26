#pragma once
#include <GL/glew.h>

#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/mesh.hpp"
#include "render/image.hpp"

namespace tto {

struct Texture {
    GLuint id = 0;
    int w = 0, h = 0;
    bool hasAlpha = false;
};

// A mesh uploaded to the GPU, drawn per material group.
struct GpuMesh {
    struct Draw {
        GLsizei first = 0, count = 0;
        const Texture* tex = nullptr;  // nullptr = untextured
        glm::vec3 color{1.0f};
        float alpha = 1.0f;
        bool tintable = false;  // true for SMD "null.bmp" body faces (vehicle paint)
    };
    GLuint vao = 0, vbo = 0;
    std::vector<Draw> draws;
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

class Renderer {
public:
    // Resolves a texture file name (as stored in a mesh) to a real path.
    using TextureResolver = std::function<std::optional<std::filesystem::path>(const std::string&)>;

    bool init();  // after a GL 3.3 core context is current
    void shutdown();

    const Texture* texture(const std::filesystem::path& p);       // cached
    const Texture* texture_image(const std::string& key, const Image& img);
    GpuMesh upload(const Mesh& m, const glm::mat4& bake, const TextureResolver& res);
    void destroy(GpuMesh& m);

    void begin_frame(int w, int h, const glm::vec3& clear);
    void set_camera(const glm::mat4& view, const glm::mat4& proj);
    void set_light(const Light& l) { light_ = l; }
    void set_fog(const Fog& f) { fog_ = f; }
    // Skybox: six textures in order +X(Dogu) -X(Bati) +Y(Ust) -Y(Alt) +Z(Guney) -Z(Kuzey).
    void draw_sky(const std::array<const Texture*, 6>& faces);

    // tint replaces the colour of tintable draws (vehicle body paint), or of
    // every draw when forceTint is set (lamps, markers).
    void draw(const GpuMesh& m, const glm::mat4& model, std::optional<glm::vec3> tint = std::nullopt,
              bool unlit = false, float alpha = 1.0f, bool forceTint = false);

    // 2D (pixel coordinates, origin top-left).
    void begin_2d(int w, int h);
    void quad(float x, float y, float w, float h, const Texture* t, glm::vec4 color = glm::vec4(1),
              glm::vec4 uv = {0, 0, 1, 1});
    Image read_pixels(int w, int h) const;

private:
    GLuint prog3d_ = 0, prog2d_ = 0, quadVao_ = 0, quadVbo_ = 0, skyVao_ = 0, skyVbo_ = 0;
    glm::mat4 view_{1}, proj_{1}, ortho_{1};
    Light light_;
    Fog fog_;
    Texture white_;
    std::unordered_map<std::string, Texture> cache_;
};

}  // namespace tto
