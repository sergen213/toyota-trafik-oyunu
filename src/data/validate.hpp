#pragma once
#include <string>
#include <vector>

#include "core/fs.hpp"

namespace tto {

// Startup check of a user-supplied game folder ("Toyota Trafik Oyunu v2" install).
struct ValidationReport {
    std::vector<std::string> missing;   // required files not found
    std::vector<std::string> invalid;   // present but failed to parse
    std::string version;                // "v2", "v1" or "unknown"
    bool ok() const { return missing.empty() && invalid.empty(); }
    std::string text() const;
};

ValidationReport validate_game_dir(const GameFs& fs);

}  // namespace tto
