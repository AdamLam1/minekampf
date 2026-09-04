#include "gameplay/block_interaction.hpp"

#include "core/math.hpp"
#include "physics/aabb.hpp"
#include "physics/raycast.hpp"
#include "core/event_bus.hpp"

namespace mc {

bool break_block(World& world, BlockPos pos) {
    BlockId b = world.get_block(pos);
    if (b == BLOCK_AIR || b == BLOCK_BEDROCK || b == BLOCK_QUEST_NPC) return false;
    bool success = world.set_block(pos, BLOCK_AIR);
    if (success) {
        EventBus::get().publish(BlockBreakEvent{pos.x, pos.y, pos.z, b});
    }
    return success;
}

bool place_block(World& world, BlockPos pos, BlockId block, const Player& player) {
    if (block == BLOCK_AIR) return false;
    BlockId existing = world.get_block(pos);
    if (existing != BLOCK_AIR && !is_liquid(existing)) return false;

    // Don't place inside the player's own AABB (PHASE4 sanity).
    AABB block_box(Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)),
                   Vec3(static_cast<float>(pos.x + 1), static_cast<float>(pos.y + 1),
                        static_cast<float>(pos.z + 1)));
    if (block_box.intersects(player.aabb()) && is_solid(block)) return false;

    bool success = world.set_block(pos, block);
    if (success) {
        EventBus::get().publish(BlockPlaceEvent{pos.x, pos.y, pos.z, block});
    }
    return success;
}

std::optional<HitResult> interact_break(World& world, const Player& player, float reach) {
    Vec3 eye = player.eye_position();
    Vec3 dir = forward_from_yaw_pitch(player.yaw, player.pitch);
    auto hit = voxel_raycast(world, eye, dir, reach);
    if (hit) (void) break_block(world, hit->block_pos);
    return hit;
}

std::optional<HitResult> interact_place(World& world, const Player& player, float reach) {
    Vec3 eye = player.eye_position();
    Vec3 dir = forward_from_yaw_pitch(player.yaw, player.pitch);
    auto hit = voxel_raycast(world, eye, dir, reach);
    if (!hit) return std::nullopt;
    BlockPos target = hit->block_pos + offset(hit->face);
    BlockId to_place = player.inventory.get_selected_item().item;
    if (to_place == BLOCK_AIR) return std::nullopt;
    bool placed = place_block(world, target, to_place, player);
    if (!placed) return std::nullopt;
    return hit;
}

} // namespace mc
