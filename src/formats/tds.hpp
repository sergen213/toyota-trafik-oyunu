#pragma once
#include <cstdint>
#include <span>

#include "formats/mesh.hpp"

namespace tto {

// Autodesk 3D Studio (.3DS) chunk file: materials (name, diffuse, texture map),
// triangle meshes (vertices, faces, per-face material groups, UVs, smoothing).
// Vertices are kept exactly as stored (3DS world space, Z up). Normals are
// generated per smoothing group. Throws std::runtime_error on malformed input.
Mesh load_3ds(std::span<const std::uint8_t> data);

}  // namespace tto
