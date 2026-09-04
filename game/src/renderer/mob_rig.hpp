#pragma once

// Procedural skeletal animation for mobs: bone definitions + forward
// kinematics. Pure glm math, no OpenGL, so pose math is unit-testable
// (tests/test_mob_rig).
//
// Each bone rotates around its pivot (the joint), not its box center — that
// distinction is the whole point: limbs must swing like pendulums from the
// shoulder/hip. `center_offset` places the box relative to its joint, so a
// hanging limb is offset (0, -len/2, 0) below the pivot.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace mc::rig {

struct BoneDef {
    glm::vec3 pivot;         // joint position in mob-local space (feet at y = 0)
    glm::vec3 center_offset; // box center relative to the joint
    glm::vec3 size;          // box extents
    float base_rot_x = 0.0f; // resting pose angle (zombie arms raised, etc.)
};

struct Pose {
    float walk_swing = 0.0f; // radians, sin of the walk cycle
    float attack = 0.0f;     // 1 -> 0 linear decay after a swing
    float head_pitch = 0.0f; // radians (player pitch convention)
    float hurt = 0.0f;       // 1 -> 0 red flash mix
};

// Composes a bone's world-local matrix: parent * T(pivot) * Rx(angle) *
// T(center_offset) * S(size). `root` already contains mob position + yaw.
inline glm::mat4 bone_matrix(const BoneDef& def, const glm::mat4& root, float rot_x) {
    glm::mat4 m = glm::translate(root, def.pivot);
    float total = def.base_rot_x + rot_x;
    if (total != 0.0f) m = glm::rotate(m, total, glm::vec3(1, 0, 0));
    m = glm::translate(m, def.center_offset);
    return glm::scale(m, def.size);
}

// World position of a bone's box center after posing — what tests assert on.
inline glm::vec3 bone_center(const BoneDef& def, const glm::mat4& root, float rot_x) {
    glm::mat4 m = bone_matrix(def, root, rot_x);
    return glm::vec3(m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

// ---- Animation curves ----

// Walk cycle phase: fixed cadence is fine for the box mobs; speed only gates
// on/off (a standing mob must not moonwalk).
inline float walk_phase(float time_seconds, bool moving) {
    return moving ? time_seconds * 10.0f : 0.0f;
}

inline float leg_swing(float phase, float amplitude = 0.45f) {
    return std::sin(phase) * amplitude;
}

// Arm swing: pendulum plus an attack chop that thrusts the arm forward-down
// as `attack` decays from 1 to 0.
inline float arm_swing(float phase, float amplitude, float attack) {
    return std::sin(phase) * amplitude - attack * 1.2f;
}

// Diagonal quadruped gait: FL/BR in phase, FR/BL mirrored.
inline float gait_leg(float phase, bool diagonal_pair, float amplitude = 0.35f) {
    float s = std::sin(phase) * amplitude;
    return diagonal_pair ? s : -s;
}

// Linear decay used by both attack and hurt timers (1 at trigger, 0 after
// `decay_per_tick` per tick).
inline float decay(float value, float decay_per_tick) {
    float v = value - decay_per_tick;
    return v < 0.0f ? 0.0f : v;
}

} // namespace mc::rig
