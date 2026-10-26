#include <gtest/gtest.h>

#include "data/userdata.hpp"

using namespace tto;

TEST(UserData, NewProfileRoundTripsThroughBytes) {
    auto p = new_profile("Çağrı", "Işık", 12, "İstanbul");
    EXPECT_EQ(p.id(), -1);
    std::vector<Profile> all;
    save_profile(all, p, "", 0);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].id(), 1);
    const auto bytes = serialize_profiles(all);
    ASSERT_EQ(bytes.size(), Profile::kSize);
    const auto back = parse_profiles(bytes);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].name(), "Çağrı");
    EXPECT_EQ(back[0].surname(), "Işık");
    EXPECT_EQ(back[0].city(), "İstanbul");
    EXPECT_EQ(back[0].age(), 12);
    EXPECT_EQ(back[0].unlocked(), 1);
    EXPECT_FALSE(back[0].licence());
}

TEST(UserData, RejectsTruncatedFile) {
    std::vector<std::uint8_t> d(Profile::kSize + 5);
    EXPECT_THROW(parse_profiles(d), std::runtime_error);
    EXPECT_TRUE(parse_profiles({}).empty());
}

TEST(UserData, ScoreKeyFormat) {
    // total 150: digit sum 6 -> (6*7+17) mod 16 = 59 mod 16 = 11 -> 'B'
    EXPECT_EQ(score_key(150), "0096B");
    EXPECT_EQ(score_key(0).size(), 5u);
}
