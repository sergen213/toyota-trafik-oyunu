#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tto {

// Delphi binary form resources (TPF0 streams in RT_RCDATA of the original
// TrafikOyunu.exe). The menu layout, captions and button glyphs are read from
// the user's own executable at runtime instead of being copied into this project.
struct DfmValue {
    // int64 (ints, bools as 0/1), double, string (UTF-8; also identifiers and sets
    // as "a,b"), binary.
    std::variant<std::int64_t, double, std::string, std::vector<std::uint8_t>> v;
    std::optional<std::int64_t> integer() const;
    const std::string* str() const { return std::get_if<std::string>(&v); }
    const std::vector<std::uint8_t>* bin() const { return std::get_if<std::vector<std::uint8_t>>(&v); }
};

struct DfmObject {
    std::string cls, name;
    std::map<std::string, DfmValue> props;
    std::vector<DfmObject> children;

    const DfmObject* find(std::string_view name) const;  // depth-first, by object name
    int num(std::string_view prop, int fallback = 0) const;
    std::string text(std::string_view prop) const;
    // Glyph.Data / Picture.Data payload with Delphi's size/class prefix removed:
    // a BMP ("BM...") or JPEG ("\xFF\xD8") file image, or empty.
    std::span<const std::uint8_t> image(std::string_view prop) const;
};

// Throws std::runtime_error on malformed/truncated input.
DfmObject parse_dfm(std::span<const std::uint8_t> data);

// RT_RCDATA resources of a PE file by name ("TGIRISFORM"). Throws on malformed PE.
std::map<std::string, std::vector<std::uint8_t>> pe_rcdata(std::span<const std::uint8_t> pe);

}  // namespace tto
