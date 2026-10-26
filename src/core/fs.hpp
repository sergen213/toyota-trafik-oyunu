#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tto {

// Read a whole file. Throws std::runtime_error on failure.
std::vector<std::uint8_t> read_file(const std::filesystem::path& p);
std::string read_text(const std::filesystem::path& p);

// Convert CP1254 (Turkish Windows ANSI) bytes to UTF-8.
std::string cp1254_to_utf8(std::string_view s);
// Convert UTF-8 to CP1254 (unmappable code points become '?').
std::string utf8_to_cp1254(std::string_view s);

// The original data uses Windows-style, case-insensitive relative paths
// ("Arabalar\\rav4\\korna.wav"). GameFs resolves them against the install
// directory on a case-sensitive filesystem.
class GameFs {
public:
    explicit GameFs(std::filesystem::path root);
    const std::filesystem::path& root() const { return root_; }
    // Resolve a Windows-style relative path; nullopt if no such file.
    std::optional<std::filesystem::path> find(std::string_view winPath) const;
    // Like find() but throws with the path in the message.
    std::filesystem::path need(std::string_view winPath) const;

private:
    std::filesystem::path root_;
};

std::string to_lower_ascii(std::string_view s);

}  // namespace tto
