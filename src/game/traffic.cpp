#include "game/traffic.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>

namespace tto {

const VehicleType kVehicleTypes[16] = {
    {"Rav4", "Gezgin"},       {"CAMRY", "Bilgin"},          {"YARIS", "Afacan"},     {"SEDAN", "Dost"},
    {"01_minnos", "Minnoş"},  {"02_osman", "Haylaz"},       {"03_kaptan", "Kaptan"}, {"04_katil", "Katil"},
    {"05_mba", "Düzenli Amca"}, {"06_neseli", "Neşeli"},    {"07_yorgun", "Yorgun"}, {"12_ambulans", "Ambulans"},
    {"Polis", "Polis"},       {"OkulTasiti", "Okul Taşıtı"}, {"13_albay", "Albay"},  {"CiftTeker", "Çift Teker"},
};

namespace {

constexpr float kStopLine = 8.3f;        // TEtkiIsik distance (0x4104cccd)
constexpr double kViolationEvery = 15.0;  // Canlilar.Progress 0x53d294
constexpr float kRideHeight = 0.72f;

float approach(float cur, float target, float step) {
    if (cur < target) return std::min(cur + step, target);
    return std::max(cur - step, target);
}

glm::vec3 flat(glm::vec3 v) { return {v.x, 0, v.z}; }
glm::vec3 right_of(glm::vec3 f) { return glm::normalize(glm::cross(glm::normalize(flat(f)), glm::vec3(0, 1, 0))); }

}  // namespace

Sim::Sim(const MapData& map, const std::vector<CarParams>& carParams, const std::vector<glm::vec3>& carColors, Config cfg)
    : map_(map), cfg_(cfg), phys_(std::make_unique<Physics>()), rng_(cfg.seed) {
    for (const auto& b : map_.boxes)
        phys_->add_static_box({b.center[0], b.center[1], b.center[2]}, {b.size[0], b.size[1], b.size[2]},
                              {b.facing[0], b.facing[1], b.facing[2]});
    cars_.resize(16);
    for (int i = 0; i < 16; ++i) {
        Car& c = cars_[static_cast<std::size_t>(i)];
        c.type = i + 1;
        c.player = c.type == kPlayerType;
        c.color = carColors.size() > static_cast<std::size_t>(i) ? carColors[static_cast<std::size_t>(i)] : glm::vec3(0.7f);
    }
    playerIdx_ = kPlayerType - 1;
    // All 16 TAraba objects exist for the whole map (factory 0x4fe0f0); idle ones are parked far away.
    for (int i = 0; i < 16; ++i) {
        Car& c = cars_[static_cast<std::size_t>(i)];
        c.body = std::make_unique<OdeCar>(*phys_, carParams.at(static_cast<std::size_t>(i)),
                                          glm::vec3(static_cast<float>(i) * 20.0f, -500.0f, -3000.0f), glm::vec3(0, 0, 1));
        c.body->set_enabled(false);
    }
    int activeAi = 0;
    for (const auto& s : map_.cars) {
        if (s.car < 0 || s.car >= 16) continue;
        Car& c = cars_[static_cast<std::size_t>(s.car)];
        if (!c.player && activeAi >= cfg_.activeCarLimit) continue;
        // AI cruise speed = Random(50)+25 at the Talim placement (0x52d430).
        const float cruise = c.player ? s.speed : static_cast<float>(std::uniform_int_distribution<int>(0, 49)(rng_) + 25);
        activate(c);
        place(c, s.a, s.b, s.dist, cruise);
        if (!c.player) ++activeAi;
    }
    // Hospital zones: the roads carrying a TLHastahane sign (flag +0x148).
    for (const auto& sg : map_.signs) {
        if (sg.kind != "TLHastahane") continue;
        int road = -1;
        if (sg.onConnection && sg.on >= 0 && sg.on < static_cast<int>(map_.connKind.size()) && map_.connKind[static_cast<std::size_t>(sg.on)] == 0)
            road = map_.connIndex[static_cast<std::size_t>(sg.on)];
        else if (!sg.onConnection)
            road = map_.road_between(sg.on, sg.toward);
        if (road >= 0) hospitalRoads_.push_back(road);
    }
    // Map 3 railway: a light group without phase strings; the train drives it (0x508afc).
    if (map_.index == 3) {
        int railGroup = -1;
        for (std::size_t g = 0; g < map_.lightGroups.size(); ++g)
            if (map_.lightGroups[g].phases.empty()) railGroup = static_cast<int>(g);
        for (std::size_t i = 0; i < map_.lights.size() && railGroup >= 0; ++i) {
            const auto& L = map_.lights[i];
            if (L.group != railGroup || map_.road_between(L.on, L.toward) < 0) continue;  // vehicle approaches only
            const auto& rd = map_.roads[static_cast<std::size_t>(map_.road_between(L.on, L.toward))];
            if (!rd.aToB && !rd.bToA) continue;
            LevelCrossing x;
            x.light = static_cast<int>(i);
            const glm::vec3 N = node(L.on), f = glm::normalize(flat(N - node(L.toward)));
            x.pos = N - f * 4.5f + right_of(f) * 3.3f;  // post on the kerb of the approach lane [G]
            x.facing = -f;
            crossings_.push_back(x);
        }
        hasTrain_ = !crossings_.empty() && map_.nodes.size() > 22;
        if (hasTrain_) train_.x = node(22).x;
    }
    phys_->onDynamicContact = [this](dBodyID a, dBodyID b, float dv) {
        Car& p = player();
        if (!p.body) return;
        const bool pa = p.body->owns(a), pb = p.body->owns(b);
        if (pa == pb) return;
        const dBodyID other = pa ? b : a;
        bool otherCar = false;
        for (const auto& c : cars_)
            if (!c.player && c.body && c.body->owns(other)) otherCar = true;
        if (!otherCar || dv < 0.5f) return;
        ++crashSounds;
        if (wall_ > lastCrashPenalty_ + 1.0) {
            lastCrashPenalty_ = wall_;
            rules_.event(Rules::Event::CarCollision, wall_);
        }
    };
    // Up to 10 active pedestrians (spawner 0x53c780); maps without sidewalks (Talim, Otoban) have none.
    if (!map_.walks.empty()) {
        peds_.resize(10);
        for (std::size_t i = 0; i < peds_.size(); ++i) {
            Pedestrian& p = peds_[i];
            p.model = static_cast<int>(i * 3 % 15);
            // Clothes tint: Insanlar-<type>-Renkler entry [idx-1], else a random entry (0x5467af).
            const auto type = static_cast<std::size_t>(p.model / 5);
            if (type < cfg_.pedColors.size() && !cfg_.pedColors[type].empty()) {
                const auto& list = cfg_.pedColors[type];
                const auto idx = static_cast<std::size_t>(p.model % 5);
                p.tint = idx < list.size() ? list[idx] : list[std::uniform_int_distribution<std::size_t>(0, list.size() - 1)(rng_)];
            }
            place_pedestrian(p);
            p.animTime = std::uniform_real_distribution<float>(0, 2)(rng_);
        }
    }
    lightState_.resize(map_.lights.size());
    groupPhase_.assign(map_.lightGroups.size(), 0);
    groupStart_.assign(map_.lightGroups.size(), -1.0);
    update_lights();
}

void Sim::activate(Car& c) {
    c.active = true;
    c.body->set_enabled(true);
    c.violation = 0;
    c.runsRed = c.parked = c.unloading = false;
    c.siren = false;
    c.priority = 0;
    c.waitingSince = -1;
    c.signalLeft = c.signalRight = false;
    // Emergency vehicles drive with the siren on (0x544644) [G: the switch-on site was not found].
    if (c.type == 12 || c.type == 13) {
        c.siren = true;
        c.priority = c.type == 12 ? 101 : 100;
    }
}

void Sim::deactivate(Car& c) {
    // vmt+0x8c 0x54484c: unlinked, ODE disabled, parked far away, back to the pool.
    c.active = false;
    c.parked = c.unloading = c.siren = false;
    c.violation = 0;
    c.body->place(glm::vec3(static_cast<float>(c.type) * 20.0f, -500.0f, -3000.0f), glm::vec3(0, 0, 1));
    c.body->set_enabled(false);
}

void Sim::place(Car& c, int a, int b, float dist, float cruise) {
    const int r = map_.road_between(a, b);
    const glm::vec3 A = node(a), B = node(b);
    const glm::vec3 f = glm::normalize(flat(B - A));
    const float w = r >= 0 ? map_.roads[static_cast<std::size_t>(r)].width : 6.0f;
    const glm::vec3 pos = A + f * dist + right_of(f) * (w / 4) + glm::vec3(0, kRideHeight, 0);
    c.body->place(pos, f);
    c.road = r;
    c.from = a;
    c.to = b;
    c.route.clear();
    c.cruise = cruise;
    c.cmd = 0;
    c.lateral = w / 4;
}

float Sim::remaining(const Car& c) const {
    if (c.to < 0 || !c.body) return 0;
    const glm::vec3 A = node(c.from), B = node(c.to), P = c.body->position();
    const glm::vec3 d = flat(B - A);
    const float len = glm::length(d);
    if (len < 1e-3f) return 0;
    const float along = glm::dot(flat(P - A), d / len);
    return std::max(0.0f, len - along);
}

int Sim::slot_of(int n, int neighbour) const {
    const auto& nb = map_.nodes[static_cast<std::size_t>(n)].nb;
    for (int k = 0; k < 4; ++k)
        if (nb[static_cast<std::size_t>(k)] == neighbour) return k;
    return -1;
}

void Sim::set_paused(bool p) {
    if (paused_ && !p) {
        // Resume (0x5081a8): AI cars get a new random cruise speed Random(65)+25.
        for (auto& c : cars_)
            if (c.active && !c.player) c.cruise = static_cast<float>(std::uniform_int_distribution<int>(0, 64)(rng_) + 25);
    }
    paused_ = p;
    phys_->set_paused(p);
}

void Sim::toggle_signal(bool left) {
    Car& c = player();
    bool& on = left ? c.signalLeft : c.signalRight;
    on = !on;
    (left ? c.signalLeftEarly : c.signalRightEarly) = on && remaining(c) >= 3.0f;
    (left ? c.signalLeftRoad : c.signalRightRoad) = on ? c.road : -1;
}

void Sim::horn() {
    Car& c = player();
    ++hornSounds;
    if (wall_ < c.lastHorn + 5.0) rules_.event(Rules::Event::HornSpam, wall_);
    c.lastHorn = wall_;
    if (c.inHospitalZone) {  // 0x547266: the flag is cleared after the penalty
        c.inHospitalZone = false;
        rules_.event(Rules::Event::HornHospital, wall_);
    }
}

void Sim::update_lights() {
    for (std::size_t g = 0; g < map_.lightGroups.size(); ++g) {
        const auto& ph = map_.lightGroups[g].phases;
        if (ph.empty()) continue;
        bool apply = false;
        if (groupStart_[g] < 0) {
            groupStart_[g] = wall_;
            apply = true;
        } else if (ph[static_cast<std::size_t>(groupPhase_[g])].ms < std::lround((wall_ - groupStart_[g]) * 1000.0)) {
            groupPhase_[g] = groupPhase_[g] + 1 >= static_cast<int>(ph.size()) ? 0 : groupPhase_[g] + 1;
            groupStart_[g] = wall_;
            apply = true;
        }
        if (!apply) continue;
        const auto& p = ph[static_cast<std::size_t>(groupPhase_[g])];
        for (std::size_t i = 0; i < map_.lights.size(); ++i) {
            if (map_.lights[i].group != static_cast<int>(g)) continue;
            LightState& s = lightState_[i];
            if (p.red && !s.red) s.redOn = wall_;
            if (p.yellow && !s.yellow) s.yellowOn = wall_;
            if (p.green && !s.green) s.greenOn = wall_;
            s.red = p.red;
            s.yellow = p.yellow;
            s.green = p.green;
        }
    }
}

int Sim::car_ahead(const Car& me, float* gap) const {
    int best = -1;
    float bestGap = 1e9f;
    const float mine = remaining(me);
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        const Car& o = cars_[i];
        if (&o == &me || !o.active || o.road != me.road || o.to != me.to) continue;
        if (o.parked && std::abs(o.lateral - me.lateral) > 1.8f) continue;  // parked at the kerb, not in my lane
        const float g = mine - remaining(o) - o.body->params().length;
        if (g > -1.0f && g < bestGap) {
            bestGap = g;
            best = static_cast<int>(i);
        }
    }
    if (gap) *gap = bestGap;
    return best;
}

