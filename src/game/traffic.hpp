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
    // Emergency vehicles (kinds 12/13): siren flag +0x11c and priority +0xe8 (101 ambulance, 100 police).
    bool siren = false;
    int priority = 0;
    // School bus (kind 14) unloading pupils (flag 0x80000, docs/re/WORLD.md §2.8).
    bool unloading = false;
    int unloadRoad = -1, prevRoad = -1;
    double unloadStopped = -1;
    int busPenalised = -1;   // player: bus already penalised (+0x138)
    double lastBusWarn = -100;
    bool parked = false;     // wrong-parked (flag 0x10000)
    bool playerBehind = false;  // school bus: the player was behind it within 25 m while unloading
    int rowCode = 0;         // right-of-way direction code (+0xe4): 2 right, 3 ahead, 4 left
    double lastRowPenalty = -100, waitingSince = -1;
    bool inHospitalZone = false;  // +0x148, set on a TLHastahane road
};

// Pedestrian (TYaya, docs/re/WORLD.md §3): walks sidewalk/crossing links, obeys lights.
struct Pedestrian {
    int model = 0;          // 0..14: a1..a5, k1..k5, ak1..ak5
    int walk = -1;          // index into MapData::walks
    int from = -1, to = -1;
    float along = 0, speed = 0;
    bool active = false;
    double lastHit = -100;
    glm::vec3 pos{0}, dir{0, 0, 1};
    float animTime = 0;      // walk cycle position (s); frozen while standing
    int latchedLight = -1;   // +0x11c: light already reported for the code-5 question
    glm::vec3 tint{1};       // clothes colour (Insanlar-<type>-Renkler)
};

// Map 3 level crossing (TDingDing, WORLD.md §5.1): post + boom on a road approach.
struct LevelCrossing {
    int light = -1;        // governing railway light (group 4) index
    glm::vec3 pos{0}, facing{0, 0, 1};
    bool closed = false;
    float boom = -70;      // degrees: 0 closed (horizontal), -70 open
    bool moving = false;   // bell rings while the boom moves
    dGeomID block = nullptr;
};

struct Train {
    bool running = false;
    float z = 0, v = 0, x = 0;
    double lastEnd = 0;
};

struct Litter {  // TCop: thrown by a littering AI car
    glm::vec3 pos;
};

class Sim {
public:
    struct Config {
        int activeCarLimit = 11;
        unsigned seed = 1;
        std::vector<std::vector<glm::vec3>> pedColors;  // per type 1..3 (index 0..2)
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
    // Control another car ('t' car ahead / 'y' back to Afacan, TFormOyun key handler): the old car
    // continues as AI, all player rules follow the controlled car.
    void take_over(int carIndex);
    void take_over_ahead();
    // Pedestrian picked in question mode: returns the governing red light (code 5) or -1 (0x539c78).
    int pedestrian_violation(int ped);

    const MapData& map() const { return map_; }
    Physics& physics() { return *phys_; }
    std::vector<Car>& cars() { return cars_; }
    Car& player() { return cars_[static_cast<std::size_t>(playerIdx_)]; }
    const std::vector<LightState>& lights() const { return lightState_; }
    const std::vector<Litter>& litter() const { return litter_; }
    const std::vector<Pedestrian>& pedestrians() const { return peds_; }
    std::vector<Pedestrian>& pedestrians() { return peds_; }
    const std::vector<LevelCrossing>& crossings() const { return crossings_; }
    const Train& train() const { return train_; }
    bool has_train() const { return hasTrain_; }
    glm::vec3 train_pos() const { return {train_.x, 1.15f, train_.z}; }
    Rules& rules() { return rules_; }
    float remaining(const Car& c) const;  // distance to the target node (0x53b068)

    // Events for audio.
    int crashSounds = 0;   // incremented on every dynamic contact >= 0.5 with the player
    int hornSounds = 0;
    int pedYieldSounds = 0;  // every 5th "Dikkat Yayaya Öncelik Tanı" plays sesler\\yaya.wav

private:
    void place(Car& c, int a, int b, float dist, float cruise);
    void activate(Car& c);
    void deactivate(Car& c);
    void spawner();          // city maps (0x53c8c0) and Otoban (0x52a90c)
    void school_bus(Car& c, double dt);
    void bus_rules(Car& p);
    bool right_of_way(Car& c, int* code);  // 0x5390d0 (simplified right-hand rule)
    void update_train(double dt);
    void park(Car& c);
    void update_lights();
    void update_player_road(Car& c);
    void ai_control(Car& c, double dt);
    void choose_route(Car& c);
    void advance_link(Car& c);
    void player_rules(Car& c);
    void violation_generator();
    void update_pedestrians(double dt);
    void place_pedestrian(Pedestrian& p);
    double lastYield_ = -100;
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
    std::vector<Pedestrian> peds_;
    Rules rules_;
    std::mt19937 rng_;
    double time_ = 0, wall_ = 0, lastViolationRoll = -15;
    bool paused_ = false;
    double lastCrashPenalty_ = -100;
    bool armParking_ = false;
    double lastSpawn_ = -100;
    int yieldCount_ = 0;
    std::vector<LevelCrossing> crossings_;
    Train train_;
    bool hasTrain_ = false;
    std::vector<int> hospitalRoads_;
};

}  // namespace tto
