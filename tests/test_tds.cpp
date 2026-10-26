#include <gtest/gtest.h>

#include <cstring>

#include "formats/tds.hpp"

using namespace tto;

namespace {
struct W {
    std::vector<std::uint8_t> d;
    void u16(int v) { d.push_back(static_cast<std::uint8_t>(v)); d.push_back(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
    void f32(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
    void str(const char* s) { d.insert(d.end(), s, s + std::strlen(s) + 1); }
    std::size_t open(int id) { u16(id); std::size_t p = d.size(); u32(0); return p; }
    void close(std::size_t p) {
        const std::uint32_t len = static_cast<std::uint32_t>(d.size() - (p - 2));
        for (int i = 0; i < 4; ++i) d[p + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(len >> (8 * i));
    }
};

// One textured quad (2 triangles) with material "M" -> "TEX.JPG", smoothing group 1.
std::vector<std::uint8_t> quad3ds() {
    W w;
    auto m = w.open(0x4D4D);
    auto e = w.open(0x3D3D);
    auto mat = w.open(0xAFFF);
    auto mn = w.open(0xA000); w.str("M"); w.close(mn);
    auto tm = w.open(0xA200); auto mf = w.open(0xA300); w.str("TEX.JPG"); w.close(mf); w.close(tm);
    w.close(mat);
    auto o = w.open(0x4000); w.str("plane");
    auto t = w.open(0x4100);
    auto v = w.open(0x4110); w.u16(4);
    const float P[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    for (auto& p : P) for (float c : p) w.f32(c);
    w.close(v);
    auto uv = w.open(0x4140); w.u16(4);
    for (auto& p : P) { w.f32(p[0]); w.f32(p[1]); }
    w.close(uv);
    auto f = w.open(0x4120); w.u16(2);
    w.u16(0); w.u16(1); w.u16(2); w.u16(7);
    w.u16(0); w.u16(2); w.u16(3); w.u16(7);
    auto fm = w.open(0x4130); w.str("M"); w.u16(2); w.u16(0); w.u16(1); w.close(fm);
    auto sm = w.open(0x4150); w.u32(1); w.u32(1); w.close(sm);
    w.close(f);
    w.close(t);
    w.close(o);
    w.close(e);
    w.close(m);
    return w.d;
}
}  // namespace

TEST(Tds, LoadsMaterialsFacesUvsAndSmoothNormals) {
    const auto d = quad3ds();
    const Mesh m = load_3ds(d);
    ASSERT_EQ(m.materials.size(), 1u);
    EXPECT_EQ(m.materials[0].texture, "TEX.JPG");
    ASSERT_EQ(m.objects.size(), 1u);
    const auto& o = m.objects[0];
    EXPECT_EQ(o.name, "plane");
    ASSERT_EQ(o.groups.size(), 1u);
    EXPECT_EQ(o.groups[0].material, 0);
    EXPECT_EQ(o.groups[0].indices.size(), 6u);
    ASSERT_EQ(o.uvs.size(), o.positions.size());
    for (const auto& n : o.normals) EXPECT_NEAR(n[2], 1.0f, 1e-6f);  // planar, CCW in XY -> +Z
}

TEST(Tds, RejectsTruncatedAndOutOfRangeInput) {
    auto d = quad3ds();
    auto cut = d;
    cut.resize(cut.size() - 10);
    EXPECT_THROW(load_3ds(cut), std::runtime_error);
    std::vector<std::uint8_t> junk = {0x11, 0x11, 6, 0, 0, 0};
    EXPECT_THROW(load_3ds(junk), std::runtime_error);
    // Corrupt a face index (first face's vertex a) to 99.
    for (std::size_t i = 0; i + 1 < d.size(); ++i) {
        if (d[i] == 0x20 && d[i + 1] == 0x41) {  // 0x4120 chunk id
            d[i + 8] = 99;
            break;
        }
    }
    EXPECT_THROW(load_3ds(d), std::runtime_error);
}
