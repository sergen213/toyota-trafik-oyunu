#pragma once
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "data/mapdata.hpp"
#include "game/rules.hpp"
#include "game/vehicle.hpp"

namespace tto {

// Vehicle types 1..16 (TAraba kind, docs/re/WORLD.md §2.1).
struct VehicleType {
    const char* key;    // HG.vdd "Arabalar-<key>" and folder name
    const char* plate;  // display name
};
extern const VehicleType kVehicleTypes[16];
inline constexpr int kPlayerType = 3;  // YARIS "Afacan"

// Traffic-light group state (TIsiklar 0x53d920). Phases advance on a clock that
// does not run while the game is paused, and at most one phase per update,
// which reproduces the original wall-clock behaviour after a pause.
struct LightState {
    bool red = false, yellow = false, green = false;
    double redOn = 0, yellowOn = 0, greenOn = 0;  // sim time the lamp switched on
};

struct Car {
    int type = 0;  // 1..16
    std::unique_ptr<OdeCar> body;
    bool active = false, player = false;
    glm::vec3 color{1, 0, 0};
    // Road-graph state (TCisim): current road, travel from -> to.
    int road = -1, from = -1, to = -1;
    std::vector<int> route;  // next nodes after `to`
    float cruise = 50, target = 0, cmd = 0, lateral = 0;
    // Rule latches / timers (seconds of sim time).
    int lightGreenCredited = -1, lightPenalised = -1;
    double lastLightPenalty = -100, lastSpeeding = -100, lastJunction = -100, lastTailgate = -100, lastHorn = -10;
    int wrongWayRoad = -1;
    bool signalLeft = false, signalRight = false;
    bool signalLeftEarly = false, signalRightEarly = false;  // switched on with >= 3 m remaining
    int signalLeftRoad = -1, signalRightRoad = -1;
    // Violations the player can report (TAraba+0xb0: 1 red light, 2 litter, 3 parking).
    int violation = 0;
    bool runsRed = false;
    bool brakeLights = false, reverseLights = false;
};

struct Litter {  // TCop: thrown by a littering AI car
    glm::vec3 pos;
};

class Sim {
public:
    struct Config {
        int activeCarLimit = 11;
        unsigned seed = 1;
    };
    Sim(const MapData& map, const std::vector<CarParams>& carParams, const std::vector<glm::vec3>& carColors, Config cfg);

    // One real frame. `input` drives the player. Returns nothing; read state.
    void update(double dt, const DriveInput& input);
    void set_paused(bool p);
    bool paused() const { return paused_; }
    void reset_clock() { time_ = 0; }
    double time() const { return time_; }  // map clock: HUD "Süre", stops while paused
    double wall() const { return wall_; }  // cadencer/wall clock: lights, cooldowns

    void toggle_signal(bool left);
    void horn();

    const MapData& map() const { return map_; }
    Physics& physics() { return *phys_; }
    std::vector<Car>& cars() { return cars_; }
    Car& player() { return cars_[static_cast<std::size_t>(playerIdx_)]; }
    const std::vector<LightState>& lights() const { return lightState_; }
    const std::vector<Litter>& litter() const { return litter_; }
    Rules& rules() { return rules_; }
    float remaining(const Car& c) const;  // distance to the target node (0x53b068)

    // Events for audio.
    int crashSounds = 0;   // incremented on every dynamic contact >= 0.5 with the player
    int hornSounds = 0;

private:
    void place(Car& c, int a, int b, float dist, float cruise);
    void update_lights();
    void update_player_road(Car& c);
    void ai_control(Car& c, double dt);
    void choose_route(Car& c);
    void advance_link(Car& c);
    void player_rules(Car& c);
    void violation_generator();
    int car_ahead(const Car& c, float* gap) const;
    glm::vec3 node(int i) const { return {map_.nodes[static_cast<std::size_t>(i)].pos[0], map_.nodes[static_cast<std::size_t>(i)].pos[1], map_.nodes[static_cast<std::size_t>(i)].pos[2]}; }
    int slot_of(int nodeIdx, int neighbour) const;

    const MapData& map_;
    Config cfg_;
    std::unique_ptr<Physics> phys_;
    std::vector<Car> cars_;
    int playerIdx_ = 2;
    std::vector<LightState> lightState_;
    std::vector<int> groupPhase_;
    std::vector<double> groupStart_;
    std::vector<Litter> litter_;
    Rules rules_;
    std::mt19937 rng_;
    double time_ = 0, wall_ = 0, lastViolationRoll = -15;
    bool paused_ = false;
    double lastCrashPenalty_ = -100;
};

}  // namespace tto
