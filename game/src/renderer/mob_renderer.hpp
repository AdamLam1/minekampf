#pragma once

// Batched, textured mob renderer driven by Blockbench models.
//
// Species come from MobRegistry: the four built-ins plus custom species
// discovered in assets/models/mobs/ (.geo.json or .bbmodel, texture from a
// sidecar .png or embedded in the .bbmodel). Species without a usable model
// file fall back to procedural colored boxes so the game never renders an
// invisible mob.
//
// All mobs — textured or fallback — are drawn in ONE draw call per frame:
// the CPU poses every bone (rig curves from mob_rig.hpp), transforms a
// per-cube vertex template and streams the batch into a single dynamic VBO.

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "renderer/camera.hpp"
#include "renderer/geo_model.hpp"
#include "renderer/texture_atlas.hpp"
#include "renderer/shader.hpp"

namespace mc {

struct Mob; // defined in gameplay/entity.hpp (struct — tag must match!)
enum class MobType : uint8_t;

// Nearest-neighbour RGBA resampler. Used to normalize mob textures of
// arbitrary sizes into one GL_TEXTURE_2D_ARRAY (all layers share dims).
// Exposed for unit tests. `dst` receives dw*dh*4 bytes.
void resample_rgba_nearest(const uint8_t* src, int src_w, int src_h,
                           std::vector<uint8_t>& dst, int dst_w, int dst_h);

class MobRenderer {
public:
    bool init();
    void shutdown();

    // Box-UV rect (texture pixels) for one cube face.
    struct UvRect {
        float u, v, w, h;
    };

    void draw(const Camera& camera, const std::vector<Mob>& mobs, float time,
              float sky_brightness, const TextureAtlas& atlas);

    // Depth-only draw into the shadow map so mobs cast shadows on terrain.
    // Caller owns the FBO/state (front-face culling); this temporarily
    // disables culling since box UVs on thin bones wind both ways.
    void draw_depth(const glm::mat4& light_space_matrix, const std::vector<Mob>& mobs,
                    float time);

private:
    struct ModelEntry {
        std::unique_ptr<GeoModel> geo;   // null = procedural fallback boxes
        std::vector<GeoAnimation> animations; // from <species>.animation.json
        int idle_anim = -1;              // index into animations ("idle")
        int walk_anim = -1;              // index into animations ("walk")
        int texture_layer = -1;          // layer in the mob texture array
        bool quadruped = false;          // fallback skeleton layout
        bool zombie_arms = false;        // fallback: raised arms
        float scale = 1.0f;              // model render scale from the spec
        std::string name;                // species key (for logs)
        glm::vec3 col_body{1.0f};
        glm::vec3 col_head{1.0f};
        glm::vec3 col_limb{1.0f};
    };

    struct Vertex {
        glm::vec3 pos;
        glm::vec2 uv;
        float layer;   // -1 = untextured (use vertex color)
        float light;   // 0..1 shading multiplier
        uint8_t r, g, b, a;
    };

    // Poses every alive mob and appends its cubes to batch_ (shared by the
    // color and depth passes).
    void collect_mob_geometry(const std::vector<Mob>& mobs, float time,
                              float sky_light, bool apply_hurt);
    const ModelEntry& entry_for(MobType type) const;
    // Emits a unit cube transformed by `transform`. `rects` provides per-face
    // box-UV rects; nullptr renders an untextured colored cube (layer = -1).
    void emit_cube(std::vector<Vertex>& out, const glm::mat4& transform,
                   const UvRect* rects, int layer, float light,
                   const glm::vec3& tint) const;

    Shader shader_;
    Shader shadow_shader_;  // depth-only variant for the shadow pass
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    uint32_t tex_array_ = 0;
    int layer_count_ = 0;
    int array_w_ = 0; // mob texture array size (all layers share it)
    int array_h_ = 0;
    std::vector<ModelEntry> entries_; // indexed by MobType (registry id)
    std::vector<Vertex> batch_;
    std::vector<glm::mat4> bone_mats_;
};

} // namespace mc
