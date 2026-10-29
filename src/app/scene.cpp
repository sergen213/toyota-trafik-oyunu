#include "app/scene.hpp"

#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

#include "core/log.hpp"
#include "formats/smd.hpp"
#include "formats/tds.hpp"

namespace tto {
namespace {

// 3DS/SMD (Z-up) -> GLScene object with Direction (0,1,0): (x, y, z) -> s*(x, z, -y).
glm::mat4 zup_to_yup(float s) {
    glm::mat4 m(0.0f);
    m[0] = glm::vec4(s, 0, 0, 0);   // file x -> world x
    m[1] = glm::vec4(0, 0, -s, 0);  // file y -> world -z
    m[2] = glm::vec4(0, s, 0, 0);   // file z -> world y
    m[3] = glm::vec4(0, 0, 0, 1);
    return m;
}

glm::mat4 basis(glm::vec3 pos, glm::vec3 fwd) {
    fwd = glm::normalize(glm::vec3(fwd.x, 0, fwd.z) + glm::vec3(0, 0, 1e-6f));
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 x = glm::normalize(glm::cross(up, fwd));
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x, 0);
    m[1] = glm::vec4(up, 0);
    m[2] = glm::vec4(fwd, 0);
    m[3] = glm::vec4(pos, 1);
    return m;
}

Mesh box_mesh(const std::string& tex, glm::vec3 color) {
    Mesh m;
    MeshMaterial mat;
    mat.name = "box";
    mat.texture = tex;
    mat.diffuse = {color.r, color.g, color.b};
    m.materials.push_back(mat);
    MeshObject o;
    o.name = "box";
    MeshGroup g{0, {}};
    const glm::vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& N : n) {
        const glm::vec3 u = std::abs(N.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 v = glm::cross(N, u);
        const glm::vec3 c = N * 0.5f;
        const glm::vec3 q[4] = {c - u * 0.5f - v * 0.5f, c + u * 0.5f - v * 0.5f, c + u * 0.5f + v * 0.5f, c - u * 0.5f + v * 0.5f};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int i : {0, 1, 2, 0, 2, 3}) {
            g.indices.push_back(static_cast<std::uint32_t>(o.positions.size()));
            o.positions.push_back({q[i].x, q[i].y, q[i].z});
            o.normals.push_back({N.x, N.y, N.z});
            o.uvs.push_back({uv[i][0], uv[i][1]});
        }
    }
    o.groups.push_back(g);
    m.objects.push_back(o);
    return m;
}

// Front-facing unit quad in the XY plane (normal +Z), UV 0..1.
Mesh plate_mesh() {
    Mesh m;
    MeshMaterial mat;
    mat.name = "plate";
    mat.diffuse = {1, 1, 1};
    m.materials.push_back(mat);
    MeshObject o;
    MeshGroup g{0, {}};
    const glm::vec3 q[4] = {{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0.5f, 0.5f, 0}, {-0.5f, 0.5f, 0}};
    const float uv[4][2] = {{1, 0}, {0, 0}, {0, 1}, {1, 1}};  // seen from +Z: image upright, not mirrored
    for (int i : {0, 1, 2, 0, 2, 3}) {
        g.indices.push_back(static_cast<std::uint32_t>(o.positions.size()));
        o.positions.push_back({q[i].x, q[i].y, q[i].z});
        o.normals.push_back({0, 0, 1});
        o.uvs.push_back({uv[i][0], uv[i][1]});
    }
    o.groups.push_back(g);
    m.objects.push_back(o);
    return m;
}

glm::vec3 v(const V3& a) { return {a[0], a[1], a[2]}; }

}  // namespace

Scene::Scene(Renderer& r, const GameFs& fs, const VddNode& cfg) : r_(r), fs_(fs), cfg_(cfg) {
    const auto none = [](const std::string&) { return std::optional<std::filesystem::path>{}; };
    signPlate_ = r_.upload(plate_mesh(), glm::mat4(1), none);
    pole_ = r_.upload(box_mesh("", {0.55f, 0.55f, 0.58f}), glm::mat4(1), none);
    litterBox_ = r_.upload(box_mesh("", {0.35f, 0.3f, 0.2f}), glm::mat4(1), none);
    arrow_ = r_.upload(box_mesh("", {1, 0, 0}), glm::mat4(1), none);
    beacon_ = r_.upload(box_mesh("", {1, 1, 1}), glm::mat4(1), none);
    lamp_ = load_mesh("Bolumler\\Diger\\lamba.3ds", glm::mat4(1), {"Bolumler\\Diger"});
    static const char* faces[6] = {"dogu", "bati", "ust", "alt", "guney", "kuzey"};
    for (int i = 0; i < 6; ++i)
        if (auto p = fs_.find(std::string("Bolumler\\Diger\\GokYuzu\\") + faces[i] + ".jpg")) sky_[static_cast<std::size_t>(i)] = r_.texture(*p);
}

