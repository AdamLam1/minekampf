#include <gtest/gtest.h>
#include "renderer/mob_rig.hpp"

#include <cmath>

namespace mc {

namespace {
constexpr float PI = 3.14159265358979f;
} // namespace

// The rig's reason to exist: limbs rotate around the JOINT, not the box
// center. A leg hanging 1 unit below its hip pivot, rotated 90 degrees about
// X, must end up 1 unit in front of (or behind) the hip — not floating in
// place the way the old center-rotation code behaved.
TEST(MobRigTest, LimbRotatesAroundJointNotCenter) {
    rig::BoneDef leg;
    leg.pivot = {0.0f, 0.6f, 0.0f};      // hip
    leg.center_offset = {0.0f, -0.3f, 0.0f}; // box center below the joint
    leg.size = {0.2f, 0.6f, 0.2f};

    glm::mat4 root(1.0f);

    glm::vec3 resting = rig::bone_center(leg, root, 0.0f);
    EXPECT_NEAR(resting.y, 0.3f, 1e-4f); // 0.6 - 0.3
    EXPECT_NEAR(resting.x, 0.0f, 1e-4f);
    EXPECT_NEAR(resting.z, 0.0f, 1e-4f);

    // 90-degree forward swing: the center must move to joint height,
    // displaced along Z — the signature of pivot (not center) rotation.
    glm::vec3 swung = rig::bone_center(leg, root, PI / 2.0f);
    EXPECT_NEAR(swung.y, 0.6f, 1e-4f);
    EXPECT_NEAR(std::fabs(swung.z), 0.3f, 1e-4f);
    EXPECT_NEAR(swung.x, 0.0f, 1e-4f);
}

TEST(MobRigTest, BaseRotationComposesWithPoseRotation) {
    // Zombie arm: base_rot_x = -1.35 (raised), swing adds on top.
    rig::BoneDef arm;
    arm.pivot = {0.35f, 1.12f, 0.0f};
    arm.center_offset = {0.0f, -0.33f, 0.0f};
    arm.size = {0.18f, 0.66f, 0.18f};
    arm.base_rot_x = -1.35f;

    glm::vec3 idle = rig::bone_center(arm, glm::mat4(1.0f), 0.0f);
    glm::vec3 mid_swing = rig::bone_center(arm, glm::mat4(1.0f), -0.5f);

    // Additional rotation must actually move the bone.
    EXPECT_GT(glm::distance(idle, mid_swing), 0.1f);
    // Raised arm points forward: center shifts +Z (forward) from the joint.
    EXPECT_GT(idle.z, 0.2f);
}

TEST(MobRigTest, RootCarriesPositionAndYaw) {
    rig::BoneDef torso;
    torso.pivot = {0, 1.15f, 0};
    torso.center_offset = {0, -0.35f, 0};
    torso.size = {0.5f, 0.7f, 0.3f};

    glm::mat4 root = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 64.0f, -5.0f));
    root = glm::rotate(root, glm::radians(90.0f), glm::vec3(0, 1, 0));

    glm::vec3 c = rig::bone_center(torso, root, 0.0f);
    // Local (0, 0.8, 0) is on the yaw axis: position translates, yaw ignores it.
    EXPECT_NEAR(c.x, 10.0f, 1e-4f);
    EXPECT_NEAR(c.y, 64.8f, 1e-4f);
    EXPECT_NEAR(c.z, -5.0f, 1e-4f);
}

TEST(MobRigTest, WalkPhaseStopsWhenStanding) {
    EXPECT_FLOAT_EQ(rig::walk_phase(123.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(rig::walk_phase(0.0f, true), 0.0f);
    EXPECT_GT(rig::walk_phase(0.5f, true), 0.0f);

    // Standing mobs must not moonwalk: zero phase -> zero swing.
    EXPECT_FLOAT_EQ(rig::leg_swing(rig::walk_phase(9.0f, false)), 0.0f);
}

TEST(MobRigTest, DiagonalGaitMirrorsPairs) {
    float p = 0.7f;
    float a = rig::gait_leg(p, true);
    float b = rig::gait_leg(p, false);
    EXPECT_NEAR(a, -b, 1e-5f); // FL+BR oppose FR+BL
}

TEST(MobRigTest, AttackSwingDrivesArmForward) {
    // Chop pushes the arm angle negative (forward-down).
    EXPECT_NEAR(rig::arm_swing(0.0f, 0.0f, 1.0f), -1.2f, 1e-5f);
    EXPECT_NEAR(rig::arm_swing(0.0f, 0.0f, 0.0f), 0.0f, 1e-5f);
    EXPECT_LT(rig::arm_swing(0.0f, 0.0f, 0.5f), 0.0f);
}

TEST(MobRigTest, DecayClampsAtZero) {
    EXPECT_FLOAT_EQ(rig::decay(0.3f, 0.1f), 0.2f);
    EXPECT_FLOAT_EQ(rig::decay(0.05f, 0.1f), 0.0f);
    EXPECT_FLOAT_EQ(rig::decay(0.0f, 0.1f), 0.0f);
}

} // namespace mc