void Sim::choose_route(Car& c) {
    // 0x538444/0x53833c: random legal exit at the last queued node, no U-turn,
    // no same node, respect one-way roads.
    while (c.route.size() < 3) {
        const int prev = c.route.empty() ? c.from : (c.route.size() >= 2 ? c.route[c.route.size() - 2] : c.to);
        const int cur = c.route.empty() ? c.to : c.route.back();
        std::vector<int> opts;
        for (std::size_t r = 0; r < map_.roads.size(); ++r) {
            const auto& rd = map_.roads[r];
            int other = -1;
            if (rd.a == cur && rd.aToB) other = rd.b;
            else if (rd.b == cur && rd.bToA) other = rd.a;
            if (other < 0 || other == prev || other == cur) continue;
            opts.push_back(other);
        }
        if (opts.empty()) {
            // Dead end: the original queue stops; allow the U-turn so the car keeps moving.
            opts.push_back(prev);
        }
        c.route.push_back(opts[std::uniform_int_distribution<std::size_t>(0, opts.size() - 1)(rng_)]);
    }
}

void Sim::advance_link(Car& c) {
    if (c.route.empty()) return;
    const int next = c.route.front();
    c.route.erase(c.route.begin());
    c.from = c.to;
    c.to = next;
    c.road = map_.road_between(c.from, c.to);
}