Scene::~Scene() {
    auto kill = [&](GpuMesh& m) {
        if (m.handle) r_.destroy(m);
    };
    for (auto& p : people_) kill(p);
    for (auto& [n, m] : driver_) kill(m);
    for (auto& [m, t] : scenery_) kill(m);
    for (auto& w : wheels_) kill(w);
    for (auto* m : {&lamp_, &signPlate_, &pole_, &litterBox_, &arrow_, &beacon_, &billboardFrame_, &billboardPanel_, &train_, &boom_, &post_, &durSign_})
        kill(*m);
    for (auto& c : cars_) {
        kill(c.body);
        for (auto* o : {&c.brakeR, &c.brakeL, &c.signalR, &c.signalL, &c.revR, &c.revL})
            if (*o) kill(**o);
    }
}

std::optional<std::filesystem::path> Scene::resolve(const std::string& tex, const std::vector<std::string>& dirs) const {
    for (const auto& d : dirs)
        if (auto p = fs_.find(d + "\\" + tex)) return p;
    return std::nullopt;
}

Mesh Scene::read_mesh(const std::string& winPath0, std::vector<std::string>* dirs) {
    // Map builders use paths relative to the game dir or, for maps 2-4, to Bolumler\IlkHarita
    // (GLScene media search path); a leading backslash is ignored.
    std::string winPath = winPath0;
    while (!winPath.empty() && (winPath[0] == '\\' || winPath[0] == '/')) winPath.erase(0, 1);
    if (!fs_.find(winPath))
        for (const char* base : {"Bolumler\\IlkHarita\\", "Bolumler\\Diger\\"})
            if (fs_.find(base + winPath)) {
                winPath = base + winPath;
                break;
            }
    const auto path = fs_.need(winPath);
    const auto slash = winPath.find_last_of("\\/");
    if (dirs && slash != std::string::npos) dirs->push_back(winPath.substr(0, slash));
    if (to_lower_ascii(path.extension().string()) == ".smd") return parse_smd(read_text(path)).mesh;
    return load_3ds(read_file(path));
}

GpuMesh Scene::load_mesh(const std::string& winPath, const glm::mat4& bake, const std::vector<std::string>& dirs) {
    auto dirsAll = dirs;
    const Mesh mesh = read_mesh(winPath, &dirsAll);
    return r_.upload(mesh, bake, [&](const std::string& t) { return resolve(t, dirsAll); });
}

void Scene::load_map(const MapData& m, std::mt19937& rng) {
    map_ = &m;
    const bool logo = to_lower_ascii(cfg_.text("Logo", "True")) != "false";
    std::vector<std::string> dirs;
    if (logo) dirs.push_back("Bolumler\\maps\\Logo");
    dirs.insert(dirs.end(), {"Bolumler\\maps", "Bolumler\\Diger", m.sehirDir, "Bolumler\\Diger\\TIsaret", "Insanlar", "Arabalar"});
    for (const auto& me : m.meshes) {
        if (me.relative || me.cls != "TGLFreeForm") continue;  // TGLOXStaMesh is collision only
        if (to_lower_ascii(me.file).find("dingding") != std::string::npos) continue;  // drawn by the level crossings
        glm::mat4 bake(1.0f);
        bake[0] = glm::vec4(v(me.x), 0);
        bake[1] = glm::vec4(v(me.y), 0);
        bake[2] = glm::vec4(v(me.z), 0);
        bake[3] = glm::vec4(v(me.origin), 1);
        try {
            scenery_.push_back({load_mesh(me.file, bake, dirs), glm::mat4(1)});
        } catch (const std::exception& e) {
            log_warn("scenery {}: {}", me.file, e.what());
        }
    }
    for (const auto& s : m.signs) sign_texture(s.number);
    // Billboards (0x509388, MAPS.md §Billboards): billboard.smd frame at scale 0.01 and a
    // 4.88 x 3.07 x 0.3 panel at local y 1.38 showing half of an ad texture that scrolls.
    if (!m.billboards.empty()) {
        try {
            billboardFrame_ = load_mesh("Bolumler\\Diger\\billboard.smd", zup_to_yup(0.01f), {"Bolumler\\Diger"});
        } catch (const std::exception& e) {
            log_warn("billboard: {}", e.what());
        }
        const auto none = [](const std::string&) { return std::optional<std::filesystem::path>{}; };
        billboardPanel_ = r_.upload(plate_mesh(), glm::mat4(1), none);
        static const char* ads[3] = {"Bolumler\\Diger\\01_02.jpg", "Bolumler\\Diger\\03_04.jpg", "Bolumler\\Diger\\05_06.jpg"};
        for (int i = 0; i < 3; ++i)
            if (auto p = fs_.find(ads[i])) billboardTex_[static_cast<std::size_t>(i)] = r_.texture(*p);
        for (const auto& [pos, facing] : m.billboards) {
            Billboard b;
            const glm::vec3 base = v(pos) - glm::vec3(0, 2.5f, 0);
            // Frame bbox centre in x is ~1.13 m (file -143..369 cm): centre the panel on it [G].
            b.frame = basis(base, v(facing)) * glm::translate(glm::mat4(1), {-1.13f, 0, 0});
            b.panel = basis(base, v(facing)) * glm::translate(glm::mat4(1), {0, 2.5f + 1.38f, 0.16f}) * glm::scale(glm::mat4(1), {4.88f, 3.07f, 1});
            b.tex = std::uniform_int_distribution<int>(0, 2)(rng);
            billboards_.push_back(b);
        }
    }
    // Map 3: train (Arabalar\tren.smd, scale 0.01) and the level-crossing models (TDingDing).
    if (m.index == 3) {
        try {
            const SmdModel t = parse_smd(read_text(fs_.need("Arabalar\\tren.smd")));
            glm::vec3 mn(1e9f), mx(-1e9f);
            for (const auto& o : t.mesh.objects)
                for (const auto& p : o.positions) {
                    const glm::vec3 w = glm::vec3(zup_to_yup(0.01f) * glm::vec4(p[0], p[1], p[2], 1));
                    mn = glm::min(mn, w);
                    mx = glm::max(mx, w);
                }
            // Centre the 55 m train on its bounding box, wheels on the dummy (y 1.15 is the dummy height).
            const glm::mat4 bake = glm::translate(glm::mat4(1), {-(mn.x + mx.x) / 2, -mn.y - 1.15f, -(mn.z + mx.z) / 2}) * zup_to_yup(0.01f);
            train_ = r_.upload(t.mesh, bake, [&](const std::string& tx) { return resolve(tx, {"Arabalar", "Bolumler\\maps"}); });
            boom_ = load_mesh("Bolumler\\Diger\\DingDing.3ds", zup_to_yup(0.01f), {"Bolumler\\Diger"});
            post_ = load_mesh("Bolumler\\Diger\\DingDing2.3ds", zup_to_yup(0.01f), {"Bolumler\\Diger\\TIsaret", "Bolumler\\Diger"});
        } catch (const std::exception& e) {
            log_warn("train/level crossing: {}", e.what());
        }
    }
}

