// Map-specific actors of the Sim: AI spawner/culling, school bus, emergency priority,
// wrong parking, right of way, train and level crossings (docs/re/WORLD.md §2, §5.1).
#include <algorithm>
#include <cmath>

#include "game/traffic.hpp"

namespace tto {
namespace {
float approach(float cur, float target, float step) { return cur < target ? std::min(cur + step, target) : std::max(cur - step, target); }
glm::vec3 flat(glm::vec3 v) { return {v.x, 0, v.z}; }
glm::vec3 right_of(glm::vec3 f) { return glm::normalize(glm::cross(glm::normalize(flat(f)), glm::vec3(0, 1, 0))); }
}  // namespace

void Sim::park(Car& c) {
    // PlaceOnRoad wrong-parking branch (0x544cc4): kerb slot at lateral -2.7 m, or -0.5 m next to a
    // TLParkYasak sign of this approach. Code 3 can then be reported.
    const auto& rd = map_.roads[static_cast<std::size_t>(c.road)];
    const glm::vec3 A = node(c.from), B = node(c.to), f = glm::normalize(flat(B - A));
    float along = -1, lateral = -2.7f;
    if (std::uniform_int_distribution<int>(0, 99)(rng_) > 50) {
        along = rd.len * static_cast<float>(std::uniform_int_distribution<int>(1, 3)(rng_)) / 4.0f;
    } else {
        for (const auto& s : map_.signs)
            if (s.kind == "TLParkYasak" && !s.onConnection && s.on == c.to && s.toward == c.from) {
                along = std::clamp(rd.len - s.dist + 4.0f, 3.0f, rd.len - 3.0f);
                lateral = -0.5f;
            }
    }
    if (along < 0) return;  // stays armed for the next placement
    c.body->place(A + f * along + right_of(f) * lateral + glm::vec3(0, 0.72f, 0), f);
    c.lateral = lateral;
    c.parked = true;
    c.violation = 3;
    armParking_ = false;
    lastViolationRoll = wall_;
}

void Sim::spawner() {
    Car& pl = player();
    const glm::vec3 P = flat(pl.body->position());
    int active = 0;
    std::vector<Car*> pool;
    for (auto& c : cars_) {
        if (c.player || c.type == kPlayerType) continue;  // Afacan is never recycled ('y' returns to it)
        if (c.active) ++active;
        else pool.push_back(&c);
    }
    const glm::vec3 heading = glm::normalize(flat(pl.body->forward()) + glm::vec3(0, 0, 1e-4f));
    // Despawn: Otoban behind the player > 300 m; city maps out of range (stand-in for the camera-visibility cull).
    for (auto& c : cars_) {
        if (c.player || !c.active || c.type == kPlayerType) continue;
        const glm::vec3 d = flat(c.body->position()) - P;
        if (map_.index == 5 ? (glm::length(d) > 300 && glm::dot(d, heading) < 0) : (map_.index != 1 && glm::length(d) > 220)) {
            deactivate(c);
            --active;
            pool.push_back(&c);
        }
    }
    if (map_.index == 1 || pool.empty() || active >= cfg_.activeCarLimit) return;
    auto road_empty = [&](int r) {
        for (const auto& c : cars_)
            if (c.active && c.road == r) return false;
        return true;
    };
    Car& c = *pool[std::uniform_int_distribution<std::size_t>(0, pool.size() - 1)(rng_)];
    if (map_.index == 5) {
        // Otoban (0x52a90c): from t-+6 to t-+5 relative to the player's target node, cruise Random(10)+90.
        const int t = pl.to, p = pl.from, n = static_cast<int>(map_.nodes.size());
        const int a = t < p ? t - 6 : t + 6, b = t < p ? t - 5 : t + 5;
        if (a < 0 || b < 0 || a >= n || b >= n) return;
        const int r = map_.road_between(a, b);
        if (r < 0 || !road_empty(r)) return;
        activate(c);
        place(c, a, b, 0, static_cast<float>(std::uniform_int_distribution<int>(0, 9)(rng_) + 90));
        return;
    }
    // City maps (0x53c8c0): every >= 0.3 s place a pool car on a link out of the player's
    // immediate surroundings, heading in an allowed direction; cruise Random(65)+25.
    if (wall_ < lastSpawn_ + 0.3) return;
    lastSpawn_ = wall_;
    const auto ri = std::uniform_int_distribution<std::size_t>(0, map_.roads.size() - 1)(rng_);
    const auto& rd = map_.roads[ri];
    if (!rd.aToB && !rd.bToA) return;
    const bool ab = rd.aToB && (!rd.bToA || std::uniform_int_distribution<int>(0, 1)(rng_));
    const int a = ab ? rd.a : rd.b, b = ab ? rd.b : rd.a;
    const float L = c.body->params().length + 2.0f;
    const float dist = std::min(L * static_cast<float>(std::uniform_int_distribution<int>(2, 4)(rng_)) - L / 2, rd.len - 10.0f);
    if (dist < 2) return;
    const glm::vec3 A = node(a), f = glm::normalize(flat(node(b) - A));
    const glm::vec3 at = A + f * dist;
    const float dp = glm::length(flat(at) - P);
    if (dp < 60 || dp > 170) return;
    for (const auto& o : cars_)
        if (o.active && glm::length(flat(o.body->position()) - at) < 10) return;
    activate(c);
    place(c, a, b, dist, static_cast<float>(std::uniform_int_distribution<int>(0, 64)(rng_) + 25));
    if (armParking_ && c.type != 12 && c.type != 13 && c.type != 14) park(c);
}

void Sim::school_bus(Car& c, double) {
    const float r = remaining(c);
    if (!c.unloading) {
        // ~1 % per frame on a new link with > 40 m left and > 10 m free ahead (0x5434f8).
        float gap = 1e9f;
        car_ahead(c, &gap);
        if (map_.index != 5 && c.road != c.unloadRoad && r > 40 && gap > 10 && std::uniform_int_distribution<int>(0, 99)(rng_) == 50) {
            c.unloading = true;
            c.unloadRoad = c.road;
            c.unloadStopped = -1;
        }
        return;
    }
    if (c.unloadStopped < 0 && c.lateral >= 2.0f && c.body->kmh() < 1.0f) c.unloadStopped = wall_;
    if ((c.unloadStopped >= 0 && wall_ > c.unloadStopped + 8.0) || r < 8 || c.road != c.unloadRoad) {
        c.unloading = false;
        c.playerBehind = false;
    }
}

void Sim::bus_rules(Car& p) {
    // Overtaking an unloading school bus (0x542a5b): -7 once per bus, warning while alongside.
    if (p.road < 0) return;
    const glm::vec3 A = node(p.from), B = node(p.to), f = glm::normalize(flat(B - A));
    const float side = glm::dot(flat(p.body->position()) - A, right_of(f));
    const float mine = remaining(p);
    for (std::size_t i = 0; i < cars_.size(); ++i) {
        Car& bus = cars_[i];
        if (!bus.active || bus.type != 14 || !bus.unloading || bus.road != p.road || bus.to != p.to) continue;
        const float behind = mine - remaining(bus);  // > 0: the player is behind the bus
        if (behind > 0 && behind < 25 + bus.body->params().length) {
            bus.playerBehind = true;
            if (side < 0 && wall_ > p.lastBusWarn + 3.0) {
                p.lastBusWarn = wall_;
                rules_.notice("Dikkat okul taşıtı öğrenci indirirken sollanamaz!", wall_);
            }
        } else if (behind < -1 && bus.playerBehind && p.busPenalised != static_cast<int>(i)) {
            p.busPenalised = static_cast<int>(i);
            rules_.event(Rules::Event::SchoolBus, wall_);
        }
    }
}

bool Sim::right_of_way(Car& c, int* code) {
    // Simplified 0x5390d0: at an unsignalled junction yield to a higher priority (siren) car, to the
    // car from the right, to oncoming traffic when turning left, and to a car from the left already
    // inside the junction. Codes: 2 right, 3 ahead, 4 left.
    *code = 0;
    if (c.to < 0 || c.from < 0 || map_.index == 5 || map_.index == 1) return true;
    const int N = c.to;
    int degree = 0;
    for (const auto& rd : map_.roads) degree += (rd.a == N || rd.b == N) && (rd.aToB || rd.bToA);
    if (degree < 3) return true;
    for (const auto& L : map_.lights)
        if (L.on == N && L.toward == c.from) return true;
    const float r = remaining(c), kmh = c.body->kmh();
    if (r - 3.0f - 1.0f >= std::max(kmh, 3.0f)) return true;  // not yet at the junction (speed in metres)
    const int sMe = slot_of(N, c.from);
    if (sMe < 0) return true;
    auto turns_left = [&](const Car& x) {
        if (x.player) return x.signalLeft;
        if (x.route.empty()) return false;
        const int si = slot_of(N, x.from), so = slot_of(N, x.route.front());
        return si >= 0 && so >= 0 && ((so - si) % 4 + 4) % 4 == 1;
    };
    for (const auto& o : cars_) {
        if (&o == &c || !o.active || o.parked || o.to != N || o.from == c.from) continue;
        const float ro = remaining(o), ko = o.body->kmh();
        const bool inside = ro < 3.0f && ko > 1.0f;
        const bool coming = ko > 3.0f && ro - 3.0f < ko / 3.6f * 2.5f;
        if (!inside && !coming) continue;
        const int sO = slot_of(N, o.from);
        if (sO < 0) continue;
        const int rel = ((sO - sMe) % 4 + 4) % 4;  // 3 right, 2 ahead, 1 left
        const int dir = rel == 3 ? 2 : rel == 2 ? 3 : 4;
        if (o.priority > c.priority) { *code = dir; return false; }
        if (c.priority > o.priority) continue;
        if (rel == 3 || (rel == 2 && turns_left(c) && !turns_left(o)) || (rel == 1 && inside)) {
            *code = dir;
            return false;
        }
    }
    return true;
}

void Sim::update_train(double dtd) {
    const float dt = static_cast<float>(dtd);
    Train& t = train_;
    if (!t.running) {
        if (wall_ > t.lastEnd + 20.0) {  // >= 20 s after the last run (0x508afc)
            t.running = true;
            t.z = node(22).z + 208.0f;  // [G] "-208 along node17->node22", taken beyond node 22
            t.v = static_cast<float>(std::uniform_int_distribution<int>(0, 59)(rng_) + 60);
        }
    } else {
        t.z -= t.v * dt;
        if (t.z - 28 < node(2).z - 150) {
            t.running = false;
            t.lastEnd = wall_;
        }
    }
    Car& pl = player();
    int railGroup = -1;
    for (const auto& x : crossings_) railGroup = map_.lights[static_cast<std::size_t>(x.light)].group;
    for (auto& x : crossings_) {
        const auto& L = map_.lights[static_cast<std::size_t>(x.light)];
        const float zc = node(L.on).z;
        x.closed = t.running && t.z - 28 - 2 * t.v < zc && zc <= t.z + 28;
        const float before = x.boom;
        x.boom = x.closed ? approach(x.boom, 0, 60 * dt) : approach(x.boom, -70, 50 * dt);
        x.moving = x.boom != before;
        LightState& s = lightState_[static_cast<std::size_t>(x.light)];
        if (x.closed && !s.red) s.redOn = wall_;
        if (!x.closed && !s.green) s.greenOn = wall_;
        s.red = x.closed;
        s.yellow = false;
        s.green = !x.closed;
        const glm::vec3 N = node(L.on), f = glm::normalize(flat(N - node(L.toward)));
        const glm::vec3 c = N - f * 4.5f + right_of(f) * 1.5f + glm::vec3(0, 1, 0);
        if (x.closed && x.boom == 0 && !x.block) {
            x.block = phys_->add_static_box(c, {3.2f, 2.0f, 0.4f}, f);
            // A player caught in the box is put back to the start of the map (0x509ce0).
            const glm::vec3 d = pl.body->position() - c;
            if (std::abs(glm::dot(flat(d), f)) < 1.5f && std::abs(glm::dot(flat(d), right_of(f))) < 2.0f) {
                place(pl, 0, 1, 0, pl.cruise);
                rules_.notice("Bunu Tekrarlama !!!", wall_);
            }
        } else if (!x.closed && x.block) {
            phys_->remove_static(x.block);
            x.block = nullptr;
        }
    }
    // Railway pedestrian heads follow the nearest crossing.
    for (std::size_t i = 0; i < map_.lights.size(); ++i) {
        const auto& L = map_.lights[i];
        if (L.group != railGroup || std::any_of(crossings_.begin(), crossings_.end(), [&](const LevelCrossing& x) { return x.light == static_cast<int>(i); }))
            continue;
        bool closed = false;
        for (const auto& x : crossings_)
            closed |= x.closed && std::abs(node(map_.lights[static_cast<std::size_t>(x.light)].on).z - L.pos[2]) < 20;
        lightState_[i].red = closed;
        lightState_[i].green = !closed;
    }
}

void Sim::take_over(int idx) {
    if (idx < 0 || idx >= static_cast<int>(cars_.size()) || idx == playerIdx_ || !cars_[static_cast<std::size_t>(idx)].active) return;
    Car& old = player();
    old.player = false;
    old.route.clear();
    old.cruise = static_cast<float>(std::uniform_int_distribution<int>(0, 64)(rng_) + 25);
    old.cmd = old.body->kmh();
    Car& n = cars_[static_cast<std::size_t>(idx)];
    n.player = true;
    n.parked = n.unloading = false;
    n.violation = 0;
    n.route.clear();
    n.signalLeft = n.signalRight = false;
    n.body->set_kmh(n.body->kmh());  // keep rolling
    playerIdx_ = idx;
}

void Sim::take_over_ahead() {
    float gap = 0;
    const int a = car_ahead(player(), &gap);
    if (a < 0) return;
    const int t = cars_[static_cast<std::size_t>(a)].type;
    if (t == 12 || t == 13) return;
    take_over(a);
}

int Sim::pedestrian_violation(int i) {
    // 0x539c78: a pedestrian governed by a red/yellow head it is not waiting at (already on the crossing).
    if (i < 0 || i >= static_cast<int>(peds_.size())) return -1;
    const Pedestrian& p = peds_[static_cast<std::size_t>(i)];
    if (!p.active) return -1;
    const float len = glm::length(flat(node(p.to) - node(p.from)));
    const float rem = len - p.along;
    for (std::size_t k = 0; k < map_.lights.size(); ++k) {
        const auto& L = map_.lights[k];
        if (L.on != p.to || L.toward != p.from) continue;
        const auto& s = lightState_[k];
        if ((s.red || s.yellow) && rem <= L.dist - 0.5f) return static_cast<int>(k);
    }
    return -1;
}

}  // namespace tto
