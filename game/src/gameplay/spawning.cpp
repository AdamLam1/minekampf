#include "gameplay/spawning.hpp"
#include "world/block.hpp"
#include "world/chunk.hpp"
#include <algorithm>
#include <cmath>

namespace mc {

int MobSpawner::count_mobs_in_category(const std::vector<Mob>& mobs, MobCategory cat,
                                        const std::vector<Vec3>& player_positions) const {
    int count = 0;
    for (const auto& mob : mobs) {
        if (!mob.alive) continue;
        if (static_cast<MobCategory>(mob.spawn_category) != cat) continue;
        for (const auto& pp : player_positions) {
            float dx = mob.pos.x - pp.x;
            float dz = mob.pos.z - pp.z;
            if (dx * dx + dz * dz <= 128.0f * 128.0f) {
                ++count;
                break;
            }
        }
    }
    return count;
}

bool MobSpawner::can_spawn_category(MobCategory cat, int current_count, int player_count) const {
    int cap = MOB_CAP_PER_PLAYER[static_cast<int>(cat)] * player_count;
    return current_count < cap;
}

bool MobSpawner::can_spawn_at(const World& world, BlockPos pos, MobCategory cat,
                               const std::vector<Vec3>& player_positions,
                               float time_of_day) const {
    if (pos.y < MIN_Y + 1 || pos.y >= MAX_Y - 2) return false;

    BlockId block_at = world.get_block(pos);
    BlockId block_below = world.get_block({pos.x, pos.y - 1, pos.z});
    BlockId block_above = world.get_block({pos.x, pos.y + 1, pos.z});

    // Get block light at spawn position
    uint8_t block_light = 0;
    ChunkPos cp = chunk_from_block(pos);
    if (Chunk* ch = world.get_chunk(cp)) {
        block_light = ch->get_block_light(pos);
    }

    float min_player_dist = INFINITY;
    for (const auto& pp : player_positions) {
        float dx = static_cast<float>(pos.x) + 0.5f - pp.x;
        float dy = static_cast<float>(pos.y) + 0.5f - pp.y;
        float dz = static_cast<float>(pos.z) + 0.5f - pp.z;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < min_player_dist) min_player_dist = d;
    }

    switch (cat) {
        case MobCategory::Monster: {
            if (min_player_dist < 24.0f) return false;
            if (block_at != BLOCK_AIR || block_above != BLOCK_AIR) return false;
            if (!is_opaque(block_below) || !is_full_cube(block_below)) return false;
            if (block_light > 0) return false;
            // Surface monsters are night-only; deep spawns (dungeons, caves)
            // are darkness-gated and happen at any hour (vanilla rule).
            bool underground = pos.y <= SEA_LEVEL - 12;
            if (!underground && !(time_of_day < 0.15f || time_of_day > 0.85f)) return false;
            return true;
        }
        case MobCategory::Creature: {
            if (block_at != BLOCK_AIR || block_above != BLOCK_AIR) return false;
            if (block_below != BLOCK_GRASS) return false;
            if (block_light < 9) return false;
            return true;
        }
        case MobCategory::Ambient: {
            if (pos.y >= 63) return false;
            if (block_at != BLOCK_AIR) return false;
            if (block_light > 4) return false;
            return true;
        }
        case MobCategory::WaterCreature:
        case MobCategory::WaterAmbient: {
            if (block_at != BLOCK_WATER) return false;
            if (!is_opaque(block_below)) return false;
            return true;
        }
        default: return false;
    }
}

Mob MobSpawner::spawn_mob(MobCategory cat, BlockPos pos, Rng& rng,
                          std::optional<MobType> forced_type) {
    Mob m;
    m.pos = Vec3(static_cast<float>(pos.x) + 0.5f, static_cast<float>(pos.y), static_cast<float>(pos.z) + 0.5f);
    m.spawn_category = static_cast<uint8_t>(cat);
    m.alive = true;
    m.is_natural_spawn = true;
    m.yaw = rng.next_float() * 6.2831853f;

    if (cat == MobCategory::Monster) {
        // Night surface monsters: zombie (melee bruiser) or skeleton (archer).
        bool skeleton = (forced_type == MobType::Skeleton) ||
                        (forced_type == std::nullopt && rng.next_int(2) == 0);
        m.type = skeleton ? MobType::Skeleton : MobType::Zombie;
        m.max_health = skeleton ? 16.0f : 20.0f;
        m.health = m.max_health;
        m.speed = skeleton ? 0.07f : 0.06f;
        m.attack_damage = skeleton ? 2.0f : 3.0f;
        m.xp_reward = 5;
        if (skeleton) {
            m.goal_selector.add_goal(2, std::make_shared<RangedAttackGoal>());
            m.goal_selector.add_goal(3, std::make_shared<MeleeAttackGoal>());
        } else {
            m.goal_selector.add_goal(2, std::make_shared<MeleeAttackGoal>());
        }
        m.goal_selector.add_goal(4, std::make_shared<LookAtPlayerGoal>());
        m.goal_selector.add_goal(6, std::make_shared<WanderGoal>());
    } else if (cat == MobCategory::Creature) {
        // Passive pasture animals; both drop edible meat.
        if (forced_type == MobType::Cow || forced_type == MobType::Pig) {
            m.type = *forced_type;
        } else {
            m.type = (rng.next_int(2) == 0) ? MobType::Pig : MobType::Cow;
        }
        m.max_health = 10.0f;
        m.health = m.max_health;
        m.speed = 0.05f;
        m.xp_reward = 2;
        m.goal_selector.add_goal(4, std::make_shared<LookAtPlayerGoal>());
        m.goal_selector.add_goal(6, std::make_shared<WanderGoal>());
    } else {
        // Ambient / water: unchanged generic wanderer.
        m.type = MobType::Zombie;
        m.health = m.max_health = 20.0f;
        m.speed = (cat == MobCategory::Ambient) ? 0.1f : 0.04f;
        m.goal_selector.add_goal(5, std::make_shared<WanderGoal>());
    }

    return m;
}

