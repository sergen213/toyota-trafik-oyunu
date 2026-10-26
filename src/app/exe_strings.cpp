#include "app/exe_strings.hpp"

#include "core/fs.hpp"

namespace tto {
namespace {
std::uint32_t rd32(const std::vector<std::uint8_t>& d, std::size_t o) {
    if (o + 4 > d.size()) return 0;
    return d[o] | d[o + 1] << 8 | d[o + 2] << 16 | static_cast<std::uint32_t>(d[o + 3]) << 24;
}
std::uint16_t rd16(const std::vector<std::uint8_t>& d, std::size_t o) {
    if (o + 2 > d.size()) return 0;
    return static_cast<std::uint16_t>(d[o] | d[o + 1] << 8);
}
}  // namespace

ExeStrings::ExeStrings(const std::filesystem::path& exe) {
    try {
        d_ = read_file(exe);
    } catch (...) {
        return;
    }
    if (d_.size() < 0x200 || d_[0] != 'M' || d_[1] != 'Z') return;
    const std::uint32_t pe = rd32(d_, 0x3C);
    if (pe + 0xF8 > d_.size() || rd32(d_, pe) != 0x4550) return;
    const std::uint16_t nsec = rd16(d_, pe + 6), optSize = rd16(d_, pe + 20);
    base_ = rd32(d_, pe + 24 + 28);
    std::size_t s = pe + 24 + optSize;
    for (int i = 0; i < nsec; ++i, s += 40) secs_.push_back({rd32(d_, s + 12), rd32(d_, s + 8), rd32(d_, s + 20), rd32(d_, s + 16)});
    const auto t = ansi(exe_va::kLoaderTitle);
    valid_ = t && *t == "Harita Yükleniyor";
}

std::optional<std::size_t> ExeStrings::offset(std::uint32_t va) const {
    if (va < base_) return std::nullopt;
    const std::uint32_t rva = va - base_;
    for (const auto& s : secs_)
        if (rva >= s.va && rva < s.va + s.rsize) return s.raw + (rva - s.va);
    return std::nullopt;
}

std::optional<std::string> ExeStrings::ansi(std::uint32_t va) const {
    if (!secs_.size()) return std::nullopt;
    const auto o = offset(va);
    if (!o || *o < 4) return std::nullopt;
    const std::uint32_t n = rd32(d_, *o - 4);
    if (n == 0 || n > 4096 || *o + n > d_.size()) return std::nullopt;
    return cp1254_to_utf8(std::string(reinterpret_cast<const char*>(d_.data() + *o), n));
}

std::string ExeStrings::ansi_or(std::uint32_t va, std::string_view fb) const {
    if (auto s = ansi(va)) return *s;
    return std::string(fb);
}

std::string ExeStrings::short_or(std::uint32_t va, std::string_view fb) const {
    const auto o = offset(va);
    if (!valid_ || !o || *o >= d_.size()) return std::string(fb);
    const std::size_t n = d_[*o];
    if (n == 0 || *o + 1 + n > d_.size()) return std::string(fb);
    return cp1254_to_utf8(std::string(reinterpret_cast<const char*>(d_.data() + *o + 1), n));
}

std::string ExeStrings::pchar_or(std::uint32_t va, std::string_view fb) const {
    const auto o = offset(va);
    if (!valid_ || !o) return std::string(fb);
    std::size_t e = *o;
    while (e < d_.size() && d_[e] && e - *o < 4096) ++e;
    if (e == *o || e >= d_.size()) return std::string(fb);
    return cp1254_to_utf8(std::string(reinterpret_cast<const char*>(d_.data() + *o), e - *o));
}

}  // namespace tto