const Texture* Scene::sign_texture(int n) {
    if (auto it = signTex_.find(n); it != signTex_.end()) return it->second;
    const Texture* t = nullptr;
    if (auto p = fs_.find("Bolumler\\Diger\\TIsaret\\Levha" + std::to_string(n) + ".jpg")) t = r_.texture(*p);
    signTex_[n] = t;
    return t;
}

std::vector<CarParams> Scene::car_params() const {
    std::vector<CarParams> out(16);
    for (int i = 0; i < 16; ++i) {
        const std::string k = std::string("Arabalar-") + kVehicleTypes[i].key + "-";
        CarParams& p = out[static_cast<std::size_t>(i)];
        p.radius = static_cast<float>(cfg_.number(k + "Radius").value_or(0.25));
        p.wheelA = {static_cast<float>(cfg_.number(k + "WheelPosA_X").value_or(0.1)),
                    static_cast<float>(cfg_.number(k + "WheelPosA_Y").value_or(-0.05)),
                    static_cast<float>(cfg_.number(k + "WheelPosA_Z").value_or(-0.4))};
        p.wheelC = {static_cast<float>(cfg_.number(k + "WheelPosC_X").value_or(0.1)),
                    static_cast<float>(cfg_.number(k + "WheelPosC_Y").value_or(-0.05)),
                    static_cast<float>(cfg_.number(k + "WheelPosC_Z").value_or(0.5))};
        if (i == 15) {  // Boy/En/Yukseklik are read only for CiftTeker (0x53ea52)
            p.length = static_cast<float>(cfg_.number(k + "Boy").value_or(2.8));
            p.width = static_cast<float>(cfg_.number(k + "En").value_or(0.95));
            p.height = static_cast<float>(cfg_.number(k + "Yukseklik").value_or(0.7));
        }
        p.frontTraction = i == 14;
    }
    return out;
}

namespace {
glm::vec3 parse_rgb(const std::string& e) {
    glm::vec3 c(1);
    std::size_t p = 0;
    for (int k = 0; k < 3; ++k) {
        const std::size_t q = e.find(';', p);
        try {
            c[k] = std::stof(e.substr(p, q - p));
        } catch (...) {
        }
        p = q == std::string::npos ? q : q + 1;
        if (p == std::string::npos) break;
    }
    // Renkler values are stored in a dark 0..0.42 range; GLScene applies them as
    // diffuse under strong default lighting, so they are scaled for this shader.
    const float mx = std::max({c.r, c.g, c.b});
    return mx > 0 && mx < 0.5f ? c * (1.0f / 0.45f) : c;
}
}  // namespace

std::vector<glm::vec3> Scene::car_colors(std::mt19937& rng) const {
    std::vector<glm::vec3> out(16, glm::vec3(1));
    for (int i = 0; i < 16; ++i) {
        const VddNode* list = cfg_.at(std::string("Arabalar-") + kVehicleTypes[i].key + "-Renkler");
        if (!list || list->children.empty()) continue;
        out[static_cast<std::size_t>(i)] = parse_rgb(list->children[std::uniform_int_distribution<std::size_t>(0, list->children.size() - 1)(rng)].value);
    }
    return out;
}

std::vector<std::vector<glm::vec3>> Scene::people_colors() const {
    std::vector<std::vector<glm::vec3>> out(3);
    for (int t = 0; t < 3; ++t)
        if (const VddNode* list = cfg_.at("Insanlar-" + std::to_string(t + 1) + "-Renkler"))
            for (const auto& e : list->children) out[static_cast<std::size_t>(t)].push_back(parse_rgb(e.value));
    return out;
}

