#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "formats/mesh.hpp"

namespace tto {

// Valve Half-Life studiomdl text format (.smd) as read by GLScene's
// GLFileSMD: "nodes" (bones), "skeleton" (per-frame bone pos + Euler XYZ
// radians) and "triangles" (texture file name + 3 vertex lines
// "bone x y z nx ny nz u v").
struct SmdBone {
    int id = 0;
    std::string name;
    int parent = -1;
};

struct SmdBonePose {
    Vec3 pos{};
    Vec3 rot{};  // radians, applied X then Y then Z (studiomdl convention)
};

struct SmdModel {
    std::vector<SmdBone> bones;
    std::vector<std::vector<SmdBonePose>> frames;  // frames[f][boneIndex]
    // Triangles grouped by texture name; materials[i].texture = file name.
    Mesh mesh;
    std::vector<int> vertexBone;  // per vertex of mesh.objects[0] (if any)
};

// Throws std::runtime_error("SMD line N: ...") on malformed input.
SmdModel parse_smd(std::string_view text);

}  // namespace tto

namespace tto {

// Skeletal pose of a reference SMD with another SMD's animation frame (bones matched by
// name; bones missing from the animation keep the reference pose). Positions and normals
// are skinned rigidly (one bone per vertex, studiomdl). Matrices: Rz*Ry*Rx, parent first.
Mesh smd_pose(const SmdModel& ref, const SmdModel& anim, std::size_t frame);

}  // namespace tto
