#pragma once

#include <vector>
#include <cstdint>

#include <glm/glm.hpp>
#include <glad/gl.h>

#include "core/types.hpp"
#include "renderer/camera.hpp"
#include "renderer/shader.hpp"
#include "renderer/texture_atlas.hpp"
#include "world/world.hpp"

namespace mc {

enum class ParticleType {
    BlockDust,
    Smoke,
    Flame,
    WaterDrip,
    RainStreak // world-vertical falling streak, faded at the tail
};

struct Particle {
    Vec3 pos;
    Vec3 velocity;
    uint32_t age = 0;
    uint32_t max_age = 30;
    float size = 0.2f;
    float gravity = 0.04f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float texture_u = 0.0f, texture_v = 0.0f, texture_w = 0.0f;
    float texture_size = 0.0625f; // 1/16
    bool collision = true;
    ParticleType type = ParticleType::BlockDust;
};

// Batched particle vertex
struct ParticleVertex {
    float x, y, z;
    float u, v, w;
    uint8_t r, g, b, a;
    float size;
};

class ParticleSystem {
public:
    bool init();
    void shutdown();
    
    void update(const World& world);
    void draw(const Camera& camera, TextureAtlas& atlas);
    
    void spawn(const Particle& p);
    void spawn_block_dust(BlockPos pos, BlockId id, const TextureAtlas& atlas);
    [[nodiscard]] std::size_t count() const { return particles_.size(); }

private:
    std::vector<Particle> particles_;
    std::vector<ParticleVertex> verts_;
    
    Shader shader_;
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
};

} // namespace mc