void Sim::ai_control(Car& c, double dtd) {
    const float dt = static_cast<float>(dtd);
    if (c.parked) {  // wrong-parked car: static, no lights (0x4e4a4c)
        c.body->set_kmh(0);
        c.cmd = 0;
        c.brakeLights = false;
        return;
    }
    choose_route(c);
    const float rem0 = remaining(c);
    if (rem0 < 1.0f) advance_link(c);
    if (c.road < 0 || c.to < 0) return;
    const float w = map_.roads[static_cast<std::size_t>(c.road)].width;
    const float kmh = c.body->kmh();
    c.target = c.cruise;
    const float r = remaining(c);
    // Lane function 0x545cbc with the turn bias toward the next node.
    float lane = w / 4;
    if (!c.route.empty() && map_.index != 5) {
        const int sIn = slot_of(c.to, c.from), sOut = slot_of(c.to, c.route.front());
        if (sIn >= 0 && sOut >= 0) {
            const int d = ((sOut - sIn) % 4 + 4) % 4;  // 1 = left turn, 3 = right turn (right-hand traffic)
            const float dd = std::clamp(r, 10.0f, 21.0f);
            float k = (d == 1 || d == 3) && dd < 20 ? (20 - dd) / 27 : 0;
            if (d == 1) k = -2 * k;
            lane += k;
        }
    }
    if (c.body->params().width / 2 > lane) lane = 2 * lane - c.body->params().width / 2;
    if (c.type == 14) school_bus(c, dtd);
    if (c.unloading) lane = 2.2f;
    // Overtaking a parked car on the same lane (0x5398f8, simplified): use the opposite lane.
    float gap = 0;
    const int ahead = car_ahead(c, &gap);
    const bool overtake = ahead >= 0 && cars_[static_cast<std::size_t>(ahead)].parked && gap < 18 && r > 4 && map_.index != 5;
    if (overtake) {
        if (!c.signalLeft) ++hornSounds, c.signalLeft = true;  // honk 0x5412a4 [audible only near the camera]
        lane = -w / 4;
    } else if (c.signalLeft && !c.unloading) {
        c.signalLeft = false;
    }
    c.lateral = approach(c.lateral, lane, (c.unloading ? 1.0f : 2.0f) * dt);
    // Traffic lights: TEtkiIsik.Etkile 0x53b6d8; emergency cars with the siren on ignore them.
    for (std::size_t i = 0; i < map_.lights.size() && !c.siren; ++i) {
        const auto& L = map_.lights[i];
        if (L.on != c.to || L.toward != c.from) continue;
        const auto& s = lightState_[i];
        if (!(s.red || s.yellow)) continue;
        if (c.runsRed) {
            if (r > kStopLine && r < kStopLine + 1.0f) {
                c.violation = 1;
                c.runsRed = false;
            }
            continue;
        }
        if (r > kStopLine && r < kStopLine + kmh + 1.0f) {
            const float limit = r < kStopLine + 1.0f ? 0.0f : r - kStopLine;
            c.target = std::min(c.target, std::max(limit, 0.0f));
            if (c.target < 0.01f) c.target = 0;
        }
    }
    // Following: blocked by the car ahead; stop behind an unloading school bus within 25 m (0x539bfc).
    if (ahead >= 0 && !overtake) {
        const Car& o = cars_[static_cast<std::size_t>(ahead)];
        if (o.unloading && gap < 25) c.target = std::min(c.target, std::max(gap - 4.0f, 0.0f));
        else if (gap < 8.0f) c.target = std::min(c.target, std::max(kmh / 2 - (8.0f - gap) * 4, 0.0f));
    }
    if (c.unloading) c.target = r >= 8 && c.lateral >= 2.0f ? 0.0f : std::min(c.target, 15.0f);
    // Unsignalled junctions: give way (0x5390d0); waiting more than 4 s breaks deadlocks.
    int code = 0;
    if (!right_of_way(c, &code)) {
        if (c.waitingSince < 0) c.waitingSince = wall_;
        if (wall_ - c.waitingSince < 4.0) c.target = std::min(c.target, std::max(r - 4.0f, 0.0f));
    } else {
        c.waitingSince = -1;
    }
    // Steering toward a look-ahead point on the lane (next road near the end).
    const glm::vec3 P = c.body->position();
    const glm::vec3 A = node(c.from), B = node(c.to);
    const glm::vec3 f = glm::normalize(flat(B - A));
    const float look = 6.0f;
    glm::vec3 tp;
    if (r > look || c.route.empty()) {
        tp = B - f * std::max(r - look, 0.0f) + right_of(f) * c.lateral;
    } else {
        const glm::vec3 N = node(c.route.front());
        const glm::vec3 f2 = glm::normalize(flat(N - B));
        tp = B + f2 * (look - r) + right_of(f2) * c.lateral;
    }
    const glm::vec3 fw = glm::normalize(flat(c.body->forward()));
    const glm::vec3 to = glm::normalize(flat(tp - P) + glm::vec3(1e-4f, 0, 0));
    const float ang = std::atan2(-glm::cross(fw, to).y, glm::dot(fw, to));  // + = target to the right
    c.body->steer = std::clamp(ang * 1.5f, -1.0f, 1.0f);
    // Curve slow-down (0x5429a4).
    if (c.target > 5 && !overtake) c.target = std::min(c.target, std::max(10.0f, c.target - std::abs(c.body->steer) * (c.target - 1.5f)));
    const float accel = kmh > c.target ? (map_.index == 5 ? 100.0f : 50.0f) : 25.0f;
    c.cmd = approach(c.cmd, c.target, accel * dt);
    c.body->set_kmh(c.cmd);
    c.body->motorRunning = true;
    c.brakeLights = kmh < 2 || (kmh > 0 && (kmh - c.target) / kmh > 0.0025f);
    // Turn signals within 30 m of the node, toward the next route node (0x542958/0x542874).
    if (!overtake && !c.unloading && !c.route.empty()) {
        const int sIn = slot_of(c.to, c.from), sOut = slot_of(c.to, c.route.front());
        const int d = sIn >= 0 && sOut >= 0 ? ((sOut - sIn) % 4 + 4) % 4 : 2;
        c.signalLeft = r < 30 && d == 1;
        c.signalRight = r < 30 && d == 3;
    }
}

