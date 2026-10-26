#include <gtest/gtest.h>

#include "game/vehicle.hpp"

using namespace tto;

TEST(Vehicle, PlayerInputRampsAndClamps) {
    Physics ph;
    OdeCar car(ph, CarParams{}, {0, 1, 0}, {0, 0, 1});
    DriveInput up;
    up.up = true;
    for (int i = 0; i < 1000; ++i) apply_player_input(car, up, 0.02, false);
    EXPECT_FLOAT_EQ(car.speed, -35.0f + 3 * 0.02f * 0 - 0.06f) << "clamped to -35 then one more step";
    DriveInput right;
    right.right = true;
    for (int i = 0; i < 200; ++i) apply_player_input(car, right, 0.02, false);
    EXPECT_GT(car.steer, 0.9f);
    apply_player_input(car, DriveInput{}, 0.02, false);
    EXPECT_LE(car.steer, 0.6f) << "steering recentres by half per frame";
}

TEST(Vehicle, CarDrivesForwardOnGround) {
    Physics ph;
    ph.add_static_box({0, -0.5f, 0}, {200, 1, 200}, {0, 0, 1});
    OdeCar car(ph, CarParams{}, {0, 0.6f, 0}, {0, 0, 1});
    DriveInput up;
    up.up = true;
    for (int i = 0; i < 150; ++i) {
        apply_player_input(car, up, 0.02, false);
        ph.advance(0.02, [&] { car.update_joints(); });
    }
    EXPECT_GT(car.kmh(), 5.0f);
    EXPECT_GT(car.position().z, 1.0f) << "forward is +Z";
    EXPECT_NEAR(car.position().y, 0.5f, 0.5f) << "rests on the ground";
}

TEST(Vehicle, PositiveSteerTurnsRight) {
    Physics ph;
    ph.add_static_box({0, -0.5f, 0}, {400, 1, 400}, {0, 0, 1});
    OdeCar car(ph, CarParams{}, {0, 0.6f, 0}, {0, 0, 1});
    DriveInput in;
    in.up = in.right = true;
    for (int i = 0; i < 200; ++i) {
        apply_player_input(car, in, 0.02, false);
        ph.advance(0.02, [&] { car.update_joints(); });
    }
    // Facing +Z with Y up, the driver's right is -X.
    EXPECT_LT(car.position().x, -1.0f);
}
