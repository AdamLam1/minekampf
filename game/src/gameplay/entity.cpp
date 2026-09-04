#include "gameplay/entity.hpp"
#include <algorithm>
#include <cmath>
#include "physics/collision.hpp"
#include "renderer/mob_rig.hpp"

namespace mc {

void Mob::apply_damage(float amount) {
    if (!alive) return;
    health -= amount;
    hurt_time = 1.0f;
    if (health <= 0.0f) {
        health = 0.0f;
        alive = false;
    }
}

void Mob::tick(const World& world) {
    if (!alive) return;

    if (attack_cooldown > 0) --attack_cooldown;
    attack_anim = rig::decay(attack_anim, 0.1f);
    hurt_time = rig::decay(hurt_time, 0.05f);

    // 1. Tick AI goals
    goal_selector.tick(world, *this);
    
    // 2. Tick navigation (updates velocity.x and velocity.z based on path)
    navigation.tick(world, *this);
    
    // 3. Apply physics (Gravity)
    if (!on_ground) {
        velocity.y -= 0.04f;
    }
    
    // Apply damping
    velocity.x *= 0.91f;
    velocity.z *= 0.91f;
    velocity.y *= 0.98f;
    
    // Integrate with collision (using AABB swept-like resolution)
    AABB box = aabb();
    CollisionResult res = move_and_collide(world, box, velocity);
    pos = Vec3((box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f);
    on_ground = res.on_ground;
    
    // Apply collision response to velocity (zero out velocity against walls/floor)
    if (res.hit_x) velocity.x = 0.0f;
    if (res.hit_y) velocity.y = 0.0f;
    if (res.hit_z) velocity.z = 0.0f;
}

} // namespace mc
