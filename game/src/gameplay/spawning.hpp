#pragma once

#include <optional>
#include <vector>
#include "core/config.hpp"
#include "core/random.hpp"
#include "core/types.hpp"
#include "gameplay/entity.hpp"
#include "world/world.hpp"

namespace mc {

enum class MobCategory : uint8_t {
    Monster = 0,
    Creature = 1,
    Ambient = 2,
    WaterCreature = 3,
    WaterAmbient = 4,
    Count,
};

struct SpawnEntry {
    MobCategory category;
    int weight;
    int min_pack;
    int max_pack;
};

class MobSpawner {
public:
    void tick(World& world, std::vector<Mob>& mobs, Rng& rng, int64_t tick,
              const std::vector<Vec3>& player_positions, float time_of_day);

    // Deterministic spawn for chat commands / automated tests (/spawnmob).
    // `forced_type` pins the species (skeleton archer tests etc.).
    Mob spawn_forced(MobCategory cat, BlockPos pos, Rng& rng,
                     std::optional<MobType> forced_type = std::nullopt);

    // Spawn-position rule for one category at one instant. Public so tests
    // can lock the underground/night split (dungeons spawn at any hour).
    bool can_spawn_at(const World& world, BlockPos pos, MobCategory cat,
                      const std::vector<Vec3>& player_positions,
                      float time_of_day) const;

private:
    int count_mobs_in_category(const std::vector<Mob>& mobs, MobCategory cat,
                               const std::vector<Vec3>& player_positions) const;
    bool can_spawn_category(MobCategory cat, int current_count, int player_count) const;
    Mob spawn_mob(MobCategory cat, BlockPos pos, Rng& rng,
                  std::optional<MobType> forced_type = std::nullopt);

    static constexpr int MOB_CAP_PER_PLAYER[] = {70, 10, 15, 5, 20};

    int spawn_timer_monster_ = 0;
    int spawn_timer_creature_ = 0;
    int spawn_timer_ambient_ = 0;
};

} // namespace mc
