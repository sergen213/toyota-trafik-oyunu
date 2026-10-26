#pragma once
#include <ode/ode.h>

#include <array>
#include <functional>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace tto {

// ODE world configured like the original TGLOXOdeEngine (docs/re/DRIVING.md §3):
// gravity (0,-10,0), QuickStep 20 iterations, StepTime 0.05 s, MultiplyTime 7,
// one step per 0.1 accumulated, frame dt clamped to 0.04 s.
class Physics {
public:
    static constexpr double kStep = 0.05, kMultiply = 7.0, kAccStep = 0.1, kMaxDt = 0.04;

    Physics();
    ~Physics();
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    dWorldID world() const { return world_; }
    dSpaceID space() const { return space_; }

    dGeomID add_static_box(glm::vec3 center, glm::vec3 size, glm::vec3 facing);
    void remove_static(dGeomID g);  // e.g. a level-crossing blocking box
    void add_static_trimesh(std::vector<glm::vec3> verts, std::vector<int> indices);
    // Advance by one real frame; calls before_step() before every ODE step.
    void advance(double realDt, const std::function<void()>& beforeStep);
    void set_paused(bool p) { paused_ = p; }

    // Collision callback for dynamic-dynamic contacts (CarpSesi): bodies and |v1-v2|.
    std::function<void(dBodyID, dBodyID, float)> onDynamicContact;
    // Geoms registered as belonging to a car use the car surface (Mu 30, soft ERP 0.5).
    void mark_car_geom(dGeomID g) { carGeoms_.push_back(g); }
    void unmark_car_geoms(const std::vector<dGeomID>& gs);

private:
    static void near_cb(void* data, dGeomID a, dGeomID b);
    bool is_car(dGeomID g) const;
    dWorldID world_ = nullptr;
    dSpaceID space_ = nullptr;
    dJointGroupID contacts_ = nullptr;
    double acc_ = 0;
    bool paused_ = false;
    std::vector<dGeomID> statics_, carGeoms_;
    struct Tri {
        dTriMeshDataID data;
        std::vector<float> v;
        std::vector<dTriIndex> i;
    };
    std::vector<std::unique_ptr<Tri>> tris_;
};

// Port of oxGLODE TGLOXDynCar as configured by TMotorlu.Create (0x540a64):
// box chassis W×H×L (0.95×0.7×2.8, mass 5), four sphere wheels (mass 1) on
// Hinge2 joints; front pair steered by a P-controller, all four driven.
// Local frame: X right, Y up, Z forward.
struct CarParams {
    float length = 2.8f, width = 0.95f, height = 0.7f, radius = 0.25f;
    glm::vec3 wheelA{0.1f, -0.05f, -0.4f}, wheelC{0.1f, -0.05f, 0.5f};
    bool frontTraction = false;  // ModeTraction mdFront (13_albay)
};

class OdeCar {
public:
    static constexpr float kMassChassis = 5, kMassWheel = 1, kRunForce = 50, kAltForce = 10, kFMax = 100,
                           kFrontLimit = 2, kTurnGain = 5, kBackUp = 0.25f, kSuspErp = 0.5f, kSuspCfm = 0.03f,
                           kFrontLoHi = 3;
    OdeCar(Physics& ph, const CarParams& p, glm::vec3 pos, glm::vec3 forward);
    ~OdeCar();
    OdeCar(const OdeCar&) = delete;
    OdeCar& operator=(const OdeCar&) = delete;

    void place(glm::vec3 pos, glm::vec3 forward);  // reset velocities
    void update_joints();                          // once per ODE step (0x4e06cc)
    float kmh() const;                             // |v|*3.6*3 (0x4e44b8)
    void set_kmh(float v);                         // Speed = -v/R/3.6/3 (0x4e451c)
    glm::vec3 position() const;
    glm::quat rotation() const;
    glm::vec3 forward() const;
    glm::vec3 velocity() const;
    glm::mat4 transform() const;
    glm::mat4 wheel_transform(int i) const;
    void set_enabled(bool on);
    bool owns(dBodyID b) const;
    const CarParams& params() const { return p_; }

    float steer = 0;   // -1..1 (+ = right)
    float speed = 0;   // wheel angular velocity target; negative = forward
    bool motorRunning = false;

private:
    Physics& ph_;
    CarParams p_;
    dBodyID chassis_ = nullptr;
    std::array<dBodyID, 4> wheels_{};
    std::array<dJointID, 4> joints_{};
    std::vector<dGeomID> geoms_;
    std::array<glm::vec3, 4> wheelLocal_{};
};

struct DriveInput {
    bool up = false, down = false, left = false, right = false, brake = false, handbrake = false;
    // Joystick mode ('j'): axes in percent -100..100 (TJoystick), Y negative = forward.
    bool joystick = false;
    float joyX = 0, joyY = 0;
};

// TMotorlu.PlayerInput (0x541410), keyboard mode. dt = real frame seconds.
void apply_player_input(OdeCar& car, const DriveInput& in, double dt, bool frozen);

}  // namespace tto
