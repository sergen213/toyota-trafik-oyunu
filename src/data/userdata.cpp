#include "data/userdata.hpp"

#include <cstring>
#include <format>
#include <stdexcept>

#include "core/fs.hpp"

namespace tto {
namespace {

int get32(const std::array<std::uint8_t, Profile::kSize>& r, std::size_t o) {
    return static_cast<int>(r[o] | r[o + 1] << 8 | r[o + 2] << 16 | static_cast<std::uint32_t>(r[o + 3]) << 24);
}
void put32(std::array<std::uint8_t, Profile::kSize>& r, std::size_t o, int v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; ++i) r[o + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(u >> (8 * i));
}
std::string getstr(const std::array<std::uint8_t, Profile::kSize>& r, std::size_t o, std::size_t cap) {
    const std::size_t n = std::min<std::size_t>(r[o], cap);
    return std::string(reinterpret_cast<const char*>(r.data() + o + 1), n);
}
void putstr(std::array<std::uint8_t, Profile::kSize>& r, std::size_t o, std::size_t cap, std::string_view cp) {
    const std::size_t n = std::min(cp.size(), cap);
    r[o] = static_cast<std::uint8_t>(n);
    std::memcpy(r.data() + o + 1, cp.data(), n);
}

}  // namespace

int Profile::id() const { return get32(raw, 0x00); }
void Profile::set_id(int v) { put32(raw, 0x00, v); }
std::string Profile::name() const { return cp1254_to_utf8(getstr(raw, 0x04, 50)); }
std::string Profile::surname() const { return cp1254_to_utf8(getstr(raw, 0x37, 50)); }
std::string Profile::city() const { return cp1254_to_utf8(getstr(raw, 0x6E, 50)); }
void Profile::set_name(std::string_view s) { putstr(raw, 0x04, 50, utf8_to_cp1254(s)); }
void Profile::set_surname(std::string_view s) { putstr(raw, 0x37, 50, utf8_to_cp1254(s)); }
void Profile::set_city(std::string_view s) { putstr(raw, 0x6E, 50, utf8_to_cp1254(s)); }
int Profile::age() const { return get32(raw, 0x6A); }
void Profile::set_age(int v) { put32(raw, 0x6A, v); }
int Profile::score() const { return get32(raw, 0xA1); }
void Profile::set_score(int v) { put32(raw, 0xA1, v); }
int Profile::unlocked() const { return get32(raw, 0xA6); }
void Profile::set_unlocked(int v) { put32(raw, 0xA6, v); }
std::string Profile::key() const { return getstr(raw, 0xAA, 100); }
void Profile::set_key(std::string_view k) { putstr(raw, 0xAA, 100, k); }

std::vector<Profile> parse_profiles(std::span<const std::uint8_t> d) {
    if (d.size() % Profile::kSize)
        throw std::runtime_error("UserData.dat: size " + std::to_string(d.size()) + " is not a multiple of 271");
    std::vector<Profile> out(d.size() / Profile::kSize);
    for (std::size_t i = 0; i < out.size(); ++i)
        std::memcpy(out[i].raw.data(), d.data() + i * Profile::kSize, Profile::kSize);
    return out;
}

std::vector<std::uint8_t> serialize_profiles(const std::vector<Profile>& ps) {
    std::vector<std::uint8_t> d;
    d.reserve(ps.size() * Profile::kSize);
    for (const auto& p : ps) d.insert(d.end(), p.raw.begin(), p.raw.end());
    return d;
}

std::string score_key(int total) {
    int digits = 0;
    for (int t = total < 0 ? -total : total; t; t /= 10) digits += t % 10;
    const int check = (digits * 7 + 17) % 16;
    return std::format("{:04X}{:X}", static_cast<unsigned>(total) & 0xFFFF, check);
}

void save_profile(std::vector<Profile>& all, Profile& p, const std::string& currentKey, int sessionTotal) {
    if (p.name().empty()) return;
    if (p.id() == -1) {
        p.set_id(static_cast<int>(all.size()) + 1);
        all.push_back(p);  // the original writes new records without a key
        return;
    }
    const std::string k = p.key();
    int stored = -1;
    if (k.size() >= 4) {
        try {
            stored = std::stoi(k.substr(0, 4), nullptr, 16);
        } catch (...) {
            stored = -1;
        }
    }
    if (k.empty() || stored < sessionTotal) p.set_key(currentKey);
    const auto idx = static_cast<std::size_t>(p.id() - 1);
    if (idx >= all.size()) all.resize(idx + 1);
    all[idx] = p;
}

Profile new_profile(std::string_view name, std::string_view surname, int age, std::string_view city) {
    Profile p;
    p.set_id(-1);
    p.set_name(name);
    p.set_surname(surname);
    p.set_age(age);
    p.set_city(city);
    p.set_score(0);
    p.set_licence(false);
    p.set_unlocked(1);
    return p;
}

}  // namespace tto
