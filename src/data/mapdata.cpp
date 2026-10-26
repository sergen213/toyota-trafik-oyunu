#include "data/mapdata.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "core/fs.hpp"

namespace tto {
namespace {

V3 v3(const nlohmann::json& j) { return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; }

int node_ref(const std::string& s, bool& conn) {
    if (s.size() < 2 || (s[0] != 'N' && s[0] != 'C')) throw std::runtime_error("maps.json: bad reference " + s);
    conn = s[0] == 'C';
    return std::stoi(s.substr(1));
}

}  // namespace

int MapData::road_between(int a, int b) const {
    for (std::size_t i = 0; i < roads.size(); ++i)
        if ((roads[i].a == a && roads[i].b == b) || (roads[i].a == b && roads[i].b == a)) return static_cast<int>(i);
    return -1;
}

std::vector<MapData> load_maps(const std::filesystem::path& path) {
    const auto j = nlohmann::json::parse(read_text(path));
    std::vector<MapData> out;
    for (const auto& m : j.at("maps")) {
        MapData d;
        d.index = m.at("index");
        d.name = m.at("name");
        d.sehirDir = m.at("sehir_dir");
        d.loadingImage = m.at("loading_image");
        d.timeKey = m.at("time_key");
        d.timeDefault = m.at("time_default_s");
        d.speedLimit = m.at("speed_limit_kmh");
        for (const auto& n : m.at("nodes")) {
            MapNode mn;
            mn.pos = {n.at("x"), n.at("y"), n.at("z")};
            for (int k = 0; k < 4; ++k) mn.nb[static_cast<std::size_t>(k)] = n.at("nb").at(k);
            if (n.at("id").get<int>() != static_cast<int>(d.nodes.size())) throw std::runtime_error("maps.json: node ids not sequential");
            d.nodes.push_back(mn);
        }
        auto setConn = [&](int id, int kind, int idx) {
            if (id < 0) throw std::runtime_error("maps.json: negative connection id");
            if (d.connKind.size() <= static_cast<std::size_t>(id)) {
                d.connKind.resize(static_cast<std::size_t>(id) + 1, -1);
                d.connIndex.resize(static_cast<std::size_t>(id) + 1, -1);
            }
            d.connKind[static_cast<std::size_t>(id)] = kind;
            d.connIndex[static_cast<std::size_t>(id)] = idx;
        };
        const auto checkNode = [&](int n) {
            if (n < 0 || n >= static_cast<int>(d.nodes.size())) throw std::runtime_error("maps.json: node index out of range");
            return n;
        };
        for (const auto& r : m.at("roads")) {
            MapRoad mr{checkNode(r.at("a")), checkNode(r.at("b")), r.at("width"), r.at("len"), r.at("a_to_b"), r.at("b_to_a")};
            setConn(r.at("id"), 0, static_cast<int>(d.roads.size()));
            d.roads.push_back(mr);
        }
        for (const auto& w : m.at("sidewalks")) {
            MapWalk mw{checkNode(w.at("a")), checkNode(w.at("b")), w.at("width"), w.at("len"), w.at("crossing"),
                       w.at("road").is_null() ? -1 : w.at("road").get<int>()};
            setConn(w.at("id"), 1, static_cast<int>(d.walks.size()));
            d.walks.push_back(mw);
        }
        for (const auto& g : m.at("light_groups")) {
            MapLightGroup lg;
            for (const std::string p : g.at("phases")) {
                if (p.size() < 5 || p[3] != ':') throw std::runtime_error("maps.json: bad light phase " + p);
                lg.phases.push_back({p[0] != '0', p[1] != '0', p[2] != '0', std::stoi(p.substr(4))});
            }
            const int gi = g.at("group");
            if (gi != static_cast<int>(d.lightGroups.size())) throw std::runtime_error("maps.json: light groups out of order");
            d.lightGroups.push_back(std::move(lg));
        }
        for (const auto& l : m.at("lights")) {
            bool conn = false;
            MapLight ml;
            ml.on = node_ref(l.at("on"), conn);
            ml.toward = l.at("toward");
            ml.dist = l.at("dist");
            ml.group = l.at("group");
            ml.pos = v3(l.at("pos"));
            ml.facing = v3(l.at("facing"));
            d.lights.push_back(ml);
        }
        for (const auto& s : m.at("signs")) {
            MapSign ms;
            ms.kind = s.at("kind");
            ms.on = node_ref(s.at("on"), ms.onConnection);
            ms.toward = s.at("toward");
            ms.dist = s.at("dist");
            const std::string tex = s.at("tex");
            ms.number = tex.size() > 1 ? std::stoi(tex.substr(1)) : 0;
            ms.pos = v3(s.at("pos"));
            ms.facing = v3(s.at("facing"));
            d.signs.push_back(ms);
        }
        for (const auto& c : m.at("crossings")) d.crossings.push_back({c.at("on_road"), c.at("toward"), c.at("walk"), c.at("dist")});
        for (const auto& me : m.at("meshes")) {
            MapMesh mm;
            mm.file = me.at("file");
            mm.cls = me.at("cls");
            mm.origin = v3(me.at("origin"));
            mm.x = v3(me.at("x_axis"));
            mm.y = v3(me.at("y_axis"));
            mm.z = v3(me.at("z_axis"));
            mm.relative = me.contains("relative_to");
            d.meshes.push_back(mm);
        }
        for (const auto& b : m.at("static_boxes"))
            if (!b.at("size").is_null())  // shortcut: 4 Şehir boxes whose size the emulator did not recover are skipped
                d.boxes.push_back({v3(b.at("center")), v3(b.at("size")), v3(b.at("facing"))});
        for (const auto& c : m.at("car_placement")) {
            const std::string car = c.at("car");
            d.cars.push_back({std::stoi(car.substr(3)), c.at("a"), c.at("b"), c.at("dist_from_a"), c.at("p5")});
        }
        for (const auto& x : m.at("misc"))
            if (x.at("kind") == "billboard") d.billboards.push_back({v3(x.at("pos")), v3(x.at("facing"))});
        out.push_back(std::move(d));
    }
    return out;
}

}  // namespace tto
