#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tto {

// Reads long user-visible texts (help panels, F1/F2 help) straight from the
// user's own TrafikOyunu.exe instead of copying them into this project.
// Addresses are those of the v2 executable (docs/re/RULES.md, work strings
// table); other builds are detected and yield nullopt.
class ExeStrings {
public:
    explicit ExeStrings(const std::filesystem::path& exe);
    bool valid() const { return valid_; }
    // Delphi AnsiString literal (int32 length at va-4), returned as UTF-8.
    std::optional<std::string> ansi(std::uint32_t va) const;

private:
    std::optional<std::size_t> offset(std::uint32_t va) const;
    std::vector<std::uint8_t> d_;
    struct Sec {
        std::uint32_t va, vsize, raw, rsize;
    };
    std::vector<Sec> secs_;
    std::uint32_t base_ = 0;
    bool valid_ = false;
};

namespace exe_va {
inline constexpr std::uint32_t kLoaderTitle = 0x52d2c0;  // "Harita Yükleniyor" (build check)
inline constexpr std::uint32_t kHelpTraffic = 0x5182d4, kHelpLight = 0x518428, kHelpLitter = 0x518514,
                               kHelpPark = 0x518600, kHelpPedestrian = 0x51871c;
inline constexpr std::uint32_t kF1Help = 0x52f708;
}  // namespace exe_va

}  // namespace tto
