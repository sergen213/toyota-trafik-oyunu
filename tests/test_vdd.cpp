#include <gtest/gtest.h>

#include "data/vdd.hpp"

using namespace tto;

TEST(Vdd, NestedKeysListsAndPaths) {
    const auto root = parse_vdd(
        "demo=true\r\n"
        "Arabalar=\r\n"
        "\tRav4=\r\n"
        "\t\tRadius=0.33\r\n"
        "\t\tRenkler=\r\n"
        "\t\t\t=0.416;0.398;0.380\r\n"
        "\t\t\t=0.135;0.004;0.018\r\n"
        "\tCAMRY=\r\n"
        "\t\tRadius=0,28\r\n"
        "Ayarlar=               \r\n"
        "\tHarita1Sure=240\r\n");
    EXPECT_EQ(root.text("demo"), "true");
    EXPECT_DOUBLE_EQ(*root.number("Arabalar-Rav4-Radius"), 0.33);
    EXPECT_DOUBLE_EQ(*root.number("arabalar-camry-radius"), 0.28) << "case-insensitive keys, comma decimals";
    const auto* colours = root.at("Arabalar-Rav4-Renkler");
    ASSERT_NE(colours, nullptr);
    ASSERT_EQ(colours->children.size(), 2u);
    EXPECT_EQ(colours->children[1].value, "0.135;0.004;0.018");
    EXPECT_DOUBLE_EQ(*root.number("Ayarlar-Harita1Sure"), 240);
    EXPECT_FALSE(root.number("Ayarlar-Harita9Sure"));
    EXPECT_EQ(root.text("Ayarlar-Yok", "x"), "x");
}

TEST(Vdd, RejectsIndentationJumpAndMissingEquals) {
    EXPECT_THROW(parse_vdd("a=\n\t\tb=1\n"), std::runtime_error);
    EXPECT_THROW(parse_vdd("a=1\njunk\n"), std::runtime_error);
}

TEST(Vdd, DecodesCp1254) {
    const auto root = parse_vdd("Ad=\xDD\xFE\xFD\xF0\n");  // "İşığ" in CP1254
    EXPECT_EQ(root.text("Ad"), "\xC4\xB0\xC5\x9F\xC4\xB1\xC4\x9F");
}
