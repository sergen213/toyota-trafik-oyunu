#include <gtest/gtest.h>

#include <cstring>

#include "data/sorubank.hpp"

using namespace tto;

namespace {
void put(std::vector<std::uint8_t>& r, std::size_t off, const char* s) {
    r[off] = static_cast<std::uint8_t>(std::strlen(s));
    std::memcpy(r.data() + off + 1, s, std::strlen(s));
}
std::vector<std::uint8_t> record(int id, const char* cat, int group) {
    std::vector<std::uint8_t> r(kQuestionRecordSize, 0);
    r[0] = static_cast<std::uint8_t>(id);
    put(r, 0x004, "Soru?");
    put(r, 0x104, "Do\xF0ru");  // CP1254 ğ
    put(r, 0x204, "Yanl\xFD\xFE");
    put(r, 0x304, "Hay\xFDr");
    put(r, 0x404, cat);
    r[0x419] = static_cast<std::uint8_t>(group);
    return r;
}
}  // namespace

TEST(SoruBank, ParsesRecordsAndKeepsCorrectAnswerFirst) {
    auto a = record(1, "Ehliyet", 1);
    auto b = record(2, "Trafik", 3);
    a.insert(a.end(), b.begin(), b.end());
    const auto qs = parse_sorubank(a);
    ASSERT_EQ(qs.size(), 2u);
    EXPECT_EQ(qs[0].id, 1);
    EXPECT_EQ(qs[0].answers[0], "Do\xC4\x9Fru");
    EXPECT_EQ(qs[0].answers[1], "Yanl\xC4\xB1\xC5\x9F");
    EXPECT_EQ(qs[1].category, "Trafik");
    EXPECT_EQ(qs[1].group, 3);
}

TEST(SoruBank, RejectsTruncatedFileAndOverlongStrings) {
    auto a = record(1, "Ehliyet", 1);
    a.pop_back();
    EXPECT_THROW(parse_sorubank(a), std::runtime_error);
    auto b = record(1, "Ehliyet", 1);
    b[0x404] = 40;  // category is string[20]
    EXPECT_THROW(parse_sorubank(b), std::runtime_error);
    EXPECT_THROW(parse_sorubank({}), std::runtime_error);
}

TEST(SoruBank, EveryTruncationThrows) {
    const auto r = record(1, "Ehliyet", 1);
    for (std::size_t n = 1; n < r.size(); n += 37) {
        std::vector<std::uint8_t> cut(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(n));
        EXPECT_THROW(parse_sorubank(cut), std::runtime_error);
    }
}
