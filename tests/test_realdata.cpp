// Parses the user's original game files when TTO_GAME_DIR points at an installed
// "Toyota Trafik Oyunu v2" folder; skipped otherwise (game data is never in the repo).
#include <gtest/gtest.h>

#include <cstdlib>

#include "core/fs.hpp"
#include "data/sorubank.hpp"
#include "data/vdd.hpp"
#include "formats/tds.hpp"

using namespace tto;

namespace {
const char* game_dir() { return std::getenv("TTO_GAME_DIR"); }
}  // namespace

TEST(RealData, SoruBankHas71LicenceAnd79TrafficQuestions) {
    if (!game_dir()) GTEST_SKIP() << "TTO_GAME_DIR not set";
    GameFs fs(game_dir());
    const auto qs = parse_sorubank(read_file(fs.need("Data\\SoruBank.dat")));
    int e = 0, t = 0;
    for (const auto& q : qs) (q.category == "Ehliyet" ? e : t)++;
    EXPECT_EQ(e, 71);
    EXPECT_EQ(t, 79);
}

TEST(RealData, HgVddHasMapTimes) {
    if (!game_dir()) GTEST_SKIP() << "TTO_GAME_DIR not set";
    GameFs fs(game_dir());
    const auto root = parse_vdd(read_text(fs.need("HG.vdd")));
    EXPECT_TRUE(root.at("Arabalar"));
}

TEST(RealData, Every3dsUnderBolumlerParses) {
    if (!game_dir()) GTEST_SKIP() << "TTO_GAME_DIR not set";
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(game_dir())) {
        if (!e.is_regular_file() || to_lower_ascii(e.path().extension().string()) != ".3ds") continue;
        SCOPED_TRACE(e.path().string());
        const Mesh m = load_3ds(read_file(e.path()));
        EXPECT_FALSE(m.objects.empty());
        ++n;
    }
    EXPECT_GT(n, 10);
}
