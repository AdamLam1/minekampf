#include "physics/movement.hpp"

#include <cmath>

#include "core/config.hpp"
#include "core/math.hpp"
#include "physics/aabb.hpp"
#include "physics/collision.hpp"
#include "world/block.hpp"

namespace mc {

namespace {
[[nodiscard]] bool is_in_water(const World& world, const Player& p) {
    // Sample the block at the player's feet and eye mid-point.
    BlockPos feet(static_cast<int>(std::floor(p.pos.x)), static_cast<int>(std::floor(p.pos.y + 0.1f)),
                  static_cast<int>(std::floor(p.pos.z)));
    return is_liquid(world.get_block(feet));
}

[[nodiscard]] Vec3 forward_xz(float yaw) { return Vec3(-std::sin(yaw), 0.0f, std::cos(yaw)); }
[[nodiscard]] Vec3 right_xz(float yaw) { return Vec3(-std::cos(yaw), 0.0f, -std::sin(yaw)); }
} // namespace

void tick_player(World& world, Player& player, const PlayerInput& input) {
    player.prev_pos = player.pos;
    player.prev_walk_dist = player.walk_dist;
    player.prev_bob_anim = player.bob_anim;
    
    player.prev_swing_progress = player.swing_progress;
    if (player.is_swinging) {
        player.swing_progress += 0.2f; // 5 ticks duration
        if (player.swing_progress >= 1.0f) {
            player.swing_progress = 0.0f;
            player.is_swinging = false;
        }
    } else {
        player.swing_progress = 0.0f;
    }
    player.sneaking = input.sneak;
    player.sprinting = input.sprint && std::abs(input.forward) > 0.0f && !input.sneak;
    player.in_water = is_in_water(world, player);

    // Build horizontal wish direction from yaw + input (diagonal normalized).
    Vec3 fwd = forward_xz(player.yaw);
    Vec3 right = right_xz(player.yaw);
    Vec3 wish = fwd * input.forward + right * input.strafe;
    if (std::sqrt(wish.x * wish.x + wish.z * wish.z) > 1.0f) {
        wish = glm::normalize(Vec3(wish.x, 0.0f, wish.z));
        wish.y = 0.0f;
    }

    if (player.flying) {
        // Creative flying (PHASE4 §3.4) with smooth acceleration and Y-damping.
        float fly_speed = WALK_SPEED * (player.sprinting ? 4.0f : 2.0f);
        float accel = fly_speed * 0.4f;
        player.velocity.x += wish.x * accel;
        player.velocity.z += wish.z * accel;
        if (input.jump) player.velocity.y += accel;
        else if (input.sneak) player.velocity.y -= accel;
        else player.velocity.y *= 0.5f;
    } else if (player.in_water) {
        // Swimming (PHASE4 §3.3): buoyancy + heavy drag + slow horizontal.
        float swim = 0.05f;
        player.velocity.x = player.velocity.x * 0.8f + wish.x * swim;
        player.velocity.z = player.velocity.z * 0.8f + wish.z * swim;
        player.velocity.y = player.velocity.y * 0.8f - 0.02f; // buoyancy vs gravity
        if (input.jump) player.velocity.y += 0.04f; // swim up
    } else {
        // Walking / jumping (PHASE4 §3.1-3.2).
        float speed = WALK_SPEED;
        if (player.sprinting) speed += (speed * 0.3f); // 30% speed boost

        // Ground friction depends on block below feet.
        float slip = 0.6f; // Default block slipperiness
        if (player.on_ground) {
            BlockPos below(static_cast<int>(std::floor(player.pos.x)),
                           static_cast<int>(std::floor(player.pos.y - 0.1f)),
                           static_cast<int>(std::floor(player.pos.z)));
            slip = slipperiness(world.get_block(below));
        }

        float accel;
        if (player.on_ground) {
            float f = slip * 0.91f;
            accel = speed * (0.16277136f / (f * f * f));
        } else {
            accel = player.sprinting ? 0.0259f : 0.02f; // Minecraft air acceleration
        }

        player.velocity.x += wish.x * accel;
        player.velocity.z += wish.z * accel;

        // Jump.
        if (input.jump && player.on_ground) {
            player.velocity.y = JUMP_VELOCITY;
            if (player.sprinting) {
                // Sprint jump boost
                float yaw_rad = player.yaw;
                player.velocity.x += -std::sin(yaw_rad) * 0.2f;
                player.velocity.z += std::cos(yaw_rad) * 0.2f;
            }
        }
    }

    // Integrate with collision BEFORE applying friction/gravity (Minecraft order)
    AABB box = player.aabb();
    CollisionResult res = move_and_collide(world, box, player.velocity);
    player.pos = Vec3((box.min.x + box.max.x) * 0.5f, box.min.y, (box.min.z + box.max.z) * 0.5f);
    player.on_ground = res.on_ground;
    
    // Accumulate walked distance for view bobbing
    float dx = player.pos.x - player.prev_pos.x;
    float dz = player.pos.z - player.prev_pos.z;
    float dist = std::sqrt(dx * dx + dz * dz);
    
    // Minecraft-like accumulation: mostly relies on horizontal distance
    if (!player.flying) {
        player.walk_dist += dist * 0.6f;
    }
    
    // Smooth the amplitude of the bobbing based on current movement speed
    float target_bob = 0.0f;
    if (player.on_ground && !player.flying) {
        target_bob = std::min(dist * 2.5f, 0.5f);
    }
    player.bob_anim += (target_bob - player.bob_anim) * 0.2f;
    
    // Apply collision response to velocity (zero out velocity against walls/floor)
    if (res.hit_x) player.velocity.x = 0.0f;
    if (res.hit_y) player.velocity.y = 0.0f;
    if (res.hit_z) player.velocity.z = 0.0f;

    if (player.flying) {
        player.velocity *= 0.6f;
    } else if (!player.in_water) {
        // Ground friction depends on block below feet.
        float slip = 0.6f; // Default block slipperiness
        if (player.on_ground) {
            BlockPos below(static_cast<int>(std::floor(player.pos.x)),
                           static_cast<int>(std::floor(player.pos.y - 0.1f)),
                           static_cast<int>(std::floor(player.pos.z)));
            slip = slipperiness(world.get_block(below));
        }
        float friction = player.on_ground ? (slip * 0.91f) : 0.91f;
        
        // Apply friction
        player.velocity.x *= friction;
        player.velocity.z *= friction;

        // Gravity + vertical drag.
        player.velocity.y -= GRAVITY_PER_TICK;
        player.velocity.y *= VERTICAL_DRAG;
    }

    // Clamp falling out of the world.
    if (player.pos.y < MIN_Y - 16) {
        player.pos.y = static_cast<float>(MAX_Y);
        player.velocity = Vec3(0, 0, 0);
    }
}

} // namespace mc
