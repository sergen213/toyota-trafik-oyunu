#include <gtest/gtest.h>

#include "formats/smd.hpp"

using namespace tto;

TEST(Smd, ParsesBonesFramesAndTrianglesByTexture) {
    const char* src =
        "version 1\r\n"
        "nodes\r\n  0 \"Object01\"  -1\r\n  1 \"bas\" 0\r\nend\r\n"
        "skeleton\r\ntime 0\r\n  0 0.0 -1.88 71.5 -1.570796 3.141593 1.570796\r\n  1 1 2 3 0 0 0\r\n"
        "time 1\r\n  0 0 0 0 0 0 0\r\nend\r\n"
        "triangles\r\nnull.bmp\r\n"
        "  0 1 2 3 0 0 1 0.1 0.2\r\n  0 4 5 6 0 0 1 0.3 0.4\r\n  1 7 8 9 0 0 1 0.5 0.6\r\n"
        "cam.jpg\r\n"
        "  0 1 1 1 0 1 0 0 0\r\n  0 2 2 2 0 1 0 1 0\r\n  0 3 3 3 0 1 0 1 1\r\n"
        "null.bmp\r\n"
        "  0 0 0 0 0 0 1 0 0\r\n  0 0 0 0 0 0 1 0 0\r\n  0 0 0 0 0 0 1 0 0\r\n"
        "end\r\n";
    const SmdModel m = parse_smd(src);
    ASSERT_EQ(m.bones.size(), 2u);
    EXPECT_EQ(m.bones[1].name, "bas");
    EXPECT_EQ(m.bones[1].parent, 0);
    ASSERT_EQ(m.frames.size(), 2u);
    EXPECT_FLOAT_EQ(m.frames[0][0].rot[1], 3.141593f);
    ASSERT_EQ(m.mesh.materials.size(), 2u);
    ASSERT_EQ(m.mesh.objects.size(), 1u);
    const auto& o = m.mesh.objects[0];
    ASSERT_EQ(o.groups.size(), 2u);
    EXPECT_EQ(o.groups[0].indices.size(), 6u) << "both null.bmp triangles share one group";
    EXPECT_EQ(m.vertexBone[2], 1);
    EXPECT_FLOAT_EQ(o.uvs[1][1], 0.4f);
}

TEST(Smd, ReportsMalformedLines) {
    EXPECT_THROW(parse_smd("nodes\nend\n"), std::runtime_error);  // no version
    EXPECT_THROW(parse_smd("version 1\ntriangles\ntex.bmp\n 0 1 2\nend\n"), std::runtime_error);
    EXPECT_THROW(parse_smd("version 1\nskeleton\n 0 0 0 0 0 0 0\nend\n"), std::runtime_error);
    EXPECT_THROW(parse_smd("version 1\nbogus\n"), std::runtime_error);
}

TEST(Smd, PoseSkinsVerticesByBoneName) {
    // Bone "arm" (child of root) rotated 90 degrees about Z in the animation moves its vertex
    // (1,0,0) relative to the bone origin (0,0,1) to (0,1,0)+origin; root-bound vertices stay.
    const char* ref =
        "version 1\nnodes\n  0 \"root\" -1\n  1 \"arm\" 0\nend\n"
        "skeleton\ntime 0\n  0 0 0 0 0 0 0\n  1 0 0 1 0 0 0\nend\n"
        "triangles\nt.bmp\n  0 5 0 0 0 0 1 0 0\n  1 1 0 1 1 0 0 0 0\n  1 0 0 1 0 0 1 0 0\nend\n";
    const char* anim =
        "version 1\nnodes\n  0 \"other_root\" -1\n  1 \"arm\" 0\nend\n"
        "skeleton\ntime 0\n  0 9 9 9 0 0 0\n  1 0 0 1 0 0 1.5707963\nend\n";
    const SmdModel r = parse_smd(ref), a = parse_smd(anim);
    const Mesh m = smd_pose(r, a, 0);
    const auto& p = m.objects[0].positions;
    EXPECT_NEAR(p[0][0], 5, 1e-5);  // root bone not in the animation: unchanged
    EXPECT_NEAR(p[1][0], 0, 1e-5);
    EXPECT_NEAR(p[1][1], 1, 1e-5);
    EXPECT_NEAR(p[1][2], 1, 1e-5);
    EXPECT_NEAR(m.objects[0].normals[1][1], 1, 1e-5);  // normal rotated with the bone
    const Mesh same = smd_pose(r, r, 0);
    for (std::size_t i = 0; i < p.size(); ++i)
        for (int k = 0; k < 3; ++k) EXPECT_NEAR(same.objects[0].positions[i][k], r.mesh.objects[0].positions[i][k], 1e-5);
}
