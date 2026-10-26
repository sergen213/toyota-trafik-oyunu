#pragma once
#include <filesystem>

namespace tto {

// Copies the game folder out of the user's original setup media into dest (replaced atomically):
//   - a setup folder: the CD root, Kurulum1 or Kurulum2 (its "program files/ToyotaSA/Toyota Trafik Oyunu*" tree), or
//   - the setup .rar archive of that CD.
// When both v1 (Kurulum1) and v2 (Kurulum2) trees are present, v2 is imported. Throws std::runtime_error.
void import_game(const std::filesystem::path& source, const std::filesystem::path& dest);

}  // namespace tto