void Sim::update_player_road(Car& c) {
    if (c.to < 0) return;
    const glm::vec3 P = flat(c.body->position());
    const glm::vec3 T = flat(node(c.to));
    if (glm::length(P - T) < 5.0f) return;  // still inside the junction
    if (remaining(c) > 0.01f) return;        // still on the current road (before the node)
    // Left the node: pick the road out of `to` best matching the heading.
    const glm::vec3 fw = glm::normalize(flat(c.body->forward()));
    int bestRoad = -1, bestOther = -1;
    float best = 0.3f;
    for (std::size_t r = 0; r < map_.roads.size(); ++r) {
        const auto& rd = map_.roads[r];
        int other = rd.a == c.to ? rd.b : rd.b == c.to ? rd.a : -1;
        if (other < 0) continue;
        const float d = glm::dot(fw, glm::normalize(flat(node(other) - T)));
        if (d > best) {
            best = d;
            bestRoad = static_cast<int>(r);
            bestOther = other;
        }
    }
    if (bestRoad < 0) return;
    const int prevFrom = c.from, node0 = c.to;
    // Signal rule (0x5450d4 / 0x545020), evaluated at junctions only.
    int degree = 0;
    for (const auto& rd : map_.roads) degree += rd.a == node0 || rd.b == node0;
    if (degree >= 3 && c.body->kmh() > 5.0f) {
        const glm::vec3 in = glm::normalize(flat(T - node(prevFrom)));
        const glm::vec3 out = glm::normalize(flat(node(bestOther) - T));
        const float cr = glm::cross(in, out).y;  // > 0: left turn (counter-clockwise from above)
        const bool straight = glm::dot(in, out) > 0.7f;
        if (straight) {
            if ((c.signalLeft && c.signalLeftRoad != c.road) || (c.signalRight && c.signalRightRoad != c.road))
                rules_.event(Rules::Event::SignalError, wall_);
        } else {
            const bool left = cr > 0;
            const bool ok = left ? (c.signalLeft && c.signalLeftEarly && !c.signalRight)
                                 : (c.signalRight && c.signalRightEarly && !c.signalLeft);
            rules_.event(ok ? Rules::Event::SignalOk : Rules::Event::SignalError, wall_);
            if (ok) c.signalLeft = c.signalRight = false;
        }
    }
    c.from = node0;
    c.to = bestOther;
    c.road = bestRoad;
    // Wrong way (0x539bb8): travelling against a one-way flag, once per road.
    const auto& rd = map_.roads[static_cast<std::size_t>(bestRoad)];
    const bool allowed = rd.a == c.from ? rd.aToB : rd.bToA;
    if (!allowed && c.wrongWayRoad != bestRoad) {
        c.wrongWayRoad = bestRoad;
        rules_.event(Rules::Event::WrongWay, wall_);
    }
}

