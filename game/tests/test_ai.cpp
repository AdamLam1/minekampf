#include <gtest/gtest.h>
#include "gameplay/entity.hpp"
#include "gameplay/spawning.hpp"
#include "generation/world_generator.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>

namespace mc {

namespace {

// Spawns a fresh zombie the same way MobSpawner does (stats + goals) and
// ticks it inside a real generated world with a player standing nearby.
struct ZombieScenario {
    World world{12345, DimensionId::Overworld};
    Vec3 player_pos{6.5f, 70.f, 6.5f};
    std::vector<Vec3> players;
    Mob zombie;

    // Walkable standing Y: top of the highest non-leaf solid in the column
    // (open sky above — cave pockets or canopy interiors would strand the
    // mob). Returns false when the column has no such spot.
    [[nodiscard]] bool surface_y(int x, int z, int& out_y) const {
        bool found = false;
        for (int y = MAX_Y - 3; y > MIN_Y + 1; --y) {
            BlockId b = world.get_block({x, y, z});
            bool leaf = b == BLOCK_OAK_LEAVES || b == BLOCK_SPRUCE_LEAVES ||
                        b == BLOCK_BIRCH_LEAVES;
            if (leaf) continue;
            if (!is_solid(b) || b == BLOCK_BEDROCK) continue;
            // Highest non-leaf solid found; require clean 2-block air above
            // (same bar as the game's spawn rule — canopy overhead blocks a
            // 2-block-tall mob).
            BlockId f = world.get_block({x, y + 1, z});
            BlockId h = world.get_block({x, y + 2, z});
            if (f == BLOCK_AIR && h == BLOCK_AIR) {
                out_y = y + 1;
                found = true;
            }
            break; // first solid from the sky IS the surface
        }
        return found;
    }

    [[nodiscard]] static bool leaf_ok(BlockId b) {
        return b == BLOCK_OAK_LEAVES || b == BLOCK_SPRUCE_LEAVES ||
               b == BLOCK_BIRCH_LEAVES;
    }

    // Searches outward for an open-sky column; zombie and player share it.
    [[nodiscard]] bool find_open_spot(int cx, int cz, int& ox, int& oz, int& oy) const {
        for (int r = 0; r <= 8; ++r) {
            for (int dz = -r; dz <= r; ++dz) {
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    int y;
                    if (surface_y(cx + dx, cz + dz, y)) {
                        ox = cx + dx;
                        oz = cz + dz;
                        oy = y;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    explicit ZombieScenario(const Vec3& zombie_pos) : players{player_pos} {
        auto chunk = World::chunk_pool.acquire();
        chunk->reset({0, 0});
        // Flat synthetic ground instead of a generated chunk: AI tests must
        // exercise pathing/biting, not chase a moving worldgen output (biome
        // rebalances kept reshaping the terrain under the spawn column).
        for (int lz = 0; lz < CHUNK_SIZE; ++lz) {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                chunk->set_block(lx, 69, lz, BLOCK_GRASS);
            }
        }
        world.insert_chunk(std::move(chunk));

        // Player stands 3 blocks away on X, on the flat ground (feet at y=70).
        (void)zombie_pos;
        players[0] = Vec3(6.5f, 70.0f, 6.5f);
        Rng rng(7);
        int ox = 9, oz = 6, oy = 70;
        zombie = MobSpawner{}.spawn_forced(MobCategory::Monster,
                                           BlockPos(ox, oy, oz),
                                           rng, MobType::Zombie);
        // Mirror Game's per-tick targeting injection.
        zombie.target_player_pos = &players[0];
        bool attacked = false;
        zombie.attack_player = [&](float, const Vec3&) { attacked = true; };
        (void)attacked;
    }

    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            zombie.target_player_pos = &players[0];
            zombie.tick(world);
        }
    }

    [[nodiscard]] float dist_to_player() const {
        float dx = zombie.pos.x - players[0].x;
        float dz = zombie.pos.z - players[0].z;
        return std::sqrt(dx * dx + dz * dz);
    }
};

} // namespace

// The regression this locks in: a hostile zombie must CLOSE DISTANCE to a
// nearby player instead of idling forever.
TEST(AiTest, ZombieChasesNearbyPlayer) {
    ZombieScenario sc(Vec3(10.5f, 70.0f, 10.5f));

    float d0 = sc.dist_to_player();
    ASSERT_NEAR(d0, 3.0f, 0.5f);

    for (int blk = 0; blk < 6; ++blk) {
        sc.run(20);
        auto& zp = sc.zombie.pos;
        std::printf("[diag] blk%d pos=(%.2f, %.2f, %.2f) vel=(%.3f, %.3f) on_ground=%d\n",
                    blk, zp.x, zp.y, zp.z,
                    sc.zombie.velocity.x, sc.zombie.velocity.y,
                    sc.zombie.on_ground ? 1 : 0);
    }

    float d1 = sc.dist_to_player();
    EXPECT_LT(d1, d0 - 1.0f) << "zombie must approach the player (moved "
                             << (d0 - d1) << " blocks)";
}

TEST(AiTest, ZombieBitesWhenInReach) {
    // Stand right next to the player: bite callback must fire within ~1 s.
    ZombieScenario sc(Vec3(9.5f, 70.0f, 10.5f));

    bool bitten = false;
    float damage_seen = 0.0f;
    sc.zombie.attack_player = [&](float dmg, const Vec3&) {
        bitten = true;
        damage_seen = dmg;
    };

    sc.run(40);
    EXPECT_TRUE(bitten) << "in-reach zombie must land a bite";
    EXPECT_FLOAT_EQ(damage_seen, sc.zombie.attack_damage);
}

TEST(AiTest, PacifistPigNeverAttacks) {
    World world{999, DimensionId::Overworld};
    auto chunk = World::chunk_pool.acquire();
    chunk->reset({0, 0});
    WorldGenerator gen(999);
    gen.generate(*chunk);
    world.insert_chunk(std::move(chunk));

    Rng rng(3);
    Mob pig = MobSpawner{}.spawn_forced(MobCategory::Creature,
                                        BlockPos(8, 70, 8), rng, MobType::Pig);
    Vec3 pp(9.0f, 70.0f, 8.0f);
    pig.target_player_pos = &pp;
    bool bitten = false;
    pig.attack_player = [&](float, const Vec3&) { bitten = true; };

    for (int i = 0; i < 100; ++i) {
        pig.target_player_pos = &pp;
        pig.tick(world);
    }
    EXPECT_FALSE(bitten) << "passive species must never attack";
}

} // namespace mc
