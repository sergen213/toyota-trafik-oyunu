#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

#include "core/fs.hpp"
#include "formats/dfm.hpp"

using namespace tto;

namespace {
std::vector<std::uint8_t> form() {
    std::vector<std::uint8_t> d = {'T', 'P', 'F', '0'};
    auto s = [&](const char* t) { d.push_back(static_cast<std::uint8_t>(std::strlen(t))); d.insert(d.end(), t, t + std::strlen(t)); };
    s("TForm1"); s("Form1");
    s("Left"); d.push_back(4); d.insert(d.end(), {0x2C, 0x01, 0, 0});           // vaInt32 300
    s("Caption"); d.push_back(6); s("S\xFCr\xFC" "c\xFC");                       // vaString CP1254 "Sürücü"
    s("Glyph.Data"); d.push_back(10); d.insert(d.end(), {8, 0, 0, 0, 4, 0, 0, 0, 'B', 'M', 1, 2});  // vaBinary
    s("Font.Style"); d.push_back(11); s("fsBold"); s("");                         // vaSet
    s("Visible"); d.push_back(8);                                                  // vaFalse
    d.push_back(0);
    s("TButton"); s("Button1"); s("Tag"); d.push_back(2); d.push_back(3); d.push_back(0); d.push_back(0);
    d.push_back(0);
    return d;
}
}  // namespace

TEST(Dfm, ParsesObjectsPropsAndImages) {
    const auto o = parse_dfm(form());
    EXPECT_EQ(o.cls, "TForm1");
    EXPECT_EQ(o.num("Left"), 300);
    EXPECT_EQ(o.text("Caption"), "Sürücü");
    EXPECT_EQ(o.text("Font.Style"), "fsBold");
    EXPECT_EQ(o.num("Visible", 1), 0);
    const auto img = o.image("Glyph.Data");
    ASSERT_EQ(img.size(), 4u);
    EXPECT_EQ(img[0], 'B');
    const auto* b = o.find("Button1");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->num("Tag"), 3);
}

TEST(Dfm, EveryTruncationThrows) {
    const auto d = form();
    for (std::size_t n = 0; n + 1 < d.size(); ++n)
        EXPECT_THROW(parse_dfm(std::span(d.data(), n)), std::runtime_error) << n;
    std::vector<std::uint8_t> bad = {'M', 'Z', 0, 0};
    EXPECT_THROW(pe_rcdata(bad), std::runtime_error);
}

TEST(Dfm, RealExecutableForms) {
    const char* g = std::getenv("TTO_GAME_DIR");
    if (!g) GTEST_SKIP() << "TTO_GAME_DIR not set";
    GameFs fs(g);
    const auto res = pe_rcdata(read_file(fs.need("TrafikOyunu.exe")));
    ASSERT_TRUE(res.contains("TGIRISFORM"));
    const auto f = parse_dfm(res.at("TGIRISFORM"));
    const auto* b = f.find("SpeedButton10");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->text("Caption"), "Tamam");
    EXPECT_FALSE(b->image("Glyph.Data").empty());
    ASSERT_TRUE(res.contains("TFORMOYUN"));
    EXPECT_NO_THROW(parse_dfm(res.at("TFORMOYUN")));
}
