#include "formats/tds.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include "core/fs.hpp"

namespace tto {

int Mesh::material_index(const std::string& name) const {
    for (std::size_t i = 0; i < materials.size(); ++i)
        if (materials[i].name == name) return static_cast<int>(i);
    return -1;
}

namespace {

class Cursor {
public:
    Cursor(std::span<const std::uint8_t> d, std::size_t b, std::size_t e) : d_(d), p_(b), e_(e) {}
    bool more(std::size_t n = 1) const { return p_ + n <= e_; }
    std::size_t pos() const { return p_; }
    std::uint8_t u8() { need(1); return d_[p_++]; }
    std::uint16_t u16() { need(2); std::uint16_t v = static_cast<std::uint16_t>(d_[p_] | d_[p_ + 1] << 8); p_ += 2; return v; }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = d_[p_] | d_[p_ + 1] << 8 | d_[p_ + 2] << 16 | static_cast<std::uint32_t>(d_[p_ + 3]) << 24;
        p_ += 4;
        return v;
    }
    float f32() { std::uint32_t u = u32(); float f; std::memcpy(&f, &u, 4); return f; }
    std::string cstr() {
        std::string s;
        while (true) {
            char c = static_cast<char>(u8());
            if (!c) break;
            s += c;
        }
        return cp1254_to_utf8(s);
    }

private:
    void need(std::size_t n) const {
        if (p_ + n > e_) throw std::runtime_error("3DS: truncated chunk at offset " + std::to_string(p_));
    }
    std::span<const std::uint8_t> d_;
    std::size_t p_, e_;
};

struct Chunk {
    std::uint16_t id;
    std::size_t body, end;
};

Chunk read_chunk(Cursor& c, std::size_t limit) {
    const std::size_t start = c.pos();
    Chunk ch{};
    ch.id = c.u16();
    const std::uint32_t len = c.u32();
    if (len < 6 || start + len > limit)
        throw std::runtime_error("3DS: bad chunk 0x" + std::to_string(ch.id) + " length at offset " + std::to_string(start));
    ch.body = c.pos();
    ch.end = start + len;
    return ch;
}

Vec3 read_color(std::span<const std::uint8_t> d, std::size_t b, std::size_t e) {
    Cursor c(d, b, e);
    while (c.more(6)) {
        Chunk ch = read_chunk(c, e);
        Cursor s(d, ch.body, ch.end);
        if (ch.id == 0x0010 || ch.id == 0x0013) return {s.f32(), s.f32(), s.f32()};
        if (ch.id == 0x0011 || ch.id == 0x0012) return {s.u8() / 255.0f, s.u8() / 255.0f, s.u8() / 255.0f};
        c = Cursor(d, ch.end, e);
    }
    return {0.8f, 0.8f, 0.8f};
}

float read_percent(std::span<const std::uint8_t> d, std::size_t b, std::size_t e) {
    Cursor c(d, b, e);
    if (!c.more(6)) return 0;
    Chunk ch = read_chunk(c, e);
    Cursor s(d, ch.body, ch.end);
    if (ch.id == 0x0030) return s.u16() / 100.0f;
    if (ch.id == 0x0031) return s.f32() / 100.0f;
    return 0;
}

MeshMaterial read_material(std::span<const std::uint8_t> d, std::size_t b, std::size_t e) {
    MeshMaterial m;
    Cursor c(d, b, e);
    while (c.more(6)) {
        Chunk ch = read_chunk(c, e);
        Cursor s(d, ch.body, ch.end);
        switch (ch.id) {
            case 0xA000: m.name = s.cstr(); break;
            case 0xA020: m.diffuse = read_color(d, ch.body, ch.end); break;
            case 0xA050: m.transparency = read_percent(d, ch.body, ch.end); break;
            case 0xA081: m.twoSided = true; break;
            case 0xA200: {  // texture map 1
                Cursor t(d, ch.body, ch.end);
                while (t.more(6)) {
                    Chunk sub = read_chunk(t, ch.end);
                    if (sub.id == 0xA300) { Cursor f(d, sub.body, sub.end); m.texture = f.cstr(); }
                    t = Cursor(d, sub.end, ch.end);
                }
                break;
            }
            default: break;
        }
        c = Cursor(d, ch.end, e);
    }
    return m;
}

struct RawTri {
    std::uint16_t a, b, c;
};

MeshObject read_trimesh(std::span<const std::uint8_t> d, std::size_t b, std::size_t e, std::string name, const Mesh& mesh) {
    std::vector<Vec3> verts;
    std::vector<Vec2> uv;
    std::vector<RawTri> tris;
    std::vector<std::uint32_t> smooth;
    std::vector<int> faceMat;
    Cursor c(d, b, e);
    while (c.more(6)) {
        Chunk ch = read_chunk(c, e);
        Cursor s(d, ch.body, ch.end);
        if (ch.id == 0x4110) {
            const std::uint16_t n = s.u16();
            verts.resize(n);
            for (auto& v : verts) v = {s.f32(), s.f32(), s.f32()};
        } else if (ch.id == 0x4140) {
            const std::uint16_t n = s.u16();
            uv.resize(n);
            for (auto& t : uv) t = {s.f32(), s.f32()};
        } else if (ch.id == 0x4120) {
            const std::uint16_t n = s.u16();
            tris.resize(n);
            for (auto& t : tris) {
                t = {s.u16(), s.u16(), s.u16()};
                s.u16();  // edge flags
            }
            faceMat.assign(n, -1);
            smooth.assign(n, 0);
            Cursor sub(d, s.pos(), ch.end);
            while (sub.more(6)) {
                Chunk f = read_chunk(sub, ch.end);
                Cursor fs(d, f.body, f.end);
                if (f.id == 0x4130) {
                    const int mi = mesh.material_index(fs.cstr());
                    const std::uint16_t cnt = fs.u16();
                    for (std::uint16_t k = 0; k < cnt; ++k) {
                        const std::uint16_t fi = fs.u16();
                        if (fi >= n) throw std::runtime_error("3DS: face material index out of range");
                        faceMat[fi] = mi;
                    }
                } else if (f.id == 0x4150) {
                    for (auto& g : smooth) g = fs.u32();
                }
                sub = Cursor(d, f.end, ch.end);
            }
        }
        c = Cursor(d, ch.end, e);
    }
    for (const auto& t : tris)
        if (t.a >= verts.size() || t.b >= verts.size() || t.c >= verts.size())
            throw std::runtime_error("3DS: face vertex index out of range in object " + name);
    if (!uv.empty() && uv.size() != verts.size()) uv.clear();

    // Face normals, then per-corner smoothing-group normals.
    std::vector<Vec3> fn(tris.size());
    std::vector<std::vector<std::uint32_t>> facesAt(verts.size());
    for (std::size_t i = 0; i < tris.size(); ++i) {
        const auto& A = verts[tris[i].a];
        const auto& B = verts[tris[i].b];
        const auto& C = verts[tris[i].c];
        const Vec3 u{B[0] - A[0], B[1] - A[1], B[2] - A[2]};
        const Vec3 v{C[0] - A[0], C[1] - A[1], C[2] - A[2]};
        Vec3 n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 0) n = {n[0] / l, n[1] / l, n[2] / l};
        fn[i] = n;
        for (auto vi : {tris[i].a, tris[i].b, tris[i].c}) facesAt[vi].push_back(static_cast<std::uint32_t>(i));
    }