void Sim::player_rules(Car& c) {
    const float kmh = c.body->kmh();
    const float rem = remaining(c);
    // Off-road (more than a road width from the centreline): no road rules.
    if (c.road >= 0) {
        const glm::vec3 A = flat(node(c.from)), B = flat(node(c.to)), P = flat(c.body->position());
        const glm::vec3 d = glm::normalize(B - A);
        const float off = std::abs(glm::dot(P - A, right_of(d)));
        if (off > map_.roads[static_cast<std::size_t>(c.road)].width) return;
    }
    for (std::size_t i = 0; i < map_.lights.size(); ++i) {
        const auto& L = map_.lights[i];
        if (L.on != c.to || L.toward != c.from) continue;
        const auto& s = lightState_[i];
        const int id = static_cast<int>(i);
        if ((s.red || s.yellow) && rem > kStopLine - 1.0f && rem < kStopLine && wall_ > c.lastLightPenalty + 2.0) {
            if (s.red) {
                rules_.event(Rules::Event::RedLight, wall_);
                c.lastLightPenalty = wall_;
                c.lightPenalised = id;
            } else if (s.yellow && s.yellowOn + 1.0 < wall_) {
                rules_.event(Rules::Event::YellowLight, wall_);
                c.lastLightPenalty = wall_;
                c.lightPenalised = id;
            }
        }
        if (s.green && rem < kStopLine && c.lightGreenCredited != id && c.lightPenalised != id) {
            c.lightGreenCredited = id;
            rules_.event(Rules::Event::GreenPass, wall_);
        }
    }
    // Speeding (0x5464a2): above limit + limit/10, repeats every 3 s.
    if (kmh > map_.speedLimit * 1.1f && wall_ > c.lastSpeeding + 3.0) {
        c.lastSpeeding = wall_;
        rules_.event(Rules::Event::Speeding, wall_);
    }
    // Entering a junction too fast (0x53c1c4): speed - 30 > remaining distance.
    if (kmh - 30.0f > rem && rem > 0.5f && wall_ > c.lastJunction + 4.0) {
        c.lastJunction = wall_;
        rules_.event(Rules::Event::JunctionFast, wall_);
    }
    // Right of way at unsignalled junctions (0x546367): inside the junction edge without priority, 2 s cooldown.
    int code = 0;
    if (!right_of_way(c, &code) && rem < 3.0f && kmh > 3.0f && code > 1 && wall_ > c.lastRowPenalty + 2.0) {
        c.lastRowPenalty = wall_;
        rules_.event(code == 2 ? Rules::Event::RightOfWayRight : code == 3 ? Rules::Event::RightOfWayAhead : Rules::Event::RightOfWayLeft, wall_);
    }
    // Tailgating (0x546128): distance gate not recovered; 6 m chosen.
    float gap = 0;
    if (car_ahead(c, &gap) >= 0 && gap < 6.0f && kmh > 10.0f && wall_ > c.lastTailgate + 3.0) {
        c.lastTailgate = wall_;
        rules_.event(Rules::Event::Tailgating, wall_);
    }
}

