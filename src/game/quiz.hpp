#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "data/sorubank.hpp"

namespace tto {

// The six answer orders used by the original (docs/re/RULES.md §1.1);
// kPermutations[r][line] = index into Question::answers (0 is the correct answer).
inline constexpr std::array<std::array<int, 3>, 6> kPermutations{{
    {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
}};

struct AskedQuestion {
    const Question* q = nullptr;
    int perm = 0;  // index into kPermutations
    const std::string& line(int i) const { return q->answers[static_cast<std::size_t>(kPermutations[static_cast<std::size_t>(perm)][static_cast<std::size_t>(i)])]; }
    int correct_line() const;
};

// Licence quiz ("Sürücü Belgesi"), TGirisForm 0x51707c/0x516b64/0x517120:
// up to 10 questions drawn with Random(NE) avoiding only an immediate repeat,
// +10/-10 profile score per answer, passes as soon as `need` are correct
// (8, or 3 if the profile already passed once), fails after 10 asked.
class LicenceQuiz {
public:
    static constexpr int kAsked = 10;
    enum class State { Asking, Passed, Failed };

    LicenceQuiz(const std::vector<Question>& bank, int need, std::uint32_t seed);
    const AskedQuestion& current() const { return cur_; }
    State state() const { return state_; }
    // Click on answer line 0..2. Returns the score delta (+10 / -10).
    int answer(int line);
    // After a failure the original starts a new round on the next click
    // while the old question stays visible (quirk kept).
    void restart_round();
    int correct() const { return correct_; }
    int asked() const { return asked_; }
    int need() const { return need_; }

private:
    void next();
    std::vector<const Question*> pool_;
    std::mt19937 rng_;
    AskedQuestion cur_;
    int last_ = -1, asked_ = 0, correct_ = 0, need_;
    State state_ = State::Asking;
};

AskedQuestion shuffle_question(const Question& q, std::mt19937& rng);

}  // namespace tto
