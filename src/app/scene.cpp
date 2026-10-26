#include "app/scene.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

#include "core/log.hpp"
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
    lamp_ = load_mesh("Bolumler\\Diger\\lamba.3ds", glm::mat4(1), {"Bolumler\\Diger"});
    static const char* faces[6] = {"dogu", "bati", "ust", "alt", "guney", "kuzey"};
    for (int i = 0; i < 6; ++i)
        if (auto p = fs_.find(std::string("Bolumler\\Diger\\GokYuzu\\") + faces[i] + ".jpg")) sky_[static_cast<std::size_t>(i)] = r_.texture(*p);
}

Scene::~Scene() {
    for (auto& [m, t] : scenery_) r_.destroy(m);
    for (auto& m : owned_) r_.destroy(m);
    for (auto* m : {&lamp_, &signPlate_, &pole_, &litterBox_, &arrow_})
        if (m->vao) r_.destroy(*m);
    for (auto& c : cars_) {
        if (c.body.vao) r_.destroy(c.body);
        for (auto* o : {&c.brakeR, &c.brakeL, &c.signalR, &c.signalL, &c.revR, &c.revL})
            if (*o) r_.destroy(**o);
    }
}

std::optional<std::filesystem::path> Scene::resolve(const std::string& tex, const std::vector<std::string>& dirs) const {
    for (const auto& d : dirs)
        if (auto p = fs_.find(d + "\\" + tex)) return p;
    return std::nullopt;
}

GpuMesh Scene::load_mesh(const std::string& winPath0, const glm::mat4& bake, const std::vector<std::string>& dirs) {
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
    auto dirsAll = dirs;
    const auto slash = winPath.find_last_of("\\/");
    if (slash != std::string::npos) dirsAll.push_back(winPath.substr(0, slash));
    Mesh mesh;
    if (to_lower_ascii(path.extension().string()) == ".smd") mesh = parse_smd(read_text(path)).mesh;
    else mesh = load_3ds(read_file(path));
    return r_.upload(mesh, bake, [&](const std::string& t) { return resolve(t, dirsAll); });
}

void Scene::load_map(const MapData& m) {
    map_ = &m;
    const bool logo = to_lower_ascii(cfg_.text("Logo", "True")) != "false";
    std::vector<std::string> dirs;
    if (logo) dirs.push_back("Bolumler\\maps\\Logo");
    dirs.insert(dirs.end(), {"Bolumler\\maps", "Bolumler\\Diger", m.sehirDir});
    for (const auto& me : m.meshes) {
        if (me.relative || me.cls != "TGLFreeForm") continue;  // TGLOXStaMesh is collision only
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

std::vector<glm::vec3> Scene::car_colors(std::mt19937& rng) const {
    std::vector<glm::vec3> out(16, glm::vec3(1));
    for (int i = 0; i < 16; ++i) {
        const VddNode* list = cfg_.at(std::string("Arabalar-") + kVehicleTypes[i].key + "-Renkler");
        if (!list || list->children.empty()) continue;
        const auto& e = list->children[std::uniform_int_distribution<std::size_t>(0, list->children.size() - 1)(rng)].value;
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
        out[static_cast<std::size_t>(i)] = mx > 0 && mx < 0.5f ? c * (1.0f / 0.45f) : c;
    }
    return out;
}

void Scene::load_cars() {
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
        } catch (const std::exception& e) {
            log_warn("car {}: {}", key, e.what());
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

void Scene::draw(Sim& sim, int w, int h, std::optional<int> arrowSign) {
    r_.begin_frame(w, h, {0.55f, 0.7f, 0.9f});
    r_.set_camera(view_, proj(w, h));
    r_.draw_sky(sky_);
    // GLScene default light: two TGLLightSource at (+-700, 700, -+700) (0x52c7ec).
    Light l;
    l.dir = glm::normalize(glm::vec3(0.4f, -1.0f, -0.3f));
    l.ambient = glm::vec3(0.55f);
    l.diffuse = glm::vec3(0.55f);
    r_.set_light(l);
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
            r_.draw(lamp_, m, on[k] ? onC[k] : offC[k], true, 1.0f, true);
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
    for (const auto& lt : sim.litter())
        r_.draw(litterBox_, glm::translate(glm::mat4(1), lt.pos * glm::vec3(1, 0, 1) + glm::vec3(0, 0.1f, 0)) * glm::scale(glm::mat4(1), glm::vec3(0.3f, 0.2f, 0.3f)));
    const bool blink = static_cast<long>(std::lround(3.0 * now)) % 2 != 0;
    for (const auto& c : sim.cars()) {
        if (!c.active || !c.body) continue;
        const CarModel& cm = cars_[static_cast<std::size_t>(c.type - 1)];
        if (!cm.loaded) continue;
        if (c.player && camMode_ == 3) continue;  // interior view: body hidden
        const glm::mat4 B = body_matrix(sim, c);
        r_.draw(cm.body, B, c.color);
        const glm::mat4 T = c.body->transform();
        auto lampAt = [&](const std::optional<GpuMesh>& m, glm::vec3 off) {
            if (m) r_.draw(*m, T * glm::translate(glm::mat4(1), off), std::nullopt, true);
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
            best = {static_cast<int>(i), -1};
        }
    }
    for (std::size_t i = 0; i < sim.cars().size(); ++i) {
        const Car& c = sim.cars()[i];
        if (!c.active || c.player || !c.body) continue;
        const float t = sphere(c.body->position(), 1.4f);
        if (t > 0 && t < bestT) {
            bestT = t;
            best = {-1, static_cast<int>(i)};
        }
    }
    return best;
}

}  // namespace tto