void Scene::load_cars(std::mt19937& rng) {
    for (int i = 0; i < 6; ++i) {
        const std::string f = "Arabalar\\Teker\\Teker" + std::to_string(i + 1) + ".3ds";
        if (!fs_.find(f)) continue;
        try {
            // Hub centred on the axle (the cylinder spans file x -34..-11, axis along x): the original's freeform sits
            // flush in the arches like this (Wine reference), not offset by the raw file coordinates.
            const glm::mat4 bake = glm::translate(glm::mat4(1), {0.2275f, 0, 0}) * zup_to_yup(0.01f);
            wheels_[static_cast<std::size_t>(i)] = load_mesh(f, bake, {"Arabalar\\Teker"});
        } catch (const std::exception& e) {
            log_warn("wheel {}: {}", f, e.what());
        }
    }
    for (int i = 0; i < 16; ++i) {
        const std::string key = kVehicleTypes[i].key;
        auto dirP = fs_.find(std::string("Arabalar\\") + key);
        if (!dirP) continue;
        const std::string folder = dirP->filename().string();
        const std::string base = "Arabalar\\" + folder;
        std::string smd;
        for (const auto& e : std::filesystem::directory_iterator(*dirP)) {
            const std::string n = to_lower_ascii(e.path().filename().string());
            if (n == to_lower_ascii(folder) + ".smd") smd = e.path().filename().string();
        }
        if (smd.empty()) continue;
        CarModel& cm = cars_[static_cast<std::size_t>(i)];
        cm.scale = i == 14 ? 0.008f : 0.007f;
        const std::string k = std::string("Arabalar-") + key + "-";
        cm.bodyY = static_cast<float>(cfg_.number(k + "BodyPosY").value_or(-0.5));
        cm.wheelScale = 0.025f * static_cast<float>(cfg_.number(k + "Radius").value_or(0.25));
        cm.wheel = std::uniform_int_distribution<int>(0, 5)(rng);
        auto num = [&](const std::string& s) { return static_cast<float>(cfg_.number(k + s).value_or(0.1)); };
        cm.kfar = {num("kfarx"), num("kfary"), num("kfarz")};
        cm.k1far = {num("k1farx"), num("k1fary"), num("k1farz")};
        cm.sfar = {num("sfarx"), num("sfary"), num("sfarz")};
        cm.s1far = {num("s1farx"), num("s1fary"), num("s1farz")};
        const std::vector<std::string> dirs = {base + "\\Kaliteli", base, "Arabalar\\Teker", "Arabalar\\Sinyaller",
                                               "Arabalar\\Logo\\K", "Arabalar", "Bolumler\\Diger"};
        try {
            cm.body = load_mesh(base + "\\" + smd, zup_to_yup(cm.scale), dirs);
            auto lamp = [&](const char* f) -> std::optional<GpuMesh> {
                if (!fs_.find(base + "\\" + f)) return std::nullopt;
                return load_mesh(base + "\\" + f, zup_to_yup(cm.scale), {"Arabalar\\Sinyaller", "Arabalar"});
            };
            cm.brakeR = lamp("kirmizi_sag.3DS");
            cm.brakeL = lamp("kirmizi_sol.3DS");
            cm.signalR = lamp("sari_sag.3DS");
            cm.signalL = lamp("sari_sol.3DS");
            cm.revR = lamp("beyaz_sag.3DS");
            cm.revL = lamp("beyaz_sol.3DS");
            cm.loaded = true;
            if (i == 13 && fs_.find(base + "\\Dur.3DS")) durSign_ = load_mesh(base + "\\Dur.3DS", zup_to_yup(0.01f), {base});
        } catch (const std::exception& e) {
            log_warn("car {}: {}", key, e.what());
        }
    }
    // Yaris driver (WORLD.md §4): ref.smd posed by the steering animations, scale 0.01.
    if (fs_.find("Bolumler\\Diger\\Anim\\ref.smd")) {
        try {
            const SmdModel ref = parse_smd(read_text(fs_.need("Bolumler\\Diger\\Anim\\ref.smd")));
            const auto res = [&](const std::string& t) { return resolve(t, {"Bolumler\\Diger\\Anim", "Arabalar"}); };
            for (const char* a : {"surus", "konus", "sag_03", "sag_06", "sag_09", "sag_12", "sag_15", "sola_03", "sola_06", "sola_09", "sola_12", "sola_15"}) {
                const auto p = fs_.find(std::string("Bolumler\\Diger\\Anim\\") + a + ".smd");
                if (!p) continue;
                const SmdModel an = parse_smd(read_text(*p));
                std::vector<Mesh> frames;
                for (std::size_t f = 0; f < std::max<std::size_t>(an.frames.size(), 1); ++f) frames.push_back(smd_pose(ref, an, f));
                driver_[a] = r_.upload_frames(frames, zup_to_yup(0.01f), res);
            }
        } catch (const std::exception& e) {
            log_warn("driver: {}", e.what());
        }
    }
}

