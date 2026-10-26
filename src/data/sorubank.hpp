#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tto {

// Data/SoruBank.dat: 1310-byte packed Delphi records (see docs/ASSET_FORMATS.md).
struct Question {
    int id = 0;
    std::string text;                    // UTF-8
    std::array<std::string, 3> answers;  // answers[0] is the correct one
    std::string category;                // "Ehliyet" or "Trafik"
    int group = 0;                       // byte at +0x419
};

inline constexpr std::size_t kQuestionRecordSize = 1310;

// Throws std::runtime_error on size/length violations.
std::vector<Question> parse_sorubank(std::span<const std::uint8_t> data);

}  // namespace tto
