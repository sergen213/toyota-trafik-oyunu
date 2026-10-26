#include "formats/smd.hpp"

#include <charconv>
#include <stdexcept>
#include <string>

#include "core/fs.hpp"

namespace tto {
namespace {

class Lines {
public:
    explicit Lines(std::string_view t) : t_(t) {}
    bool next(std::string_view& out) {
        while (p_ < t_.size()) {
            std::size_t e = t_.find('\n', p_);
            if (e == std::string_view::npos) e = t_.size();
            std::string_view l = t_.substr(p_, e - p_);
            p_ = e + 1;
            ++no_;
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.remove_suffix(1);
            while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
            if (l.empty() || l.starts_with("//")) continue;
            out = l;
            return true;
        }
        return false;
    }
    [[noreturn]] void fail(const std::string& m) const { throw std::runtime_error("SMD line " + std::to_string(no_) + ": " + m); }

private:
    std::string_view t_;
    std::size_t p_ = 0;
    int no_ = 0;
};

template <class T>
T num(std::string_view& s, const Lines& L) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    T v{};
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{}) L.fail("expected number in '" + std::string(s) + "'");
    s.remove_prefix(static_cast<std::size_t>(p - s.data()));
    return v;
}

}  // namespace

SmdModel parse_smd(std::string_view text) {
    SmdModel m;
    Lines L(text);
    std::string_view l;
    if (!L.next(l) || !l.starts_with("version")) L.fail("missing 'version' header");
    MeshObject obj;
    obj.name = "smd";
    std::vector<int> groupOfMat;
    while (L.next(l)) {
        if (l == "nodes") {
            while (L.next(l) && l != "end") {
                SmdBone b;
                b.id = num<int>(l, L);
                const auto q1 = l.find('"');
                const auto q2 = l.find('"', q1 + 1);
                if (q1 == std::string_view::npos || q2 == std::string_view::npos) L.fail("bone name must be quoted");
                b.name = std::string(l.substr(q1 + 1, q2 - q1 - 1));
                l.remove_prefix(q2 + 1);
                b.parent = num<int>(l, L);
                m.bones.push_back(std::move(b));
            }
        } else if (l == "skeleton") {
            while (L.next(l) && l != "end") {
                if (l.starts_with("time")) {
                    m.frames.emplace_back(m.bones.size());
                    continue;
                }
                if (m.frames.empty()) L.fail("bone pose before 'time'");
                const int id = num<int>(l, L);
                std::size_t bi = 0;
                while (bi < m.bones.size() && m.bones[bi].id != id) ++bi;
                if (bi == m.bones.size()) L.fail("pose for unknown bone " + std::to_string(id));
                SmdBonePose p;
                p.pos = {num<float>(l, L), num<float>(l, L), num<float>(l, L)};
                p.rot = {num<float>(l, L), num<float>(l, L), num<float>(l, L)};
                m.frames.back()[bi] = p;
            }
        } else if (l == "triangles") {
            while (L.next(l) && l != "end") {
                std::string tex(l);
                int mi = m.mesh.material_index(tex);
                if (mi < 0) {
                    mi = static_cast<int>(m.mesh.materials.size());
                    MeshMaterial mat;
                    mat.name = tex;
                    mat.texture = cp1254_to_utf8(tex);
                    mat.diffuse = {1, 1, 1};
                    m.mesh.materials.push_back(mat);
                    groupOfMat.push_back(static_cast<int>(obj.groups.size()));
                    obj.groups.push_back({mi, {}});
                }
                auto& g = obj.groups[static_cast<std::size_t>(groupOfMat[static_cast<std::size_t>(mi)])];
                for (int k = 0; k < 3; ++k) {
                    if (!L.next(l)) L.fail("truncated triangle");
                    const int bone = num<int>(l, L);
                    Vec3 p{num<float>(l, L), num<float>(l, L), num<float>(l, L)};
                    Vec3 n{num<float>(l, L), num<float>(l, L), num<float>(l, L)};
                    Vec2 t{num<float>(l, L), num<float>(l, L)};
                    g.indices.push_back(static_cast<std::uint32_t>(obj.positions.size()));
                    obj.positions.push_back(p);
                    obj.normals.push_back(n);
                    obj.uvs.push_back(t);
                    m.vertexBone.push_back(bone);
                }
            }
        } else {
            L.fail("unknown section '" + std::string(l) + "'");
        }
    }
    if (!obj.positions.empty()) m.mesh.objects.push_back(std::move(obj));
    return m;
}

}  // namespace tto

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

