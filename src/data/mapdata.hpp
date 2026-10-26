#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace tto {

using V3 = std::array<float, 3>;

// Map layout recovered from the original map builders (data/maps.json,
// docs/re/MAPS.md). World space: GLScene Y-up, metres, right-hand traffic.
struct MapNode {
    V3 pos{};
    std::array<int, 4> nb{-1, -1, -1, -1};  // slots: -Z, +X, +Z, -X (clockwise from above)
};

struct MapRoad {  // TAsfalt
    int a = 0, b = 0;
    float width = 6, len = 0;
    bool aToB = true, bToA = true;
};

struct MapWalk {  // TKaldirim
    int a = 0, b = 0;
    float width = 2, len = 0;
    bool crossing = false;
    int road = -1;  // owning road connection id
};

struct MapLight {
    int on = 0, toward = 0;  // node ids: governs traffic arriving at `on` from `toward`
    float dist = 8.3f;       // stop line distance from `on`
    int group = 0;
    V3 pos{}, facing{};
};

struct MapLightGroup {
    struct Phase {
        bool red = false, yellow = false, green = false;
        int ms = 0;
    };
    std::vector<Phase> phases;
};

struct MapSign {
    std::string kind;        // TLevha / TLParkYasak / TLHastahane / ...
    bool onConnection = false;
    int on = 0, toward = 0;  // node id (or connection id when onConnection)
    float dist = 0;
    int number = 0;          // "Lnn" -> nn
    V3 pos{}, facing{};
};

struct MapCrossing {  // TEtkiKesisme
    int onConn = 0, toward = 0, walk = 0;
    float dist = 6;
};

struct MapMesh {
    std::string file;  // Windows path relative to the game dir
    std::string cls;   // TGLFreeForm / TGLOXStaMesh
    V3 origin{}, x{}, y{}, z{};
    bool relative = false;  // relative to a native game object (e.g. DingDing) — skipped by static scenery
};

struct MapBox {
    V3 center{}, size{}, facing{0, 0, 1};
};

struct MapCarStart {
    int car = 0;  // vehicle type index 0..15 (car2 = player YARIS)
    int a = 0, b = 0;
    float dist = 0, speed = 0;
};

struct MapData {
    int index = 0;
    std::string name, sehirDir, loadingImage, timeKey;
    int timeDefault = 240;
    float speedLimit = 50;
    std::vector<MapNode> nodes;
    // Connection ids are shared by roads and walks (THarita+0x34 order).
    std::vector<int> connKind;  // per connection id: 0 road, 1 walk
    std::vector<int> connIndex; // index into roads or walks
    std::vector<MapRoad> roads;
    std::vector<MapWalk> walks;
    std::vector<MapLightGroup> lightGroups;
    std::vector<MapLight> lights;
    std::vector<MapSign> signs;
    std::vector<MapCrossing> crossings;
    std::vector<MapMesh> meshes;
    std::vector<MapBox> boxes;
    std::vector<MapCarStart> cars;
    std::vector<std::pair<V3, V3>> billboards;  // pos, facing

    int road_between(int a, int b) const;  // index into roads or -1
};

// Throws std::runtime_error on malformed data.
std::vector<MapData> load_maps(const std::filesystem::path& json);

}  // namespace tto
