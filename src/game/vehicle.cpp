#include "game/vehicle.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <glm/gtc/matrix_transform.hpp>

namespace tto {
namespace {

struct OdeInit {
    OdeInit() { dInitODE2(0); }
    ~OdeInit() { dCloseODE(); }
};
void ensure_ode() { static OdeInit once; }

glm::mat3 basis_from_forward(glm::vec3 fwd) {
    fwd = glm::normalize(glm::vec3(fwd.x, 0, fwd.z));
    const glm::vec3 up(0, 1, 0);
    const glm::vec3 left = glm::normalize(glm::cross(up, fwd));  // car-local +X (GLScene: Up x Dir)
    return glm::mat3(left, up, fwd);
}

void set_rot(dBodyID b, const glm::mat3& m) {
    dMatrix3 R;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) R[r * 4 + c] = m[c][r];
        R[r * 4 + 3] = 0;
    }
    dBodySetRotation(b, R);
}

// oxGLODE's patched ODE had dJointCorrectHinge2 (not in upstream ODE): it re-aligns a
// rear wheel's axes with the chassis so the unsteered wheels do not drift. Same effect here.
void correct_hinge2(dJointID j, dBodyID chassis) {
    const dReal* R = dBodyGetRotation(chassis);
    const dReal up[3] = {R[1], R[5], R[9]}, axis2[3] = {R[0], R[4], R[8]};
    const dReal a1[3] = {up[0], up[1], up[2]};
    dJointSetHinge2Axes(j, a1, axis2);
}

}  // namespace

// ---------------------------------------------------------------- Physics

Physics::Physics() {
    ensure_ode();
    world_ = dWorldCreate();
    space_ = dHashSpaceCreate(nullptr);
    contacts_ = dJointGroupCreate(0);
    dWorldSetGravity(world_, 0, -10, 0);
    dWorldSetCFM(world_, 1e-5);
    dWorldSetQuickStepNumIterations(world_, 20);
}

Physics::~Physics() {
    dJointGroupDestroy(contacts_);
    dSpaceDestroy(space_);  // destroys contained geoms
    dWorldDestroy(world_);
    for (auto& t : tris_) dGeomTriMeshDataDestroy(t->data);
}

dGeomID Physics::add_static_box(glm::vec3 c, glm::vec3 s, glm::vec3 facing) {
    dGeomID g = dCreateBox(space_, std::max(s.x, 0.01f), std::max(s.y, 0.01f), std::max(s.z, 0.01f));
    dGeomSetPosition(g, c.x, c.y, c.z);
    const glm::mat3 m = basis_from_forward(glm::length(facing) > 0 ? facing : glm::vec3(0, 0, 1));
    dMatrix3 R;
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) R[r * 4 + k] = m[k][r];
        R[r * 4 + 3] = 0;
    }
    dGeomSetRotation(g, R);
    statics_.push_back(g);
    return g;
}

void Physics::remove_static(dGeomID g) {
    if (!g) return;
    std::erase(statics_, g);
    dGeomDestroy(g);
}

void Physics::add_static_trimesh(std::vector<glm::vec3> verts, std::vector<int> indices) {
    auto t = std::make_unique<Tri>();
    for (const auto& v : verts) t->v.insert(t->v.end(), {v.x, v.y, v.z});
    for (int i : indices) t->i.push_back(static_cast<dTriIndex>(i));
    t->data = dGeomTriMeshDataCreate();
    dGeomTriMeshDataBuildSingle(t->data, t->v.data(), 3 * sizeof(float), static_cast<int>(verts.size()), t->i.data(),
                                static_cast<int>(t->i.size()), 3 * sizeof(dTriIndex));
    statics_.push_back(dCreateTriMesh(space_, t->data, nullptr, nullptr, nullptr));
    tris_.push_back(std::move(t));
}

void Physics::unmark_car_geoms(const std::vector<dGeomID>& gs) {
    std::erase_if(carGeoms_, [&](dGeomID g) { return std::find(gs.begin(), gs.end(), g) != gs.end(); });
}

bool Physics::is_car(dGeomID g) const { return std::find(carGeoms_.begin(), carGeoms_.end(), g) != carGeoms_.end(); }