void Scene::load_people() {
    static const char* pre[3] = {"a", "k", "ak"};
    static const char* cloth[3] = {"ceket.jpg", "gomlek.jpg", "kazak.jpg"};  // tinted per type (0x5467af) [L]
    std::optional<SmdModel> walk[2];
    for (int k = 0; k < 2; ++k)
        if (auto p = fs_.find(k ? "Insanlar\\yuruk.smd" : "Insanlar\\yuru.smd")) {
            try {
                walk[k] = parse_smd(read_text(*p));
            } catch (const std::exception& e) {
                log_warn("walk animation: {}", e.what());
            }
        }
    for (int i = 0; i < 15; ++i) {
        const std::string f = std::string("Insanlar\\") + pre[i / 5] + std::to_string(i % 5 + 1) + ".smd";
        if (!fs_.find(f)) continue;
        try {
            const SmdModel sm = parse_smd(read_text(fs_.need(f)));
            // Scale the reference pose to 1.75 m tall, feet on the ground (Z-up file -> Y-up world).
            float zmin = 1e9f, zmax = -1e9f;
            for (const auto& o : sm.mesh.objects)
                for (const auto& v : o.positions) zmin = std::min(zmin, v[2]), zmax = std::max(zmax, v[2]);
            const float sc = zmax > zmin ? 1.75f / (zmax - zmin) : 0.01f;
            const glm::mat4 bake = glm::translate(glm::mat4(1), {0, -zmin * sc, 0}) * zup_to_yup(sc);
            // Walk cycle: types 1 and 3 use yuru.smd, type 2 yuruk.smd (0x546963).
            const auto& anim = walk[i / 5 == 1 ? 1 : 0];
            std::vector<Mesh> frames;
            if (anim)
                for (std::size_t fr = 0; fr < anim->frames.size(); ++fr) frames.push_back(smd_pose(sm, *anim, fr));
            if (frames.empty()) frames.push_back(sm.mesh);
            GpuMesh g = r_.upload_frames(frames, bake, [&](const std::string& t) { return resolve(t, {"Insanlar", "Arabalar"}); });
            for (auto& d : g.draws)
                if (d.texName == cloth[i / 5]) d.tintable = true;
            people_[static_cast<std::size_t>(i)] = std::move(g);
        } catch (const std::exception& e) {
            log_warn("pedestrian {}: {}", f, e.what());
        }
    }
}

void Scene::cycle_camera(bool next) {
    // 0x532470 / 0x532394 cycle orders (Yaris: 3 is the interior view).
    static const int fwd[] = {1, 2, 3, 4, 5, 7, 8, 10};
    constexpr int n = static_cast<int>(std::size(fwd));
    int i = 0;
    while (i < n && fwd[i] != camMode_) ++i;
    i = next ? (i + 1) % n : (i + n - 1) % n;
    camMode_ = fwd[i];
}

glm::mat4 Scene::proj(int w, int h) const {
    // GLScene focal length 100 at 800x600 (calibrated against the original's chase view).
    return glm::perspective(glm::radians(45.0f), static_cast<float>(w) / static_cast<float>(std::max(h, 1)), 0.1f, 3000.0f);
}

glm::mat4 Scene::body_matrix(const Sim&, const Car& c) const {
    return c.body->transform() * glm::translate(glm::mat4(1), glm::vec3(0, cars_[static_cast<std::size_t>(c.type - 1)].bodyY, 0));
}

void Scene::update(double dt) {
    time_ += dt;
    // Billboard scroll (0x5095b8): offset.y += dt/6 until the next half is shown, then hold 3 s.
    for (auto& b : billboards_) {
        if (b.hold > 0) {
            b.hold -= static_cast<float>(dt);
            continue;
        }
        const int before = static_cast<int>(std::trunc(b.offset * 2 + 1));
        b.offset += static_cast<float>(dt) / 6;
        if (static_cast<int>(std::trunc(b.offset * 2 + 1)) != before) {
            b.offset = static_cast<float>(before) / 2.0f;
            b.hold = 3.0f;
        }
        if (b.offset >= 1) b.offset -= 1;
    }
}

void Scene::update_camera(Sim& sim, double) {
    Car& p = sim.player();
    const glm::mat4 T = p.body->transform();
    const glm::vec3 pos = p.body->position();
    auto local = [&](glm::vec3 l) { return glm::vec3(T * glm::vec4(l, 1)); };
    glm::vec3 eye, at;
    switch (camMode_) {
        case 1: {  // chase dummy, 6 iterations per frame (0x531d64)
            if (!chaseInit_) {
                chase_ = pos - p.body->forward() * 5.0f;
                chaseInit_ = true;
            }
            for (int i = 0; i < 6; ++i) {
                glm::vec3 d = pos - chase_;
                float dist = glm::length(d);
                chase_.y = pos.y + 2.0f - dist / 6.0f - 0.72f;
                if (dist > 6.0f) chase_ += glm::normalize(d) * (dist - 6.0f);
            }
            eye = chase_ + glm::vec3(0, 1, 0);
            at = pos;
            break;
        }
        case 2: eye = local({0, 1, -5}); at = pos; break;
        case 3: eye = local({0, 0.26f, -0.81f}); at = local({0, 0.26f, 4}); break;
        case 4: eye = local({p.body->params().width / 2 + 1, -0.15f, -0.8f}); at = local({0, 0, 4}); break;
        case 5: eye = local({0, 1, 5}); at = pos; break;
        case 7: eye = pos + glm::vec3(0, 75, 0.01f); at = pos; break;
        case 8: eye = local({0, 1.5f, 5}); at = pos; break;
        case 10: {
            const glm::vec3 A = glm::vec3(sim.map().nodes[static_cast<std::size_t>(std::max(p.from, 0))].pos[0], 4,
                                          sim.map().nodes[static_cast<std::size_t>(std::max(p.from, 0))].pos[2]);
            eye = A;
            at = pos;
            break;
        }
        default: eye = local({0, 1, -5}); at = pos; break;
    }
    if (camMode_ != 1) chaseInit_ = false;
    camPos_ = eye;
    camTarget_ = at;
    const glm::vec3 up = camMode_ == 7 ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    view_ = glm::lookAt(camPos_, camTarget_, up);
}

