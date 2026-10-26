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