void Physics::near_cb(void* data, dGeomID a, dGeomID b) {
    auto* self = static_cast<Physics*>(data);
    dBodyID ba = dGeomGetBody(a), bb = dGeomGetBody(b);
    if (!ba && !bb) return;
    if (ba && bb && dAreConnectedExcluding(ba, bb, dJointTypeContact)) return;
    if (ba && bb && !dBodyIsEnabled(ba) && !dBodyIsEnabled(bb)) return;
    constexpr int kMax = 12;  // ContactNum
    dContact c[kMax];
    const int n = dCollide(a, b, kMax, &c[0].geom, sizeof(dContact));
    if (!n) return;
    const bool car = self->is_car(a) || self->is_car(b);
    for (int i = 0; i < n; ++i) {
        c[i].surface.mode = dContactSoftERP;
        c[i].surface.mu = car ? 30 : 10;
        c[i].surface.soft_erp = car ? 0.5 : 0.2;
        dJointID j = dJointCreateContact(self->world_, self->contacts_, &c[i]);
        dJointAttach(j, ba, bb);
    }
    if (ba && bb && self->onDynamicContact) {
        const dReal* va = dBodyGetLinearVel(ba);
        const dReal* vb = dBodyGetLinearVel(bb);
        const float dv = static_cast<float>(std::sqrt((va[0] - vb[0]) * (va[0] - vb[0]) + (va[1] - vb[1]) * (va[1] - vb[1]) +
                                                      (va[2] - vb[2]) * (va[2] - vb[2])));
        self->onDynamicContact(ba, bb, dv);
    }
}

void Physics::advance(double realDt, const std::function<void()>& beforeStep) {
    if (paused_) return;  // StepTime is 0 while paused (0x52fb47)
    acc_ += std::min(realDt, kMaxDt) * kMultiply;
    while (acc_ >= kAccStep) {
        acc_ -= kAccStep;
        if (beforeStep) beforeStep();
        dSpaceCollide(space_, this, &Physics::near_cb);
        dWorldQuickStep(world_, kStep);
        dJointGroupEmpty(contacts_);
    }
}

// ---------------------------------------------------------------- OdeCar

OdeCar::OdeCar(Physics& ph, const CarParams& p, glm::vec3 pos, glm::vec3 fwd) : ph_(ph), p_(p) {
    const float W = p.width, H = p.height, L = p.length, R = p.radius;
    // Wheel centres relative to the chassis centre (0x4deb58); B/D mirror X.
    wheelLocal_ = {glm::vec3(W / 2 + p.wheelA.x, -H / 2 + p.wheelA.y, L / 2 + p.wheelA.z),
                   glm::vec3(-W / 2 - p.wheelA.x, -H / 2 + p.wheelA.y, L / 2 + p.wheelA.z),
                   glm::vec3(W / 2 + p.wheelC.x, -H / 2 + p.wheelC.y, -L / 2 + p.wheelC.z),
                   glm::vec3(-W / 2 - p.wheelC.x, -H / 2 + p.wheelC.y, -L / 2 + p.wheelC.z)};
    dMass m;
    chassis_ = dBodyCreate(ph.world());
    dMassSetBox(&m, 1, W, H, L);
    dMassAdjust(&m, kMassChassis);  // dSetPoids
    dBodySetMass(chassis_, &m);
    dGeomID cg = dCreateBox(ph.space(), W, H, L);
    dGeomSetBody(cg, chassis_);
    geoms_.push_back(cg);
    for (int i = 0; i < 4; ++i) {
        wheels_[static_cast<std::size_t>(i)] = dBodyCreate(ph.world());
        dMassSetSphere(&m, 1, R);
        dMassAdjust(&m, kMassWheel);
        dBodySetMass(wheels_[static_cast<std::size_t>(i)], &m);
        dGeomID g = dCreateSphere(ph.space(), R);
        dGeomSetBody(g, wheels_[static_cast<std::size_t>(i)]);
        geoms_.push_back(g);
    }
    for (auto g : geoms_) ph.mark_car_geom(g);
    for (int i = 0; i < 4; ++i) {
        dJointID j = dJointCreateHinge2(ph.world(), nullptr);
        dJointAttach(j, chassis_, wheels_[static_cast<std::size_t>(i)]);
        joints_[static_cast<std::size_t>(i)] = j;
    }
    place(pos, fwd);
}

