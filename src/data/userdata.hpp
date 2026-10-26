#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace tto {

// Data\UserData.dat: Delphi `file of TUser`, 271-byte packed records
// (docs/re/RULES.md §2). Strings are kept as CP1254 bytes so unused trailing
// bytes and the original encoding survive a load/save round trip.
struct Profile {
    static constexpr std::size_t kSize = 271;
    std::array<std::uint8_t, kSize> raw{};

    int id() const;
    void set_id(int v);
    std::string name() const;     // UTF-8
    std::string surname() const;  // UTF-8
    std::string city() const;     // UTF-8
    void set_name(std::string_view utf8);
    void set_surname(std::string_view utf8);
    void set_city(std::string_view utf8);
    int age() const;
    void set_age(int v);
    int score() const;
    void set_score(int v);
    bool licence() const { return raw[0xA5] != 0; }
    void set_licence(bool v) { raw[0xA5] = v ? 1 : 0; }
    int unlocked() const;
    void set_unlocked(int v);
    std::string key() const;  // ASCII
    void set_key(std::string_view k);
};

std::vector<Profile> parse_profiles(std::span<const std::uint8_t> data);  // throws on bad size
std::vector<std::uint8_t> serialize_profiles(const std::vector<Profile>& ps);

// "anahtar" key: IntToHex(T,4) + IntToHex((digitsum(T)*7+17) mod 16, 1).
std::string score_key(int total);

// Save rule from 0x534760: a new profile (id -1) is appended with id = count+1;
// an existing one replaces its record, and its key is replaced by `currentKey`
// when empty or when the stored key's 4-hex total is lower than `sessionTotal`.
void save_profile(std::vector<Profile>& all, Profile& p, const std::string& currentKey, int sessionTotal);

Profile new_profile(std::string_view name, std::string_view surname, int age, std::string_view city);

}  // namespace tto
