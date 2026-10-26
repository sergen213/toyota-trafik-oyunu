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
    int activeAi = 0;
    for (const auto& s : map_.cars) {
        if (s.car < 0 || s.car >= 16) continue;
        Car& c = cars_[static_cast<std::size_t>(s.car)];
        if (!c.player && activeAi >= cfg_.activeCarLimit) continue;
        const CarParams& p = carParams.at(static_cast<std::size_t>(s.car));
        c.body = std::make_unique<OdeCar>(*phys_, p, glm::vec3(0, kRideHeight, 0), glm::vec3(0, 0, 1));
        // AI cruise speed = Random(50)+25 at the Talim placement (0x52d430).
        const float cruise = c.player ? s.speed : static_cast<float>(std::uniform_int_distribution<int>(0, 49)(rng_) + 25);
        place(c, s.a, s.b, s.dist, cruise);
        c.active = true;
        if (!c.player) ++activeAi;
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
    lightState_.resize(map_.lights.size());
    groupPhase_.assign(map_.lightGroups.size(), 0);
    groupStart_.assign(map_.lightGroups.size(), -1.0);
    update_lights();
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
    choose_route(c);
    const float rem = remaining(c);
    if (rem < 1.0f) advance_link(c);
    const float w = c.road >= 0 ? map_.roads[static_cast<std::size_t>(c.road)].width : 6.0f;
    // Lane function 0x545cbc (turn bias omitted where the turn class is unknown).
    c.lateral = approach(c.lateral, w / 4, 2.0f * dt);
    const float kmh = c.body->kmh();
    c.target = c.cruise;
    // Traffic lights: TEtkiIsik.Etkile 0x53b6d8.
    for (std::size_t i = 0; i < map_.lights.size(); ++i) {
        const auto& L = map_.lights[i];
        if (L.on != c.to || L.toward != c.from) continue;
        const auto& s = lightState_[i];
        if (!(s.red || s.yellow)) continue;
        const float r = remaining(c);
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
    // Following on the same road.
    float gap = 0;
    const int ahead = car_ahead(c, &gap);
    if (ahead >= 0 && gap < 8.0f) c.target = std::min(c.target, std::max(kmh / 2 - (8.0f - gap) * 4, 0.0f));
    // Steering toward a look-ahead point on the lane (next road near the end).
    const glm::vec3 P = c.body->position();
    const glm::vec3 A = node(c.from), B = node(c.to);
    const glm::vec3 f = glm::normalize(flat(B - A));
    const float look = 6.0f;
    glm::vec3 tp;
    const float r = remaining(c);
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
    if (c.target > 5) c.target = std::min(c.target, std::max(10.0f, c.target - std::abs(c.body->steer) * (c.target - 1.5f)));
    const float accel = kmh > c.target ? 50.0f : 25.0f;
    c.cmd = approach(c.cmd, c.target, accel * dt);
    c.body->set_kmh(c.cmd);
    c.body->motorRunning = true;
    c.brakeLights = kmh < 2 || (kmh > 0 && (kmh - c.target) / kmh > 0.0025f);
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
    if (r == 2) return;  // arms a wrong-parking car for the next placement; Talim never re-places cars
    float gap = 0;
    const int a = car_ahead(player(), &gap);
    if (a < 0) return;
    Car& c = cars_[static_cast<std::size_t>(a)];
    if (c.violation || c.type == 12 || c.type == 13 || c.type == 14) return;
    lastViolationRoll = wall_;
    if (r == 0) {
        c.runsRed = true;
    } else {
        litter_.push_back({c.body->position() - c.body->forward() * (c.body->params().length / 2)});
        c.violation = 2;
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
    player_rules(p);
    violation_generator();
    rules_.expire(wall_);
}

}  // namespace tto