OdeCar::~OdeCar() {
    ph_.unmark_car_geoms(geoms_);
    for (auto j : joints_) dJointDestroy(j);
    for (auto g : geoms_) dGeomDestroy(g);
    for (auto w : wheels_) dBodyDestroy(w);
    dBodyDestroy(chassis_);
}

void OdeCar::place(glm::vec3 pos, glm::vec3 fwd) {
    const glm::mat3 B = basis_from_forward(fwd);
    auto reset = [&](dBodyID b, glm::vec3 p) {
        dBodySetPosition(b, p.x, p.y, p.z);
        set_rot(b, B);
        dBodySetLinearVel(b, 0, 0, 0);
        dBodySetAngularVel(b, 0, 0, 0);
    };
    reset(chassis_, pos);
    for (int i = 0; i < 4; ++i) reset(wheels_[static_cast<std::size_t>(i)], pos + B * wheelLocal_[static_cast<std::size_t>(i)]);
    const glm::vec3 up = B[1], right = -B[0];
    for (int i = 0; i < 4; ++i) {
        dJointID j = joints_[static_cast<std::size_t>(i)];
        const glm::vec3 a = pos + B * wheelLocal_[static_cast<std::size_t>(i)];
        dJointSetHinge2Anchor(j, a.x, a.y, a.z);
        // Axis signs chosen so that, as in the original, positive Steer turns right and
        // negative wheel velocity drives forward (0x4e451c); checked by tests/test_vehicle.cpp.
        const dReal a1[3] = {up.x, up.y, up.z}, a2[3] = {-right.x, -right.y, -right.z};
        dJointSetHinge2Axes(j, a1, a2);
        dJointSetHinge2Param(j, dParamFMax, kFMax);
        dJointSetHinge2Param(j, dParamSuspensionERP, kSuspErp);
        dJointSetHinge2Param(j, dParamSuspensionCFM, kSuspCfm);
        const float lim = i < 2 ? kFrontLoHi : 0;
        dJointSetHinge2Param(j, dParamLoStop, -lim);
        dJointSetHinge2Param(j, dParamHiStop, lim);
    }
    steer = 0;
    speed = 0;
}

void OdeCar::update_joints() {
    // oxGLODE only narrows LoStop of J0 and HiStop of J1 (quirk kept).
    dJointSetHinge2Param(joints_[0], dParamLoStop, -kFrontLimit);
    dJointSetHinge2Param(joints_[1], dParamHiStop, kFrontLimit);
    for (int i = 0; i < 2; ++i) {
        dJointID j = joints_[static_cast<std::size_t>(i)];
        dJointSetHinge2Param(j, dParamVel, (steer - dJointGetHinge2Angle1(j)) * kTurnGain);
        dJointSetHinge2Param(j, dParamFudgeFactor, kBackUp);
    }
    const int first = 0, last = p_.frontTraction ? 2 : 4;
    for (int i = first; i < last; ++i) {
        dJointID j = joints_[static_cast<std::size_t>(i)];
        dJointSetHinge2Param(j, dParamVel2, motorRunning ? speed : 0.0f);
        dJointSetHinge2Param(j, dParamFMax2, motorRunning ? kRunForce : kAltForce);
    }
    if (motorRunning) {
        correct_hinge2(joints_[2], chassis_);
        correct_hinge2(joints_[3], chassis_);
    }
}

float OdeCar::kmh() const {
    const dReal* v = dBodyGetLinearVel(chassis_);
    return static_cast<float>(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) * 3.6 * 3.0);
}

void OdeCar::set_kmh(float v) { speed = -v / p_.radius / 3.6f / 3.0f; }

glm::vec3 OdeCar::position() const {
    const dReal* p = dBodyGetPosition(chassis_);
    return {p[0], p[1], p[2]};
}