Mob MobSpawner::spawn_forced(MobCategory cat, BlockPos pos, Rng& rng,
                             std::optional<MobType> forced_type) {
    Mob m = spawn_mob(cat, pos, rng, forced_type);
    m.is_natural_spawn = false; // never distance-despawned mid-test
    return m;
}

void MobSpawner::tick(World& world, std::vector<Mob>& mobs, Rng& rng, int64_t tick,
                       const std::vector<Vec3>& player_positions, float time_of_day) {
    if (player_positions.empty()) return;

    int player_count = static_cast<int>(player_positions.size());

    // Despawn mobs far from players
    for (auto it = mobs.begin(); it != mobs.end(); ) {
        if (!it->alive || it->is_persistent) { ++it; continue; }
        float min_d = INFINITY;
        for (const auto& pp : player_positions) {
            float dx = it->pos.x - pp.x;
            float dy = it->pos.y - pp.y;
            float dz = it->pos.z - pp.z;
            float d = dx * dx + dy * dy + dz * dz;
            if (d < min_d) min_d = d;
        }
        min_d = std::sqrt(min_d);

        if (!it->is_natural_spawn) { ++it; continue; }
        if (min_d > 128.0f) {
            it->despawn_timer += 1;
            if (it->despawn_timer > 600) {
                if (rng.next_int(800) == 0) {
                    it = mobs.erase(it);
                    continue;
                }
            }
        } else {
            it->despawn_timer = 0;
        }
        ++it;
    }

    // Spawn attempts
    // Monster: night on the surface, any hour underground (darkness-gated).
    bool is_night = (time_of_day < 0.15f || time_of_day > 0.85f);
    (void)is_night; // kept for readability; the rule lives in can_spawn_at
    // Creature: spawn during day
    bool is_day = (time_of_day > 0.15f && time_of_day < 0.85f);

    // Try monster spawning (4 times per second = every 5 ticks)
    if (tick % 5 == 0) {
        int mon_count = count_mobs_in_category(mobs, MobCategory::Monster, player_positions);
        if (can_spawn_category(MobCategory::Monster, mon_count, player_count)) {
            for (int attempt = 0; attempt < 3; ++attempt) {
                // Pick random loaded chunk
                auto positions = world.loaded_positions();
                if (positions.empty()) break;
                auto& cp = positions[rng.next_int(static_cast<int>(positions.size()))];
                int lx = rng.next_int(CHUNK_SIZE);
                int lz = rng.next_int(CHUNK_SIZE);
                int ly = rng.next_int(WORLD_HEIGHT - 2);
                BlockPos bp(cp.x * CHUNK_SIZE + lx, ly, cp.z * CHUNK_SIZE + lz);

                if (can_spawn_at(world, bp, MobCategory::Monster, player_positions, time_of_day)) {
                    int pack = rng.next_int(4) + 1;
                    for (int p = 0; p < pack; ++p) {
                        BlockPos ppos(bp.x + rng.next_int(5) - 2, bp.y, bp.z + rng.next_int(5) - 2);
                        if (can_spawn_at(world, ppos, MobCategory::Monster, player_positions, time_of_day)) {
                            mobs.push_back(spawn_mob(MobCategory::Monster, ppos, rng));
                            if (count_mobs_in_category(mobs, MobCategory::Monster, player_positions) >=
                                MOB_CAP_PER_PLAYER[static_cast<int>(MobCategory::Monster)] * player_count)
                                break;
                        }
                    }
                    break;
                }
            }
        }
    }

    // Try creature spawning (every 20 ticks = 1/sec)
    if (is_day && tick % 20 == 0) {
        int cre_count = count_mobs_in_category(mobs, MobCategory::Creature, player_positions);
        if (can_spawn_category(MobCategory::Creature, cre_count, player_count)) {
            for (int attempt = 0; attempt < 2; ++attempt) {
                auto positions = world.loaded_positions();
                if (positions.empty()) break;
                auto& cp = positions[rng.next_int(static_cast<int>(positions.size()))];
                int lx = rng.next_int(CHUNK_SIZE);
                int lz = rng.next_int(CHUNK_SIZE);
                // Find surface height
                Chunk* ch = world.get_chunk(cp);
                if (!ch) continue;
                int ly = ch->heightmap[lz * CHUNK_SIZE + lx];
                BlockPos bp(cp.x * CHUNK_SIZE + lx, ly, cp.z * CHUNK_SIZE + lz);

                if (can_spawn_at(world, bp, MobCategory::Creature, player_positions, time_of_day)) {
                    int pack = rng.next_int(3) + 1;
                    for (int p = 0; p < pack; ++p) {
                        BlockPos ppos(bp.x + rng.next_int(8) - 4, bp.y, bp.z + rng.next_int(8) - 4);
                        if (can_spawn_at(world, ppos, MobCategory::Creature, player_positions, time_of_day)) {
                            mobs.push_back(spawn_mob(MobCategory::Creature, ppos, rng));
                        }
                    }
                    break;
                }
            }
        }
    }

    // Tick alive mobs
    for (auto& m : mobs) {
        if (m.alive) m.tick(world);
    }

    // Clean up dead mobs
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(),
        [](const Mob& m) { return !m.alive; }), mobs.end());
}

} // namespace mc
