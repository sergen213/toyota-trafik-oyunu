#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tto {

// HG.vdd: tab-indented key tree. "Key=Value" lines; a key whose following
// lines are indented one tab deeper owns them as children; "=Value" lines are
// anonymous list items (e.g. colour lists). Values are kept as CP1254->UTF-8.
struct VddNode {
    std::string key;
    std::string value;
    std::vector<VddNode> children;

    const VddNode* child(std::string_view k) const;  // case-insensitive
    // Path lookup with '-' separators as used by the game ("Ayarlar-Harita1Sure").
    const VddNode* at(std::string_view path) const;
    std::optional<double> number(std::string_view path) const;
    std::string text(std::string_view path, std::string_view fallback = {}) const;
};

// Throws std::runtime_error with the line number on malformed indentation.
VddNode parse_vdd(std::string_view cp1254Text);

}  // namespace tto