void Sim::violation_generator() {
    if (wall_ <= lastViolationRoll + kViolationEvery) return;
    const int r = std::uniform_int_distribution<int>(0, 2)(rng_);
    if (r == 2) {  // arms a wrong-parking car for the next placement; Talim never re-places cars
        if (map_.index != 5) armParking_ = true;  // Canlilar+0x30; the 15 s timer is not restarted
        return;
    }
    float gap = 0;
    const int a = car_ahead(player(), &gap);
    if (a < 0) return;
    Car& c = cars_[static_cast<std::size_t>(a)];
    if (c.violation || c.parked || c.type == 12 || c.type == 13 || c.type == 14) return;
    lastViolationRoll = wall_;
    if (r == 0) {
        c.runsRed = true;
    } else {
        litter_.push_back({c.body->position() - c.body->forward() * (c.body->params().length / 2)});
        c.violation = 2;
    }
}

void Sim::place_pedestrian(Pedestrian& p) {
    // Random sidewalk within 150 m of the player (initial fill 0x53a054 / respawn 0x53c780).
    const glm::vec3 P = cars_.empty() || !player().body ? glm::vec3(0) : flat(player().body->position());
    for (int tries = 0; tries < 200; ++tries) {
        const auto wi = std::uniform_int_distribution<std::size_t>(0, map_.walks.size() - 1)(rng_);
        const auto& w = map_.walks[wi];
        if (w.crossing) continue;
        if (glm::length(flat(node(w.a)) - P) > 70.0f && tries < 190) continue;
        p.walk = static_cast<int>(wi);
        const bool fwd = std::uniform_int_distribution<int>(0, 1)(rng_) != 0;
        p.from = fwd ? w.a : w.b;
        p.to = fwd ? w.b : w.a;
        const float len = glm::length(flat(node(w.b) - node(w.a)));
        p.along = std::uniform_real_distribution<float>(0, std::max(len - 1, 0.0f))(rng_);
        p.speed = 0;
        p.active = true;
        return;
    }
    p.active = false;
}

