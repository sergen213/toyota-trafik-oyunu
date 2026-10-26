#include <gtest/gtest.h>

#include <random>

#include "game/quiz.hpp"
#include "game/rules.hpp"

using namespace tto;

TEST(Rules, PenaltiesBonusesAndReportCounters) {
    Rules r;
    r.event(Rules::Event::GreenPass, 0);
    r.event(Rules::Event::RedLight, 0);
    r.event(Rules::Event::CarCollision, 0);
    r.sign_answer(true, 2, 0);
    r.violator_answer(true, Rules::RanRedLight, 0);
    EXPECT_EQ(r.score(), 2 - 10 - 15 + 40 + 100);
    EXPECT_EQ(r.green, 1);
    EXPECT_EQ(r.red, 1);
    ASSERT_EQ(r.messages().size(), 2u);  // bonuses are silent
    EXPECT_EQ(r.messages()[0].text, "Kırmızı Işıkta Geçtin (-10)");
    r.expire(Rules::kMessageSeconds + 0.1);
    EXPECT_TRUE(r.messages().empty());
    const auto rep = r.report(77);
    EXPECT_NE(rep.find("TOPLAM PUAN: 77"), std::string::npos);
    EXPECT_NE(rep.find("Hatalı Davranan Araç Yakalama: 1/1"), std::string::npos);
}

TEST(Rules, ResetKeepsSchoolBusCounterLikeOriginal) {
    Rules r;
    r.event(Rules::Event::SchoolBus, 0);
    r.event(Rules::Event::Speeding, 0);
    r.reset_attempt();
    EXPECT_EQ(r.score(), 0);
    EXPECT_EQ(r.speeding, 0);
    EXPECT_EQ(r.schoolBus, 1);
}

namespace {
std::vector<Question> bank() {
    std::vector<Question> b;
    for (int i = 0; i < 5; ++i) b.push_back({i + 1, "q" + std::to_string(i), {"right", "w1", "w2"}, "Ehliyet", 0});
    b.push_back({6, "t", {"a", "b", "c"}, "Trafik", 1});
    return b;
}
}  // namespace

TEST(Quiz, PassesAfterNeedCorrectAndFailsAfterTen) {
    const auto b = bank();
    LicenceQuiz q(b, 3, 1);
    int score = 0;
    for (int i = 0; i < 3; ++i) score += q.answer(q.current().correct_line());
    EXPECT_EQ(q.state(), LicenceQuiz::State::Passed);
    EXPECT_EQ(score, 30);

    LicenceQuiz f(b, 8, 2);
    for (int i = 0; i < 10; ++i) f.answer((f.current().correct_line() + 1) % 3);
    EXPECT_EQ(f.state(), LicenceQuiz::State::Failed);
    EXPECT_EQ(f.asked(), 10);
}

TEST(Quiz, OnlyLicenceQuestionsAndNoImmediateRepeat) {
    const auto b = bank();
    LicenceQuiz q(b, 8, 3);
    const Question* last = nullptr;
    for (int i = 0; i < 9; ++i) {
        EXPECT_EQ(q.current().q->category, "Ehliyet");
        EXPECT_NE(q.current().q, last);
        last = q.current().q;
        q.answer(q.current().correct_line() == 0 ? 1 : 0);
    }
    std::vector<Question> none{{1, "t", {"a", "b", "c"}, "Trafik", 0}};
    EXPECT_THROW(LicenceQuiz(none, 8, 1), std::runtime_error);
}
