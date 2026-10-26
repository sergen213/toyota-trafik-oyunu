#include "data/sorubank.hpp"

#include <stdexcept>
#include <string_view>

#include "core/fs.hpp"

namespace tto {
namespace {

std::string short_string(std::span<const std::uint8_t> rec, std::size_t off, std::size_t cap) {
    const std::size_t n = rec[off];
    if (n > cap) throw std::runtime_error("SoruBank: string length " + std::to_string(n) + " exceeds capacity");
    const char* p = reinterpret_cast<const char*>(rec.data() + off + 1);
    return cp1254_to_utf8(std::string_view(p, n));
}

}  // namespace

std::vector<Question> parse_sorubank(std::span<const std::uint8_t> data) {
    if (data.empty() || data.size() % kQuestionRecordSize)
        throw std::runtime_error("SoruBank: size " + std::to_string(data.size()) + " is not a multiple of 1310");
    std::vector<Question> out;
    for (std::size_t r = 0; r < data.size() / kQuestionRecordSize; ++r) {
        auto rec = data.subspan(r * kQuestionRecordSize, kQuestionRecordSize);
        Question q;
        q.id = static_cast<int>(rec[0] | rec[1] << 8 | rec[2] << 16 | static_cast<std::uint32_t>(rec[3]) << 24);
        q.text = short_string(rec, 0x004, 255);
        q.answers = {short_string(rec, 0x104, 255), short_string(rec, 0x204, 255), short_string(rec, 0x304, 255)};
        q.category = short_string(rec, 0x404, 20);
        q.group = rec[0x419];
        out.push_back(std::move(q));
    }
    return out;
}

}  // namespace tto
