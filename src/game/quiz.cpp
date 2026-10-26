#include "game/quiz.hpp"

#include <stdexcept>

namespace tto {

int AskedQuestion::correct_line() const {
    for (int i = 0; i < 3; ++i)
        if (kPermutations[static_cast<std::size_t>(perm)][static_cast<std::size_t>(i)] == 0) return i;
    return 0;
}

AskedQuestion shuffle_question(const Question& q, std::mt19937& rng) {
    return {&q, static_cast<int>(std::uniform_int_distribution<int>(0, 5)(rng))};
}

LicenceQuiz::LicenceQuiz(const std::vector<Question>& bank, int need, std::uint32_t seed) : rng_(seed), need_(need) {
    // The original counts records in file order until the first non-'Ehliyet' one.
    for (const auto& q : bank) {
        if (q.category != "Ehliyet") break;
        pool_.push_back(&q);
    }
    if (pool_.size() < 2) throw std::runtime_error("question bank has no licence questions");
    next();
}

void LicenceQuiz::next() {
    int i;
    do {
        i = std::uniform_int_distribution<int>(0, static_cast<int>(pool_.size()) - 1)(rng_);
    } while (i == last_);
    last_ = i;
    ++asked_;
    cur_ = shuffle_question(*pool_[static_cast<std::size_t>(i)], rng_);
}

int LicenceQuiz::answer(int line) {
    if (state_ != State::Asking) return 0;
    const bool ok = line == cur_.correct_line();
    if (ok) ++correct_;
    if (ok && correct_ >= need_) state_ = State::Passed;
    else if (asked_ >= kAsked) state_ = State::Failed;
    else next();
    return ok ? 10 : -10;
}

void LicenceQuiz::restart_round() {
    // Original quirk: counters reset but the failed round's last question stays
    // on screen and is answerable without counting as asked.
    asked_ = 0;
    correct_ = 0;
    state_ = State::Asking;
}

}  // namespace tto
