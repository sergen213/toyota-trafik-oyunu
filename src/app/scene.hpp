#pragma once
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "core/fs.hpp"
#include "data/mapdata.hpp"
#include "data/vdd.hpp"
#include "game/traffic.hpp"
#include "render/renderer.hpp"

namespace tto {

// Loads the original meshes/textures for a map and draws a Sim.
class Scene {
public:
    Scene(Renderer& r, const GameFs& fs, const VddNode& cfg);
    ~Scene();
    void load_map(const MapData& m, std::mt19937& rng);  // scenery, signs, lights, billboards, sky, map actors
    void load_cars(std::mt19937& rng);                  // the 16 vehicle models + wheels + driver
    void load_people();                                 // Insanlar\\a1..a5, k1..k5, ak1..ak5 with the walk cycle
    std::vector<CarParams> car_params() const;
    std::vector<glm::vec3> car_colors(std::mt19937& rng) const;
    std::vector<std::vector<glm::vec3>> people_colors() const;  // HG.vdd Insanlar-<type>-Renkler

    // Camera modes (docs/re/DRIVING.md §6): 1 chase, 2 rigid chase, 3 interior,
    // 4 side, 5 front, 7 top-down, 8 front-high, 10 roadside.
    void set_camera_mode(int m) { camMode_ = m; }
    int camera_mode() const { return camMode_; }
    void cycle_camera(bool next);
    void update_camera(Sim& sim, double dt);
    void update(double dt);  // billboard scroll
    void draw(Sim& sim, int w, int h, std::optional<int> arrowSign, bool talking = false);

    // Mouse picking in question mode: returns sign, car or pedestrian index.
    struct Pick {
        int sign = -1, car = -1, ped = -1;
    };
    Pick pick(Sim& sim, float mx, float my, int w, int h) const;
    const Texture* sign_texture(int number);
    glm::mat4 view() const { return view_; }
    glm::vec3 camera_pos() const { return camPos_; }
    void look_at(glm::vec3 eye, glm::vec3 at) { view_ = glm::lookAt(eye, at, glm::vec3(0, 1, 0)); camPos_ = eye; }
    glm::mat4 proj(int w, int h) const;

private:
    struct CarModel {
        GpuMesh body;
        std::optional<GpuMesh> brakeR, brakeL, signalR, signalL, revR, revL;
        glm::vec3 kfar{}, k1far{}, sfar{}, s1far{};
        float bodyY = -0.5f, scale = 0.007f, wheelScale = 0.0065f;
        int wheel = 0;  // Arabalar\Teker\Teker<n>.3ds, Random(6)+1 per car (0x53fbcc)
        bool loaded = false;
    };
    struct Billboard {
        glm::mat4 frame{1}, panel{1};
        int tex = 0;
        float offset = 0, hold = 0;
    };
    std::optional<std::filesystem::path> resolve(const std::string& tex, const std::vector<std::string>& dirs) const;
    Mesh read_mesh(const std::string& winPath, std::vector<std::string>* dirs);
    GpuMesh load_mesh(const std::string& winPath, const glm::mat4& bake, const std::vector<std::string>& dirs);
    glm::mat4 body_matrix(const Sim& sim, const Car& c) const;

    Renderer& r_;
    const GameFs& fs_;
    const VddNode& cfg_;
    const MapData* map_ = nullptr;
    std::vector<std::pair<GpuMesh, glm::mat4>> scenery_;
    GpuMesh lamp_, signPlate_, pole_, litterBox_, arrow_, beacon_;
    GpuMesh billboardFrame_, billboardPanel_, train_, boom_, post_, durSign_;
    std::array<GpuMesh, 6> wheels_;
    std::vector<Billboard> billboards_;
    std::array<const Texture*, 3> billboardTex_{};
    std::array<CarModel, 16> cars_;
    std::array<GpuMesh, 15> people_;  // 36-frame walk cycle each (yuru.smd / yuruk.smd)
    std::map<std::string, GpuMesh> driver_;  // Bolumler\\Diger\\Anim poses of the Yaris driver
    std::array<const Texture*, 6> sky_{};
    std::map<int, const Texture*> signTex_;
    glm::vec3 trainCenter_{0};
    int camMode_ = 1;
    glm::vec3 camPos_{0, 5, -10}, camTarget_{0};
    glm::vec3 chase_{0};
    bool chaseInit_ = false;
    glm::mat4 view_{1};
    double time_ = 0;
};

}  // namespace tto