void Scene::draw(Sim& sim, int w, int h, std::optional<int> arrowSign, bool talking) {
    r_.begin_frame(w, h, {0.55f, 0.7f, 0.9f});
    r_.set_camera(view_, proj(w, h));
    // GLScene default light: two TGLLightSource at (+-700, 700, -+700) (0x52c7ec).
    Light l;
    l.dir = glm::normalize(glm::vec3(0.4f, -1.0f, -0.3f));
    l.ambient = glm::vec3(0.55f);
    l.diffuse = glm::vec3(0.55f);
    r_.set_light(l);
    r_.set_time(static_cast<float>(std::fmod(sim.wall(), 3600.0)));  // water/cloud animation; wrapped to keep float precision
    r_.draw_sky(sky_);  // after light and clock: the enhanced cloud layer is lit and wind-animated
    for (const auto& [m, t] : scenery_) r_.draw(m, t);
    const double now = sim.wall();
    // Traffic lights: three lamba.3ds per head (0x53b3e0), lit = emissive colour.
    const auto& ls = sim.lights();
    for (std::size_t i = 0; i < sim.map().lights.size(); ++i) {
        const auto& L = sim.map().lights[i];
        const glm::mat4 head = basis(v(L.pos), v(L.facing));
        const glm::vec3 onC[3] = {{1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const glm::vec3 offC[3] = {{0.1f, 0, 0}, {0.1f, 0.1f, 0}, {0, 0.1f, 0}};
        const bool on[3] = {ls[i].red, ls[i].yellow, ls[i].green};
        const float y[3] = {0.28f, 0.0f, -0.28f};
        for (int k = 0; k < 3; ++k) {
            const glm::mat4 m = head * glm::translate(glm::mat4(1), {0, y[k], 0.05f}) * glm::scale(glm::mat4(1), glm::vec3(0.022f));
            DrawOpts o;
            o.tint = on[k] ? onC[k] : offC[k];
            o.unlit = o.forceTint = true;
            o.castShadow = false;
            r_.draw(lamp_, m, o);
        }
    }
    // Signs: plate 0.45 x 0.7 at +0.35, pole 0.07 x 1.0 at -0.5 (0x546ef8).
    for (std::size_t i = 0; i < sim.map().signs.size(); ++i) {
        const auto& s = sim.map().signs[i];
        const glm::mat4 root = basis(v(s.pos), v(s.facing));
        GpuMesh plate = signPlate_;
        if (!plate.draws.empty()) plate.draws[0].tex = sign_texture(s.number);
        r_.draw(plate, root * glm::translate(glm::mat4(1), {0, 0.35f, 0.01f}) * glm::scale(glm::mat4(1), {0.45f, 0.7f, 1}));
        r_.draw(pole_, root * glm::translate(glm::mat4(1), {0, -0.5f, 0}) * glm::scale(glm::mat4(1), {0.07f, 1.0f, 0.07f}));
        r_.draw(pole_, root * glm::translate(glm::mat4(1), {0, 0.35f, -0.01f}) * glm::scale(glm::mat4(1), {0.45f, 0.7f, 0.01f}));
        if (arrowSign && *arrowSign == static_cast<int>(i))
            r_.draw(arrow_, root * glm::translate(glm::mat4(1), {0, 1.3f, 0}) * glm::scale(glm::mat4(1), {0.2f, 0.6f, 0.2f}), std::nullopt, true);
    }
    // Billboards with the scrolling ad (half of the texture visible).
    for (const auto& b : billboards_) {
        if (billboardFrame_.handle) r_.draw(billboardFrame_, b.frame, glm::vec3(0.6f));
        GpuMesh panel = billboardPanel_;
        if (!panel.draws.empty()) panel.draws[0].tex = billboardTex_[static_cast<std::size_t>(b.tex)];
        DrawOpts o;
        o.uv = {0, 0.5f - b.offset, 1, 0.5f};
        r_.draw(panel, b.panel, o);
    }
    for (const auto& lt : sim.litter())
        r_.draw(litterBox_, glm::translate(glm::mat4(1), lt.pos * glm::vec3(1, 0, 1) + glm::vec3(0, 0.1f, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.3f, 0.2f, 0.3f)));
    // Pedestrians: walk cycle frame from the walked distance (FrameInterval ~30 ms at cruise).
    // DrawOpts::id: stable ids of moving objects for FSR motion vectors (static scenery uses 0).
    std::uint64_t pedId = 0x8000;
    for (const auto& p : sim.pedestrians()) {
        ++pedId;
        const GpuMesh& m = people_[static_cast<std::size_t>(p.model)];
        if (!p.active || !m.handle) continue;
        DrawOpts o;
        o.tint = p.tint;
        o.id = pedId;
        o.frame = p.speed > 0.05f || m.frames <= 1 ? static_cast<int>(p.animTime / 0.03f) % std::max(m.frames, 1) : 0;
        r_.draw(m, basis(p.pos, p.dir), o);
    }
    // Train and level crossings (map 3).
    if (sim.has_train() && sim.train().running && train_.handle)
        r_.draw(train_, basis(sim.train_pos(), {0, 0, -1}), [] {
            DrawOpts o;
            o.id = 0x9000;
            return o;
        }());
    std::uint64_t boomId = 0x9100;
    for (const auto& x : sim.crossings()) {
        DrawOpts bo;
        bo.id = ++boomId;
        const glm::mat4 root = basis(x.pos, x.facing);
        if (post_.handle) r_.draw(post_, root);
        if (boom_.handle)
            r_.draw(boom_, root * glm::translate(glm::mat4(1), {0.4f, 0.9f, 0}) * glm::rotate(glm::mat4(1), glm::radians(x.boom), {0, 0, 1}), bo);
    }
    const bool blink = static_cast<long>(std::lround(3.0 * now)) % 2 != 0;
    const bool beat = static_cast<long>(std::lround(8.0 * now)) % 2 != 0;
    std::uint64_t carId = 0;
    for (const auto& c : sim.cars()) {
        carId += 0x100;  // + part: 0 body, 1-4 wheels, 5.. lamps/beacons/signs/driver
        std::uint64_t part = 0;  // sequential for always-drawn parts; lamps use fixed ids (drawn conditionally)
        auto ids = [&](DrawOpts o, std::uint64_t fixed = 0) {
            o.id = 0x10000 + carId + (fixed ? fixed : part++);
            return o;
        };
        if (!c.active || !c.body) continue;
        const CarModel& cm = cars_[static_cast<std::size_t>(c.type - 1)];
        if (!cm.loaded) continue;
        const glm::mat4 T = c.body->transform();
        if (c.player && camMode_ == 3) {
            // Interior view: body hidden; the driver steers with the wheel (sag_/sola_ poses).
            const float s = c.body->steer;
            const char* pose = talking ? "konus" : s > 0.9f ? "sag_15" : s > 0.7f ? "sag_12" : s > 0.5f ? "sag_09" : s > 0.3f ? "sag_06" : s > 0.1f ? "sag_03"
                               : s < -0.9f ? "sola_15" : s < -0.7f ? "sola_12" : s < -0.5f ? "sola_09" : s < -0.3f ? "sola_06" : s < -0.1f ? "sola_03" : "surus";
            if (auto it = driver_.find(pose); it != driver_.end()) {
                DrawOpts o;
                o.frame = static_cast<int>(time_ / 0.025) % std::max(it->second.frames, 1);  // FrameInterval 25
                o = ids(o);
                r_.draw(it->second, T * glm::translate(glm::mat4(1), {0.04f, -0.5f, -0.33f}), o);
            }
            continue;
        }
        const glm::mat4 B = body_matrix(sim, c);
        {
            DrawOpts bo;
            bo.tint = c.color;
            r_.draw(cm.body, B, ids(bo));
        }
        // Wheels: Teker<n>.3ds on every ODE wheel body, oriented as the original sets them up (0x53fc55..0x53fe7b):
        // Direction (0,1,0), Up (0,-1,0) on WheelLF/LB and Up (0,1,0) on WheelRF/RB. GLScene resolves those to a 180 deg
        // roll about the wheel's forward axis on the left (car +X) wheels and identity on the right. Proper rotations
        // only: the rim dish (file x -31.1, facing -X) ends up outboard on both sides and the tyre rolls with the body
        // (positive spin about +X = forward; the old yaw/mirror pair ran it backwards). The rotation is rebuilt from the
        // hinge2 angles (yaw=steer about chassis Y, spin about axle X); the raw wheel-body quaternion carries free-axis
        // wobble that made rear tyres visibly wiggle.
        if (const GpuMesh& wm = wheels_[static_cast<std::size_t>(cm.wheel)]; wm.handle && c.type != 16) {
            const glm::mat4 invT = glm::affineInverse(T);
            const glm::mat4 S = glm::scale(glm::mat4(1), glm::vec3(cm.wheelScale / 0.01f));
            const glm::mat4 roll = glm::rotate(glm::mat4(1), glm::pi<float>(), {0, 0, 1});
            for (int i = 0; i < 4; ++i) {
                const glm::vec3 cpos = glm::vec3(invT * c.body->wheel_transform(i)[3]);  // centre in car frame
                const glm::vec2 a = c.body->wheel_angles(i);
                glm::mat4 R = glm::rotate(glm::mat4(1), a.x, {0, 1, 0}) * glm::rotate(glm::mat4(1), a.y, {1, 0, 0});
                if (cpos.x > 0) R = R * roll;
                r_.draw(wm, T * (glm::translate(glm::mat4(1), cpos) * R * S), ids({}));
            }
        }
        auto lampAt = [&](const std::optional<GpuMesh>& m, glm::vec3 off) {
            if (!m) return;
            DrawOpts o;
            o.unlit = true;
            o.castShadow = false;
            r_.draw(*m, T * glm::translate(glm::mat4(1), off), ids(o, 0x40 + static_cast<std::uint64_t>(&m - &cm.brakeR)));
        };
        if (c.brakeLights) {
            lampAt(cm.brakeR, cm.kfar);
            lampAt(cm.brakeL, cm.k1far);
        }
        if (blink && c.signalRight) lampAt(cm.signalR, cm.sfar);
        if (blink && c.signalLeft) lampAt(cm.signalL, cm.s1far);
        if (c.reverseLights && c.body->speed > 0) {
            lampAt(cm.revR, cm.sfar);
            lampAt(cm.revL, cm.sfar);
        }
        // Emergency beacons (0x544254): 2 (ambulance) or 3 (police) roof lamps alternating while the siren is on.
        if (c.type == 12 || c.type == 13) {
            const int n = c.type == 12 ? 2 : 3;
            const float top = cm.bodyY + cm.body.bmax.y + 0.06f;
            for (int k = 0; k < n; ++k) {
                const bool lit = c.siren && ((k % 2 == 0) == beat);
                const glm::vec3 col = c.type == 12 ? glm::vec3(1, 0.1f, 0.1f) : (k == 1 ? glm::vec3(1, 0.1f, 0.1f) : glm::vec3(0.1f, 0.3f, 1));
                const float x = (static_cast<float>(k) - static_cast<float>(n - 1) / 2) * 0.35f;
                DrawOpts o;
                o.tint = lit ? col : col * 0.25f;
                o.unlit = lit;
                o.forceTint = true;
                o.castShadow = false;
                r_.draw(beacon_, T * glm::translate(glm::mat4(1), {x, top, cm.body.bmax.z * 0.55f}) * glm::scale(glm::mat4(1), {0.22f, 0.12f, 0.18f}), ids(o));
            }
        }
        // School bus "Dur" sign (Arabalar\OkulTasiti\Dur.3ds): lit red (emission 2,0,0) while unloading.
        if (c.type == 14 && durSign_.handle) {
            DrawOpts o;
            if (c.unloading) {
                o.tint = glm::vec3(1, 0.15f, 0.15f);
                o.unlit = o.forceTint = true;
            }
            // Placement on the bus is not recovered [G]: left side, ahead of the middle, facing out.
            const glm::vec3 at{cm.body.bmin.x - 0.12f, cm.bodyY + (cm.body.bmin.y + cm.body.bmax.y) * 0.5f, cm.body.bmax.z * 0.35f};
            r_.draw(durSign_, T * glm::translate(glm::mat4(1), at) * glm::rotate(glm::mat4(1), glm::radians(90.0f), {0, 1, 0}) * glm::scale(glm::mat4(1), glm::vec3(2.0f)), ids(o));
        }
    }
}

Scene::Pick Scene::pick(Sim& sim, float mx, float my, int w, int h) const {
    const glm::mat4 inv = glm::inverse(proj(w, h) * view_);
    const float nx = 2 * mx / static_cast<float>(w) - 1, ny = 1 - 2 * my / static_cast<float>(h);
    glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
    const glm::vec3 o = glm::vec3(a) / a.w, d = glm::normalize(glm::vec3(b) / b.w - o);
    Pick best;
    float bestT = 1e9f;
    auto sphere = [&](glm::vec3 c, float r) {
        const glm::vec3 oc = o - c;
        const float bq = glm::dot(oc, d), cq = glm::dot(oc, oc) - r * r, disc = bq * bq - cq;
        if (disc < 0) return -1.0f;
        const float t = -bq - std::sqrt(disc);
        return t > 0 ? t : -1.0f;
    };
    for (std::size_t i = 0; i < sim.map().signs.size(); ++i) {
        const auto& s = sim.map().signs[i];
        const float t = sphere(v(s.pos) + glm::vec3(0, 0.35f, 0), 0.45f);
        if (t > 0 && t < bestT) {
            bestT = t;
            best = {static_cast<int>(i), -1, -1};
        }
    }
    for (std::size_t i = 0; i < sim.cars().size(); ++i) {
        const Car& c = sim.cars()[i];
        if (!c.active || c.player || !c.body) continue;
        const float t = sphere(c.body->position(), 1.4f);
        if (t > 0 && t < bestT) {
            bestT = t;
            best = {-1, static_cast<int>(i), -1};
        }
    }
    for (std::size_t i = 0; i < sim.pedestrians().size(); ++i) {
        const auto& p = sim.pedestrians()[i];
        if (!p.active) continue;
        const float t = sphere(p.pos + glm::vec3(0, 0.9f, 0), 0.6f);
        if (t > 0 && t < bestT) {
            bestT = t;
            best = {-1, -1, static_cast<int>(i)};
        }
    }
    return best;
}

}  // namespace tto