void Sim::update_pedestrians(double dtd) {
    const float dt = static_cast<float>(dtd);
    Car& pl = player();
    const glm::vec3 P = flat(pl.body->position());
    const float kmh = pl.body->kmh();
    for (auto& p : peds_) {
        if (!p.active) continue;
        const glm::vec3 A = node(p.from), B = node(p.to);
        const float len = std::max(glm::length(flat(B - A)), 0.01f);
        const glm::vec3 f = flat(B - A) / len;
        const auto& w = map_.walks[static_cast<std::size_t>(p.walk)];
        // Cruise 4.0 in the game's speed unit; world speeds are scaled by 3 like km/h (0x4e44b8) -> ~1.3 m/s. [G]
        float target = 4.0f / 3.0f;
        const float rem = len - p.along;
        for (std::size_t i = 0; i < map_.lights.size(); ++i) {
            const auto& L = map_.lights[i];
            if (L.on != p.to || L.toward != p.from) continue;
            const auto& s = lightState_[i];
            if ((s.red || s.yellow) && rem > L.dist - 0.5f && rem < L.dist + 1.5f) target = 0;
        }
        // Crossing: wait if a car is on the next position (0x5457a8).
        const glm::vec3 next = A + f * (p.along + 1.5f);
        if (w.crossing)
            for (const auto& c : cars_)
                if (c.active && c.body && glm::length(flat(c.body->position()) - next) < 2.0f) target = 0;
        p.speed = approach(p.speed, target, 10.0f * dt);
        p.along += p.speed * dt;
        if (p.speed > 0.05f) p.animTime += dt * p.speed / (4.0f / 3.0f);
        p.dir = f;
        p.pos = A + f * p.along + right_of(f) * 0.8f;
        // Player hits a pedestrian: -17 (0x54586c).
        if (kmh > 3.0f && glm::length(P - flat(p.pos)) < 1.3f && wall_ > p.lastHit + 3.0) {
            p.lastHit = wall_;
            rules_.event(Rules::Event::PedestrianHit, wall_);
        }
        // Road side of a crossing: "Dikkat Yayaya Öncelik Tanı" when the player approaches (0x546cfc).
        if (w.crossing && kmh > 5.0f && glm::length(P - flat(p.pos)) < kmh / 3.6f + w.len / 2 + 2 && wall_ > lastYield_ + 5.0) {
            lastYield_ = wall_;
            rules_.notice("Dikkat Yayaya Öncelik Tanı", wall_);
            if (++yieldCount_ % 5 == 0) ++pedYieldSounds;  // 0x509304
        }
        if (p.along >= len) {
            std::vector<std::size_t> opts;
            for (std::size_t i = 0; i < map_.walks.size(); ++i) {
                const auto& o = map_.walks[i];
                if (static_cast<int>(i) == p.walk) continue;
                if (o.a == p.to || o.b == p.to) opts.push_back(i);
            }
            if (opts.empty()) {
                std::swap(p.from, p.to);
            } else {
                const auto& o = map_.walks[opts[std::uniform_int_distribution<std::size_t>(0, opts.size() - 1)(rng_)]];
                p.walk = static_cast<int>(&o - map_.walks.data());
                p.from = p.to;
                p.to = o.a == p.from ? o.b : o.a;
            }
            p.along = 0;
        }
        if (glm::length(P - flat(p.pos)) > 120.0f) place_pedestrian(p);
    }
}

