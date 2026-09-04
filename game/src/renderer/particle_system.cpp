#include "renderer/particle_system.hpp"

#include <algorithm>
#include <glm/gtc/type_ptr.hpp>
#include "core/logger.hpp"

namespace mc {

bool ParticleSystem::init() {
    if (!shader_.load_from_files("shaders/particle.vert", "shaders/particle.frag")) {
        MC_LOG_ERROR("Failed to load particle shaders");
        return false;
    }
    
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    
    // Position (3f)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ParticleVertex), (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ParticleVertex), (void*)(6 * sizeof(float) + 4));
    
    glBindVertexArray(0);
    
    particles_.reserve(4096);
    verts_.reserve(4096 * 4); // 4 verts per particle
    
    return true;
}

void ParticleSystem::shutdown() {
    shader_.destroy();
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
}

void ParticleSystem::spawn(const Particle& p) {
    if (particles_.size() < 4096) {
        particles_.push_back(p);
    }
}

void ParticleSystem::spawn_block_dust(BlockPos pos, BlockId id, const TextureAtlas& atlas) {
    if (id == BLOCK_AIR) return;
    
    Tile tile = tile_for_face(id, Direction::Up); // use top texture
    float u = 0.5f, v = 0.5f; // middle of texture
    
    for (int i = 0; i < 8; ++i) { // spawn 8 particles
        Particle p;
        p.pos = Vec3(pos.x + 0.5f + (rand() % 100 / 100.0f - 0.5f), 
                     pos.y + 0.5f + (rand() % 100 / 100.0f - 0.5f), 
                     pos.z + 0.5f + (rand() % 100 / 100.0f - 0.5f));
        p.velocity = Vec3((rand() % 100 / 100.0f - 0.5f) * 0.2f, 
                          (rand() % 100 / 100.0f) * 0.2f, 
                          (rand() % 100 / 100.0f - 0.5f) * 0.2f);
        p.type = ParticleType::BlockDust;
        p.texture_u = u;
        p.texture_v = v;
        p.texture_w = static_cast<float>(tile);
        p.max_age = 20 + rand() % 20;
        spawn(p);
    }
}

void ParticleSystem::update(const World& world) {
    size_t write_idx = 0;
    const size_t size = particles_.size();
    
    for (size_t i = 0; i < size; ++i) {
        auto& p = particles_[i];
        
        // Physics
        p.velocity.y -= p.gravity;
        p.velocity.x *= 0.98f;
        p.velocity.y *= 0.98f;
        p.velocity.z *= 0.98f;
        
        p.pos.x += p.velocity.x;
        p.pos.y += p.velocity.y;
        p.pos.z += p.velocity.z;
        
        if (p.collision) {
            BlockId b = world.get_block(BlockPos{static_cast<int>(std::floor(p.pos.x)), 
                                                 static_cast<int>(std::floor(p.pos.y)), 
                                                 static_cast<int>(std::floor(p.pos.z))});
            if (is_opaque(b)) {
                p.velocity.x = 0;
                p.velocity.y = 0;
                p.velocity.z = 0;
                p.age = p.max_age; // die
            }
        }
        
        p.age++;
        
        if (p.type == ParticleType::Smoke) {
            p.a = 1.0f - (static_cast<float>(p.age) / p.max_age);
        } else if (p.type == ParticleType::Flame) {
            p.size *= 0.96f;
        }
        
        if (p.age < p.max_age) {
            if (write_idx != i) {
                particles_[write_idx] = std::move(p);
            }
            write_idx++;
        }
    }
    particles_.resize(write_idx);
}

void ParticleSystem::draw(const Camera& camera, TextureAtlas& atlas) {
    if (particles_.empty()) return;
    
    // Sort back-to-front (simple distance to camera)
    std::sort(particles_.begin(), particles_.end(), [&camera](const Particle& a, const Particle& b) {
        float da = (a.pos.x - camera.position.x)*(a.pos.x - camera.position.x) + 
                   (a.pos.y - camera.position.y)*(a.pos.y - camera.position.y) + 
                   (a.pos.z - camera.position.z)*(a.pos.z - camera.position.z);
        float db = (b.pos.x - camera.position.x)*(b.pos.x - camera.position.x) + 
                   (b.pos.y - camera.position.y)*(b.pos.y - camera.position.y) + 
                   (b.pos.z - camera.position.z)*(b.pos.z - camera.position.z);
        return da > db;
    });
    
    verts_.clear();
    
    // Calculate right and up vectors for billboard
    glm::mat4 view = camera.view();
    glm::vec3 right = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 up = glm::vec3(view[0][1], view[1][1], view[2][1]);
    
    for (const auto& p : particles_) {
        uint8_t r = static_cast<uint8_t>(p.r * 255.0f);
        uint8_t g = static_cast<uint8_t>(p.g * 255.0f);
        uint8_t b = static_cast<uint8_t>(p.b * 255.0f);
        uint8_t a = static_cast<uint8_t>(p.a * 255.0f);
        
        float hs = p.size * 0.5f;
        
        // Quad 6 vertices for 2 triangles
        int indices[6] = {0, 1, 2, 0, 2, 3};
        for (int j = 0; j < 6; ++j) {
            int i = indices[j];
            float lx = (i == 1 || i == 2) ? 1.0f : -1.0f;
            float ly = (i == 2 || i == 3) ? 1.0f : -1.0f;
            
            ParticleVertex v;
            v.x = p.pos.x + right.x * lx * hs + up.x * ly * hs;
            v.y = p.pos.y + right.y * lx * hs + up.y * ly * hs;
            v.z = p.pos.z + right.z * lx * hs + up.z * ly * hs;
            
            v.u = p.texture_u + (lx > 0 ? p.texture_size : 0.0f);
            v.v = p.texture_v + (ly > 0 ? p.texture_size : 0.0f);
            v.w = p.texture_w;
            
            v.r = r; v.g = g; v.b = b; v.a = a;
            v.size = p.size;
            
            verts_.push_back(v);
        }
    }
    
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(ParticleVertex), verts_.data(), GL_STREAM_DRAW);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    
    shader_.use();
    glm::mat4 vp = camera.view_projection();
    shader_.set_mat4("u_view_proj", glm::value_ptr(vp));
    
    atlas.bind(0);
    shader_.set_int("u_atlas", 0);
    
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace mc
