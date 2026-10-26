#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tto {

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;

struct MeshMaterial {
    std::string name;
    std::string texture;  // file name as stored in the source file (may be empty)
    Vec3 diffuse{0.8f, 0.8f, 0.8f};
    float transparency = 0.0f;  // 0 = opaque
    bool twoSided = false;
};

// Triangle soup grouped by material. Positions are in the source file's units
// and axes; callers apply the engine transform.
struct MeshGroup {
    int material = -1;  // index into Mesh::materials, -1 = none
    std::vector<std::uint32_t> indices;
};

struct MeshObject {
    std::string name;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;  // per-vertex, same size as positions
    std::vector<Vec2> uvs;      // empty or same size as positions
    std::vector<MeshGroup> groups;
};

struct Mesh {
    std::vector<MeshMaterial> materials;
    std::vector<MeshObject> objects;
    int material_index(const std::string& name) const;
};

}  // namespace tto