void Sim::update(double dt, const DriveInput& input) {
    wall_ += dt;
    if (!paused_) update_lights();
    for (auto& c : cars_) {
        if (!c.active) continue;
        if (c.player) {
            apply_player_input(*c.body, input, dt, paused_);
            const bool fwdKey = input.up, backKey = input.down;
            c.brakeLights = input.handbrake || input.brake || (fwdKey && c.body->speed > 0) || (backKey && c.body->speed < 0);
            c.reverseLights = (backKey && c.body->speed >= 0) || (fwdKey && c.body->speed > 0);
        } else if (!paused_) {
            ai_control(c, dt);
        }
    }
    phys_->advance(dt, [this] {
        for (auto& c : cars_)
            if (c.active) c.body->update_joints();
    });
    if (paused_) return;
    time_ += dt;
    Car& p = player();
    update_player_road(p);
    p.inHospitalZone = std::find(hospitalRoads_.begin(), hospitalRoads_.end(), p.road) != hospitalRoads_.end() &&
                       (p.inHospitalZone || p.road != p.prevRoad);
    p.prevRoad = p.road;
    player_rules(p);
    bus_rules(p);
    violation_generator();
    spawner();
    if (hasTrain_) update_train(dt);
    update_pedestrians(dt);
    rules_.expire(wall_);
}

}  // namespace tto
