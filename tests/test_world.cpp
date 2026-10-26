// Sim-level checks of the map actors on the recovered map layouts (data/maps.json, no game files).
#include <gtest/gtest.h>

#include "data/mapdata.hpp"
#include "game/traffic.hpp"

using namespace tto;

namespace {
const std::vector<MapData>& maps() {
    static const auto m = load_maps(std::filesystem::path(TTO_SOURCE_DIR) / "data" / "maps.json");
    return m;
}
Sim make(int index, unsigned seed = 7) {
    Sim::Config c;
    c.seed = seed;
    return Sim(maps().at(static_cast<std::size_t>(index - 1)), std::vector<CarParams>(16), std::vector<glm::vec3>(16, glm::vec3(1)), c);
}
void run(Sim& s, double seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 60); ++i) s.update(1.0 / 60.0, {});
}
int active_ai(Sim& s) {
    int n = 0;
    for (const auto& c : s.cars()) n += c.active && !c.player;
    return n;
}
}  // namespace

TEST(World, TrainClosesLevelCrossingsOnSehir) {
    Sim s = make(3);
    ASSERT_TRUE(s.has_train());
    ASSERT_EQ(s.crossings().size(), 10u) << "ten TDingDing on the railway approaches";
    bool closed = false, red = false;
    for (int i = 0; i < 60 * 40 && !closed; ++i) {
        s.update(1.0 / 60.0, {});
        for (const auto& x : s.crossings())
            if (x.closed) {
                closed = true;
                red = s.lights()[static_cast<std::size_t>(x.light)].red;
            }
    }
    EXPECT_TRUE(s.train().running);
    EXPECT_TRUE(closed);
    EXPECT_TRUE(red) << "railway light turns red while the boom closes";
}

TEST(World, CitySpawnerKeepsTrafficUnderTheLimit) {
    Sim s = make(2);
    EXPECT_EQ(active_ai(s), 0) << "Kasaba starts with the player only";
    run(s, 8);
    const int n = active_ai(s);
    EXPECT_GT(n, 3);
    EXPECT_LE(n, 11) << "EtkinArabaSayisi default";
}

TEST(World, OtobanSpawnsOncomingTraffic) {
    Sim s = make(5);
    run(s, 3);
    EXPECT_GT(active_ai(s), 0);
}

TEST(World, EmergencyVehiclesUseSirenAndPriority) {
    Sim s = make(4, 11);
    for (int t = 0; t < 60 && !std::any_of(s.cars().begin(), s.cars().end(), [](const Car& c) { return c.active && (c.type == 12 || c.type == 13); }); ++t)
        run(s, 1);
    for (const auto& c : s.cars())
        if (c.active && (c.type == 12 || c.type == 13)) {
            EXPECT_TRUE(c.siren);
            EXPECT_EQ(c.priority, c.type == 12 ? 101 : 100);
        }
}

TEST(World, TalimHasNoDynamicTraffic) {
    Sim s = make(1);
    const int n = active_ai(s);
    run(s, 5);
    EXPECT_EQ(active_ai(s), n) << "Talim never re-places cars (Canlilar+0x6c = 0)";
    EXPECT_FALSE(s.has_train());
}

TEST(World, TakeOverMovesControlAndBack) {
    Sim s = make(1);
    int other = -1;
    for (std::size_t i = 0; i < s.cars().size(); ++i)
        if (s.cars()[i].active && !s.cars()[i].player) other = static_cast<int>(i);
    ASSERT_GE(other, 0);
    s.take_over(other);
    EXPECT_EQ(&s.player(), &s.cars()[static_cast<std::size_t>(other)]);
    s.take_over(kPlayerType - 1);
    EXPECT_EQ(s.player().type, kPlayerType);
}