glm::vec3 OdeCar::velocity() const {
    const dReal* v = dBodyGetLinearVel(chassis_);
    return {v[0], v[1], v[2]};
}

glm::quat OdeCar::rotation() const {
    const dReal* q = dBodyGetQuaternion(chassis_);
    return glm::quat(static_cast<float>(q[0]), static_cast<float>(q[1]), static_cast<float>(q[2]), static_cast<float>(q[3]));
}

glm::vec3 OdeCar::forward() const { return glm::mat3_cast(rotation()) * glm::vec3(0, 0, 1); }

glm::mat4 OdeCar::transform() const {
    glm::mat4 m = glm::mat4_cast(rotation());
    m[3] = glm::vec4(position(), 1);
    return m;
}

glm::mat4 OdeCar::wheel_transform(int i) const {
    dBodyID w = wheels_[static_cast<std::size_t>(i)];
    const dReal* q = dBodyGetQuaternion(w);
    const dReal* p = dBodyGetPosition(w);
    glm::mat4 m = glm::mat4_cast(glm::quat(static_cast<float>(q[0]), static_cast<float>(q[1]), static_cast<float>(q[2]), static_cast<float>(q[3])));
    m[3] = glm::vec4(static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]), 1);
    return m;
}

glm::vec2 OdeCar::wheel_angles(int i) const {
    dJointID j = joints_[static_cast<std::size_t>(i)];
    return {static_cast<float>(dJointGetHinge2Angle1(j)), static_cast<float>(dJointGetHinge2Angle2(j))};
}

void OdeCar::set_enabled(bool on) {
    for (dBodyID b : {chassis_, wheels_[0], wheels_[1], wheels_[2], wheels_[3]}) on ? dBodyEnable(b) : dBodyDisable(b);
}

bool OdeCar::owns(dBodyID b) const {
    return b == chassis_ || std::find(wheels_.begin(), wheels_.end(), b) != wheels_.end();
}

// ---------------------------------------------------------------- input

void apply_player_input(OdeCar& car, const DriveInput& in, double dtd, bool frozen) {
    const float dt = static_cast<float>(dtd);
    const float A = 3 * dt;
    const float turn = 0.8f * dt;
    car.steer = std::clamp(car.steer, -1.0f, 1.0f);
    car.speed = std::clamp(car.speed, -35.0f, 10.0f);
    if (!frozen) {
        if (in.joystick) {
            const float x = std::clamp(in.joyX, -100.0f, 100.0f) / 10.0f;
            car.steer = (x < 0 ? -1.0f : 1.0f) * x * x / 300.0f;
        } else if (in.right) car.steer += car.steer < 0 ? 3 * turn : turn;
        else if (in.left) car.steer -= car.steer > 0 ? 3 * turn : turn;
        else car.steer *= 0.5f;
    }
    if (in.handbrake) {
        car.motorRunning = false;
        car.speed = 0;
    } else if (!frozen && in.joystick) {
        // Joystick drive (0x541410): k = floor(Y/10)/100, opposite direction brakes x4.
        car.motorRunning = true;
        const float steps = std::floor(std::clamp(in.joyY, -100.0f, 100.0f) / 10.0f);
        const float k = steps / 100.0f;
        car.speed += in.joyY * car.speed < 0 ? 4 * k : k;
        if (steps == 0) car.speed *= 0.993f;
        if (in.brake) {
            if (std::abs(car.speed) < 0.1f) car.speed = 0;
            else if (car.speed < 0) car.speed += 5 * A;
            else car.speed -= 5 * A;
        }
    } else if (!frozen) {
        if (in.down || in.brake) {
            car.motorRunning = true;
            if (in.brake) {
                if (std::abs(car.speed) < 0.1f) car.speed = 0;
                else if (car.speed < 0) car.speed += 5 * A;
                else car.speed -= 5 * A;
            } else {
                car.speed += car.speed < 0 ? 5 * A : A;
            }
        } else if (in.up) {
            car.motorRunning = true;
            car.speed -= car.speed > 0 ? 5 * A : A;
        } else {
            car.speed *= 0.993f;
            if (car.kmh() < 2) car.speed = 0;
        }
    }
}

}  // namespace tto