    MeshObject o;
    o.name = std::move(name);
    std::vector<int> groupOf(mesh.materials.size() + 1, -1);
    for (std::size_t i = 0; i < tris.size(); ++i) {
        const int mi = faceMat[i];
        int& g = groupOf[static_cast<std::size_t>(mi + 1)];
        if (g < 0) {
            g = static_cast<int>(o.groups.size());
            o.groups.push_back({mi, {}});
        }
        for (auto vi : {tris[i].a, tris[i].b, tris[i].c}) {
            Vec3 n = fn[i];
            if (smooth[i]) {
                n = {0, 0, 0};
                for (auto f : facesAt[vi])
                    if (smooth[f] & smooth[i]) n = {n[0] + fn[f][0], n[1] + fn[f][1], n[2] + fn[f][2]};
                const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l > 0) n = {n[0] / l, n[1] / l, n[2] / l};
            }
            o.groups[static_cast<std::size_t>(g)].indices.push_back(static_cast<std::uint32_t>(o.positions.size()));
            o.positions.push_back(verts[vi]);
            o.normals.push_back(n);
            if (!uv.empty()) o.uvs.push_back(uv[vi]);
        }
    }
    return o;
}

}  // namespace

Mesh load_3ds(std::span<const std::uint8_t> d) {
    Cursor top(d, 0, d.size());
    Chunk mainc = read_chunk(top, d.size());
    if (mainc.id != 0x4D4D) throw std::runtime_error("3DS: missing 0x4D4D main chunk");
    Mesh mesh;
    // Two passes over the editor chunk: materials first so faces can resolve names.
    for (int pass = 0; pass < 2; ++pass) {
        Cursor c(d, mainc.body, mainc.end);
        while (c.more(6)) {
            Chunk ch = read_chunk(c, mainc.end);
            if (ch.id == 0x3D3D) {
                Cursor e(d, ch.body, ch.end);
                while (e.more(6)) {
                    Chunk sub = read_chunk(e, ch.end);
                    if (pass == 0 && sub.id == 0xAFFF) {
                        mesh.materials.push_back(read_material(d, sub.body, sub.end));
                    } else if (pass == 1 && sub.id == 0x4000) {
                        Cursor oc(d, sub.body, sub.end);
                        std::string name = oc.cstr();
                        while (oc.more(6)) {
                            Chunk oc2 = read_chunk(oc, sub.end);
                            if (oc2.id == 0x4100)
                                mesh.objects.push_back(read_trimesh(d, oc2.body, oc2.end, name, mesh));
                            oc = Cursor(d, oc2.end, sub.end);
                        }
                    }
                    e = Cursor(d, sub.end, ch.end);
                }
            }
            c = Cursor(d, ch.end, mainc.end);
        }
    }
    return mesh;
}

}  // namespace tto