namespace tto {
namespace {

glm::mat4 bone_local(const SmdBonePose& p) {
    glm::mat4 m = glm::translate(glm::mat4(1), {p.pos[0], p.pos[1], p.pos[2]});
    m = glm::rotate(m, p.rot[2], {0, 0, 1});
    m = glm::rotate(m, p.rot[1], {0, 1, 0});
    m = glm::rotate(m, p.rot[0], {1, 0, 0});
    return m;
}

std::vector<glm::mat4> world_matrices(const std::vector<SmdBone>& bones, const std::vector<SmdBonePose>& pose) {
    std::vector<glm::mat4> w(bones.size(), glm::mat4(1));
    std::unordered_map<int, std::size_t> byId;
    for (std::size_t i = 0; i < bones.size(); ++i) byId[bones[i].id] = i;
    // Parents precede children in studiomdl files; resolve in order, falling back to a second pass.
    std::vector<bool> done(bones.size(), false);
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < bones.size(); ++i) {
            if (done[i]) continue;
            const glm::mat4 l = i < pose.size() ? bone_local(pose[i]) : glm::mat4(1);
            const auto it = byId.find(bones[i].parent);
            if (bones[i].parent < 0 || it == byId.end()) {
                w[i] = l;
                done[i] = true;
            } else if (done[it->second]) {
                w[i] = w[it->second] * l;
                done[i] = true;
            }
        }
    return w;
}

}  // namespace

Mesh smd_pose(const SmdModel& ref, const SmdModel& anim, std::size_t frame) {
    Mesh out = ref.mesh;
    if (ref.frames.empty() || anim.frames.empty() || out.objects.empty()) return out;
    frame = std::min(frame, anim.frames.size() - 1);
    const auto refW = world_matrices(ref.bones, ref.frames[0]);
    // Animation pose remapped onto the reference skeleton by bone name.
    std::vector<SmdBonePose> pose = ref.frames[0];
    for (std::size_t i = 0; i < ref.bones.size(); ++i)
        for (std::size_t k = 0; k < anim.bones.size(); ++k)
            if (anim.bones[k].name == ref.bones[i].name && k < anim.frames[frame].size()) pose[i] = anim.frames[frame][k];
    const auto animW = world_matrices(ref.bones, pose);
    std::unordered_map<int, std::size_t> byId;
    for (std::size_t i = 0; i < ref.bones.size(); ++i) byId[ref.bones[i].id] = i;
    std::vector<glm::mat4> skin(ref.bones.size());
    for (std::size_t i = 0; i < skin.size(); ++i) skin[i] = animW[i] * glm::inverse(refW[i]);
    MeshObject& o = out.objects[0];
    for (std::size_t v = 0; v < o.positions.size() && v < ref.vertexBone.size(); ++v) {
        const auto it = byId.find(ref.vertexBone[v]);
        if (it == byId.end()) continue;
        const glm::mat4& M = skin[it->second];
        const glm::vec4 p = M * glm::vec4(o.positions[v][0], o.positions[v][1], o.positions[v][2], 1);
        const glm::vec3 n = glm::mat3(M) * glm::vec3(o.normals[v][0], o.normals[v][1], o.normals[v][2]);
        o.positions[v] = {p.x, p.y, p.z};
        o.normals[v] = {n.x, n.y, n.z};
    }
    return out;
}

}  // namespace tto
