#include "renderer/mob_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/logger.hpp"
#include "gameplay/entity.hpp"
#include "renderer/mob_rig.hpp"
#include "renderer/shader.hpp"

namespace mc {

namespace {

constexpr float kUnit = 1.0f / 16.0f; // Bedrock model units -> blocks

// Minecraft box unwrap: sides in a row, up/down above them.
void box_uv_rects(const GeoCube& c, float tex_w, float tex_h, MobRenderer::UvRect out[6]) {
    float u0 = c.uv.x, v0 = c.uv.y;
    float w = std::abs(c.size.x), h = std::abs(c.size.y), d = std::abs(c.size.z);
    // Face order matches kFaceCorners: south, north, down, up, west, east.
    // Rects are normalized to 0..1 here (shader samples normalized UVs).
    float iw = 1.0f / tex_w, ih = 1.0f / tex_h;
    out[0] = {(u0 + d + w) * iw, (v0 + d) * ih, w * iw, h * ih}; // south
    out[1] = {(u0 + d) * iw, (v0 + d) * ih, w * iw, h * ih};     // north
    out[2] = {(u0 + d + w) * iw, v0 * ih, w * iw, d * ih};       // down
    out[3] = {(u0 + d) * iw, v0 * ih, w * iw, d * ih};           // up
    out[4] = {(u0 + d + w + d) * iw, (v0 + d) * ih, d * iw, h * ih}; // west
    out[5] = {u0 * iw, (v0 + d) * ih, d * iw, h * ih};               // east
}

void per_face_rects(const GeoCube& c, float tex_w, float tex_h, MobRenderer::UvRect out[6]) {
    float iw = 1.0f / tex_w, ih = 1.0f / tex_h;
    for (int f = 0; f < 6; ++f) {
        out[f] = {c.faces[f].u * iw, c.faces[f].v * ih, c.faces[f].w * iw, c.faces[f].h * ih};
    }
}

// Deterministic fallback palette for custom species without a texture: a
// stable name hash picks one of a few pleasant trios.
void fallback_palette(const std::string& name, glm::vec3& body, glm::vec3& head,
                      glm::vec3& limb) {
    static const glm::vec3 kPalettes[][3] = {
        {{0.45f, 0.26f, 0.55f}, {0.60f, 0.40f, 0.68f}, {0.34f, 0.19f, 0.42f}}, // violet
        {{0.18f, 0.45f, 0.48f}, {0.28f, 0.62f, 0.60f}, {0.12f, 0.32f, 0.35f}}, // teal
        {{0.62f, 0.38f, 0.20f}, {0.75f, 0.52f, 0.30f}, {0.45f, 0.26f, 0.13f}}, // russet
        {{0.30f, 0.42f, 0.20f}, {0.42f, 0.58f, 0.28f}, {0.20f, 0.30f, 0.13f}}, // moss
        {{0.55f, 0.20f, 0.22f}, {0.70f, 0.32f, 0.32f}, {0.40f, 0.13f, 0.15f}}, // crimson
        {{0.28f, 0.32f, 0.55f}, {0.40f, 0.46f, 0.72f}, {0.18f, 0.22f, 0.40f}}, // indigo
        {{0.60f, 0.52f, 0.28f}, {0.75f, 0.68f, 0.40f}, {0.44f, 0.38f, 0.18f}}, // gold
        {{0.35f, 0.35f, 0.38f}, {0.52f, 0.52f, 0.56f}, {0.22f, 0.22f, 0.25f}}, // slate
    };
    uint32_t hash = 2166136261u;
    for (char c : name) hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    const auto& p = kPalettes[hash % 8];
    body = p[0]; head = p[1]; limb = p[2];
}

} // namespace

// ---------------------------------------------------------------- lifecycle

bool MobRenderer::init() {
    if (!shader_.load_from_files("shaders/mob.vert", "shaders/mob.frag")) {
        MC_LOG_ERROR("Failed to load mob shader");
        return false;
    }
    if (!shadow_shader_.load_from_files("shaders/shadow.vert", "shaders/shadow.frag")) {
        MC_LOG_ERROR("Failed to load mob shadow shader");
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, layer));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, light));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glBindVertexArray(0);

    // Species list: registry (built-ins + custom Blockbench models). The scan
    // is safe to repeat — it replaces previous customs.
    MobRegistry& registry = MobRegistry::instance();
    registry.scan_directory("assets/models/mobs");
    // Synthetic species for remote players (procedural humanoid rig, never
    // spawned ambient); registered after the scan so unit tests of the
    // registry see built-ins + customs only.
    registry.ensure_player_species();
    const size_t species_count = registry.size();

    // Built-in fallback palettes (order matches MobType: zombie, skeleton,
    // cow, pig); customs get a stable name-derived palette.
    entries_.resize(species_count);
    if (species_count >= 4) {
        entries_[0].zombie_arms = true;
        entries_[2].quadruped = true;
        entries_[3].quadruped = true;
        entries_[0].col_body = {0.10f, 0.22f, 0.45f};
        entries_[0].col_head = {0.20f, 0.52f, 0.28f};
        entries_[0].col_limb = {0.12f, 0.14f, 0.34f};
        entries_[1].col_body = {0.62f, 0.63f, 0.60f};
        entries_[1].col_head = {0.88f, 0.89f, 0.86f};
        entries_[1].col_limb = {0.80f, 0.81f, 0.78f};
        entries_[2].col_body = {0.42f, 0.27f, 0.16f};
        entries_[2].col_head = {0.30f, 0.19f, 0.11f};
        entries_[2].col_limb = {0.25f, 0.16f, 0.10f};
        entries_[3].col_body = {0.95f, 0.60f, 0.64f};
        entries_[3].col_head = {0.98f, 0.70f, 0.72f};
        entries_[3].col_limb = {0.82f, 0.50f, 0.54f};
    }

    // Phase 1: decode every texture into CPU memory (a GL_TEXTURE_2D_ARRAY
    // must be allocated once with its final depth — re-allocating per layer
    // would wipe earlier layers). Textures may be any size; the array is
    // allocated at the largest and smaller textures are resampled to match.
    struct LoadedTex {
        std::vector<stbi_uc> pixels;
        int w = 0, h = 0;
        int layer = -1;
    };
    std::vector<LoadedTex> texs(species_count);
    int total_layers = 0;
    for (size_t i = 0; i < species_count; ++i) {
        const MobSpec* spec = registry.by_id(static_cast<uint8_t>(i));
        if (!spec) continue;
        entries_[i].name = spec->name;
        entries_[i].scale = spec->scale;
        entries_[i].quadruped = spec->quadruped;
        entries_[i].zombie_arms = spec->zombie_arms;
        if (spec->name == "player") {
            // Remote players: cyan shirt, skin-tone head, denim limbs — reads
            // as a person next to the mob palettes without a texture asset.
            entries_[i].col_body = {0.16f, 0.62f, 0.66f};
            entries_[i].col_head = {0.87f, 0.66f, 0.48f};
            entries_[i].col_limb = {0.20f, 0.28f, 0.55f};
        } else if (i >= 4) {
            fallback_palette(spec->name, entries_[i].col_body, entries_[i].col_head,
                             entries_[i].col_limb);
        }

        std::string error;
        auto geo = GeoModel::load_from_file(spec->model_path, &error);
        if (!geo) {
            MC_LOG_WARN("MobRenderer: no model for {} ({}) — procedural fallback",
                        spec->name, error);
            continue;
        }
        std::vector<stbi_uc> pixels;
        int w = 0, h = 0;
        if (!spec->texture_path.empty()) {
            int comp = 0;
            stbi_uc* p = stbi_load(spec->texture_path.c_str(), &w, &h, &comp, 4);
            if (p) {
                pixels.assign(p, p + static_cast<size_t>(w) * h * 4);
                stbi_image_free(p);
            } else {
                MC_LOG_WARN("MobRenderer: texture {} failed to decode — procedural fallback",
                            spec->texture_path);
            }
        }
        if (pixels.empty() && !geo->embedded_png.empty()) {
            int comp = 0;
            stbi_uc* p = stbi_load_from_memory(geo->embedded_png.data(),
                                               static_cast<int>(geo->embedded_png.size()),
                                               &w, &h, &comp, 4);
            if (p) {
                pixels.assign(p, p + static_cast<size_t>(w) * h * 4);
                stbi_image_free(p);
                MC_LOG_INFO("MobRenderer: using texture embedded in {}", spec->model_path);
            } else {
                MC_LOG_WARN("MobRenderer: embedded texture of {} failed to decode",
                            spec->name);
            }
        }
        if (pixels.empty()) {
            MC_LOG_WARN("MobRenderer: model {} missing texture — procedural fallback",
                        spec->name);
            continue;
        }
        LoadedTex& t = texs[i];
        t.pixels = std::move(pixels);
        t.w = w;
        t.h = h;
        t.layer = total_layers++;
        entries_[i].geo = std::move(geo);
        entries_[i].texture_layer = t.layer;

        // Optional Blockbench animation file next to the model.
        {
            std::filesystem::path model(spec->model_path);
            std::error_code aec;
            std::filesystem::path anim_path =
                model.parent_path() / (spec->name + ".animation.json");
            if (std::filesystem::exists(anim_path, aec)) {
                std::string aerr;
                std::vector<GeoAnimation> anims;
                int idle_anim = -1;
                int walk_anim = -1;
                if (GeoAnimation::load_from_file(anim_path.string(), anims, &aerr)) {
                    for (size_t a = 0; a < anims.size(); ++a) {
                        const std::string& n = anims[a].name;
                        if (idle_anim < 0 && n.find("idle") != std::string::npos)
                            idle_anim = static_cast<int>(a);
                        if (walk_anim < 0 && n.find("walk") != std::string::npos)
                            walk_anim = static_cast<int>(a);
                    }
                    if (idle_anim < 0 && walk_anim < 0 && !anims.empty()) idle_anim = 0;
                    if (walk_anim < 0) walk_anim = idle_anim;
                    entries_[i].animations = std::move(anims);
                    entries_[i].idle_anim = idle_anim;
                    entries_[i].walk_anim = walk_anim;
                    MC_LOG_INFO("MobRenderer: {} animations for {} ({} loaded)",
                                entries_[i].idle_anim >= 0 ? "idle" : "walk",
                                spec->name, entries_[i].animations.size());
                } else {
                    MC_LOG_WARN("MobRenderer: bad animation file {}: {}",
                                anim_path.string(), aerr);
                }
            }
        }
        MC_LOG_INFO("MobRenderer: loaded Blockbench model for {} ({} bones)",
                    spec->name, entries_[i].geo->bones.size());
    }

    if (total_layers > 0) {
        int max_w = 64, max_h = 64;
        for (const auto& t : texs) {
            if (t.layer < 0) continue;
            max_w = std::max(max_w, t.w);
            max_h = std::max(max_h, t.h);
        }
        glGenTextures(1, &tex_array_);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex_array_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, max_w, max_h, total_layers, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        std::vector<stbi_uc> scaled;
        for (size_t i = 0; i < species_count; ++i) {
            if (texs[i].layer < 0) continue;
            if (texs[i].w != max_w || texs[i].h != max_h) {
                resample_rgba_nearest(texs[i].pixels.data(), texs[i].w, texs[i].h, scaled,
                                      max_w, max_h);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, texs[i].layer, max_w, max_h, 1,
                                GL_RGBA, GL_UNSIGNED_BYTE, scaled.data());
            } else {
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, texs[i].layer, max_w, max_h, 1,
                                GL_RGBA, GL_UNSIGNED_BYTE, texs[i].pixels.data());
            }
        }
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        layer_count_ = total_layers;
        array_w_ = max_w;
        array_h_ = max_h;
    }

    batch_.reserve(1 << 15);
    return true;
}

void MobRenderer::shutdown() {
    shader_.destroy();
    shadow_shader_.destroy();
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (tex_array_) glDeleteTextures(1, &tex_array_);
    vao_ = vbo_ = tex_array_ = 0;
    entries_.clear();
}

// ------------------------------------------------------------------ drawing

namespace {

// Unit cube corners (centered) and the 6 CCW faces seen from outside.
const glm::vec3 kCorners[8] = {
    {-0.5f, -0.5f, -0.5f}, {+0.5f, -0.5f, -0.5f}, {+0.5f, +0.5f, -0.5f},
    {-0.5f, +0.5f, -0.5f}, {-0.5f, -0.5f, +0.5f}, {+0.5f, -0.5f, +0.5f},
    {+0.5f, +0.5f, +0.5f}, {-0.5f, +0.5f, +0.5f},
};
const int kFaces[6][4] = {
    {5, 4, 7, 6}, // +Z south
    {1, 0, 3, 2}, // -Z north
    {4, 0, 1, 5}, // -Y down
    {6, 7, 3, 2}, // +Y up
    {0, 4, 7, 3}, // -X west
    {1, 5, 6, 2}, // +X east
};
constexpr int kTri[6] = {0, 1, 2, 0, 2, 3};

} // namespace

void MobRenderer::emit_cube(std::vector<Vertex>& out, const glm::mat4& transform,
                            const UvRect* rects, int layer, float light,
                            const glm::vec3& tint) const {
    for (int f = 0; f < 6; ++f) {
        float u00 = 0.0f, v00 = 0.0f, u11 = 1.0f, v11 = 1.0f;
        if (rects) {
            const UvRect& r = rects[f];
            u00 = r.u; v00 = r.v; u11 = r.u + r.w; v11 = r.v + r.h;
        }
        glm::vec2 quad_uv[4] = {{u00, v11}, {u11, v11}, {u11, v00}, {u00, v00}};
        const int* idx = kFaces[f];
        for (int k = 0; k < 6; ++k) {
            const glm::vec3& p = kCorners[idx[kTri[k]]];
            Vertex v{};
            v.pos = glm::vec3(transform * glm::vec4(p, 1.0f));
            v.uv = quad_uv[k];
            v.layer = static_cast<float>(layer);
            v.light = light;
            v.r = static_cast<uint8_t>(std::min(1.0f, tint.r) * 255.0f);
            v.g = static_cast<uint8_t>(std::min(1.0f, tint.g) * 255.0f);
            v.b = static_cast<uint8_t>(std::min(1.0f, tint.b) * 255.0f);
            v.a = 255;
            out.push_back(v);
        }
    }
}

const MobRenderer::ModelEntry& MobRenderer::entry_for(MobType type) const {
    int i = static_cast<int>(type);
    if (i < 0 || i >= static_cast<int>(entries_.size())) i = 0;
    return entries_[i];
}

// Auto-animation: rotate a bone according to its classified role.
static float anim_rot(GeoAnim anim, bool zombie_arms, const rig::Pose& pose,
                      float quadruped_gait) {
    float chop = -pose.attack * 1.2f;
    float swing = pose.walk_swing * 0.7f;
    switch (anim) {
        case GeoAnim::Head: return pose.head_pitch;
        case GeoAnim::ArmLeft: return -swing + chop + (zombie_arms ? -1.35f : 0.0f);
        case GeoAnim::ArmRight: return swing + chop + (zombie_arms ? -1.35f : 0.0f);
        case GeoAnim::LegLeft: return -pose.walk_swing;
        case GeoAnim::LegRight: return pose.walk_swing;
        case GeoAnim::LegFL: return rig::gait_leg(quadruped_gait, true);
        case GeoAnim::LegBR: return rig::gait_leg(quadruped_gait, true);
        case GeoAnim::LegFR: return rig::gait_leg(quadruped_gait, false);
        case GeoAnim::LegBL: return rig::gait_leg(quadruped_gait, false);
        case GeoAnim::None: break;
    }
    return 0.0f;
}

void MobRenderer::collect_mob_geometry(const std::vector<Mob>& mobs, float time,
                                       float sky_light, bool apply_hurt) {
    batch_.clear();
    bone_mats_.clear();
    for (const auto& mob : mobs) {
        if (!mob.alive) continue;
        const ModelEntry& entry = entry_for(mob.type);

        bool moving = std::sqrt(mob.velocity.x * mob.velocity.x +
                                mob.velocity.z * mob.velocity.z) > 0.01f;
        rig::Pose pose;
        pose.walk_swing = rig::leg_swing(rig::walk_phase(time, moving));
        pose.attack = mob.attack_anim;
        pose.head_pitch = mob.pitch;
        pose.hurt = mob.hurt_time;

        // Shading: ambient sky light + hurt flash mixed toward red.
        float light = std::clamp(0.35f + 0.65f * sky_light, 0.0f, 1.0f);
        float hurt_k = apply_hurt ? std::clamp(pose.hurt * 0.8f, 0.0f, 0.8f) : 0.0f;

        glm::mat4 root = glm::translate(glm::mat4(1.0f),
                                        glm::vec3(mob.pos.x, mob.pos.y, mob.pos.z));
        root = glm::rotate(root, glm::radians(mob.yaw), glm::vec3(0, 1, 0));
        if (entry.scale != 1.0f) root = glm::scale(root, glm::vec3(entry.scale));

        if (entry.geo) {
            const GeoModel& model = *entry.geo;
            bone_mats_.assign(model.bones.size(), root);
            float quad_gait = pose.walk_swing / 0.45f * 0.35f; // quadruped amplitude

            // Active animation: walk while moving, idle otherwise (fallback
            // to whichever exists). Bones with rotation/position tracks get
            // their pose replaced by the sampled animation; the rest keep
            // the procedural rig.
            const GeoAnimation* active = nullptr;
            if (!entry.animations.empty()) {
                int idx = moving ? entry.walk_anim : entry.idle_anim;
                if (idx < 0) idx = entry.idle_anim >= 0 ? entry.idle_anim : entry.walk_anim;
                if (idx >= 0 && idx < static_cast<int>(entry.animations.size()))
                    active = &entry.animations[static_cast<size_t>(idx)];
            }
            const float anim_len = active && active->length > 0.0f ? active->length : 1.0f;
            const float anim_t = std::fmod(time, anim_len);

            // Parents appear before children in practice; a second sweep makes
            // arbitrary order safe. Bone pivots are ABSOLUTE model-space
            // coordinates (Bedrock semantics), so a child translates by the
            // DIFFERENCE to its parent's pivot — composing the parent's full
            // translation again would float the child's limbs.
            for (int pass = 0; pass < 2; ++pass) {
                for (size_t i = 0; i < model.bones.size(); ++i) {
                    const GeoBone& bone = model.bones[i];
                    glm::mat4 parent = bone.parent >= 0
                                           ? bone_mats_[static_cast<size_t>(bone.parent)]
                                           : root;
                    glm::vec3 base = bone.pivot;
                    if (bone.parent >= 0)
                        base -= model.bones[static_cast<size_t>(bone.parent)].pivot;

                    glm::vec3 rot_rad = glm::radians(bone.base_rot_deg);
                    rot_rad.x += anim_rot(bone.anim, entry.zombie_arms, pose, quad_gait);
                    const GeoBoneTrack* track = nullptr;
                    if (active) {
                        auto it = active->bones.find(bone.name);
                        if (it != active->bones.end()) track = &it->second;
                    }
                    if (track && track->has_rotation()) {
                        // Animation replaces the procedural pose for this bone.
                        rot_rad = glm::radians(GeoBoneTrack::sample(
                            track->rotation, anim_t, anim_len, bone.base_rot_deg));
                    }
                    if (track && track->has_position()) {
                        base += GeoBoneTrack::sample(track->position, anim_t, anim_len,
                                                     glm::vec3(0.0f));
                    }

                    glm::mat4 m = glm::translate(parent, base * kUnit);
                    if (rot_rad.x != 0.0f) m = glm::rotate(m, rot_rad.x, glm::vec3(1, 0, 0));
                    if (rot_rad.y != 0.0f) m = glm::rotate(m, rot_rad.y, glm::vec3(0, 1, 0));
                    if (rot_rad.z != 0.0f) m = glm::rotate(m, rot_rad.z, glm::vec3(0, 0, 1));
                    bone_mats_[i] = m;
                }
            }

            for (size_t i = 0; i < model.bones.size(); ++i) {
                const GeoBone& bone = model.bones[i];
                for (const auto& cube : bone.cubes) {
                    glm::vec3 center = cube.origin + cube.size * 0.5f - bone.pivot;
                    glm::vec3 sized = cube.size + glm::vec3(cube.inflate * 2.0f);
                    glm::mat4 t;
                    if (cube.rotated) {
                        // Rotate around the cube's own pivot, then place the
                        // (possibly inflated) box so its center lands where
                        // the unrotated center would be.
                        glm::vec3 pivot_rel = cube.rot_pivot - bone.pivot;
                        t = glm::translate(bone_mats_[i], pivot_rel * kUnit);
                        t = glm::rotate(t, glm::radians(cube.rot_deg.x), glm::vec3(1, 0, 0));
                        t = glm::rotate(t, glm::radians(cube.rot_deg.y), glm::vec3(0, 1, 0));
                        t = glm::rotate(t, glm::radians(cube.rot_deg.z), glm::vec3(0, 0, 1));
                        t = glm::translate(t, (center - pivot_rel) * kUnit);
                    } else {
                        t = glm::translate(bone_mats_[i], center * kUnit);
                    }
                    t = glm::scale(t, sized * kUnit);

                    UvRect rects[6];
                    if (cube.per_face) {
                        per_face_rects(cube, model.tex_w, model.tex_h, rects);
                    } else {
                        box_uv_rects(cube, model.tex_w, model.tex_h, rects);
                    }

                    glm::vec3 white{1.0f};
                    glm::vec3 tint = white;
                    tint.g *= (1.0f - hurt_k);
                    tint.b *= (1.0f - hurt_k);
                    tint.r += (1.0f - tint.r) * hurt_k;
                    emit_cube(batch_, t, rects, entry.texture_layer, light, tint);
                }
            }
        } else {
            // Procedural fallback: the original box rig, tinted per bone.
            auto tint_of = [&](const glm::vec3& c) {
                glm::vec3 t = c;
                t.g *= (1.0f - hurt_k);
                t.b *= (1.0f - hurt_k);
                t.r += (1.0f - t.r) * hurt_k;
                return t;
            };
            auto emit_bone = [&](const rig::BoneDef& def, float rot_x,
                                 const glm::vec3& col) {
                glm::mat4 m = rig::bone_matrix(def, root, rot_x);
                emit_cube(batch_, m, nullptr, -1, light, tint_of(col));
            };

            if (entry.quadruped) {
                const rig::BoneDef body{{0, 0.55f, 0}, {0, 0.25f, 0}, {0.55f, 0.5f, 0.9f}};
                const rig::BoneDef head{{0, 0.95f, 0.45f}, {0, 0.18f, 0.22f}, {0.35f, 0.35f, 0.35f}};
                const rig::BoneDef legFL{{-0.18f, 0.55f, 0.32f}, {0, -0.28f, 0}, {0.18f, 0.56f, 0.18f}};
                const rig::BoneDef legFR{{0.18f, 0.55f, 0.32f}, {0, -0.28f, 0}, {0.18f, 0.56f, 0.18f}};
                const rig::BoneDef legBL{{-0.18f, 0.55f, -0.32f}, {0, -0.28f, 0}, {0.18f, 0.56f, 0.18f}};
                const rig::BoneDef legBR{{0.18f, 0.55f, -0.32f}, {0, -0.28f, 0}, {0.18f, 0.56f, 0.18f}};
                float g = pose.walk_swing / 0.45f * 0.35f;
                emit_bone(body, 0, entry.col_body);
                emit_bone(head, pose.head_pitch, entry.col_head);
                emit_bone(legFL, rig::gait_leg(g, true), entry.col_limb);
                emit_bone(legBR, rig::gait_leg(g, true), entry.col_limb);
                emit_bone(legFR, rig::gait_leg(g, false), entry.col_limb);
                emit_bone(legBL, rig::gait_leg(g, false), entry.col_limb);
            } else {
                const rig::BoneDef torso{{0, 1.15f, 0}, {0, -0.35f, 0}, {0.5f, 0.7f, 0.3f}};
                const rig::BoneDef head{{0, 1.2f, 0}, {0, 0.25f, 0}, {0.45f, 0.45f, 0.45f}};
                const rig::BoneDef armL{{-0.35f, 1.12f, 0}, {0, -0.33f, 0}, {0.18f, 0.66f, 0.18f},
                                        entry.zombie_arms ? -1.35f : 0.0f};
                const rig::BoneDef armR{{0.35f, 1.12f, 0}, {0, -0.33f, 0}, {0.18f, 0.66f, 0.18f},
                                        entry.zombie_arms ? -1.35f : 0.0f};
                const rig::BoneDef legL{{-0.15f, 0.62f, 0}, {0, -0.31f, 0}, {0.2f, 0.62f, 0.2f}};
                const rig::BoneDef legR{{0.15f, 0.62f, 0}, {0, -0.31f, 0}, {0.2f, 0.62f, 0.2f}};
                float swing = pose.walk_swing * 0.7f;
                float chop = rig::arm_swing(0.0f, 0.0f, pose.attack);
                emit_bone(torso, 0, entry.col_body);
                emit_bone(head, pose.head_pitch, entry.col_head);
                emit_bone(armL, -swing + chop, entry.col_limb);
                emit_bone(armR, swing + chop, entry.col_limb);
                emit_bone(legL, -pose.walk_swing, entry.col_limb);
                emit_bone(legR, pose.walk_swing, entry.col_limb);
            }
        }
    }

}

void MobRenderer::draw(const Camera& camera, const std::vector<Mob>& mobs, float time,
                       float sky_brightness, const TextureAtlas& atlas) {
    if (mobs.empty()) return;

    collect_mob_geometry(mobs, time, sky_brightness, true);
    if (batch_.empty()) return;

    glm::mat4 vp = camera.projection() * camera.view();
    shader_.use();
    shader_.set_mat4("u_vp", glm::value_ptr(vp));
    shader_.set_float("u_ambient", std::clamp(0.32f + 0.68f * sky_brightness, 0.0f, 1.0f));
    // Match the post-pass fog band so mobs sink into the horizon like terrain.
    shader_.set_vec3("u_fog_color", 0.55f * std::max(sky_brightness, 0.25f),
                     0.68f * std::max(sky_brightness, 0.25f),
                     0.90f * std::max(sky_brightness, 0.25f));
    shader_.set_float("u_fog_near", 75.0f);
    shader_.set_float("u_fog_far", 140.0f);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch_.size() * sizeof(Vertex)),
                 batch_.data(), GL_STREAM_DRAW);

    shader_.set_int("u_tex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_array_);

    // Draw without culling (box UVs on thin bones can wind either way), then
    // restore the chunk pipeline's state — the water pass relies on culling.
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch_.size()));
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glBindVertexArray(0);
    // The chunk passes assume units 0..3 still hold the terrain atlas.
    atlas.bind_all(0);
}

void MobRenderer::draw_depth(const glm::mat4& light_space_matrix,
                             const std::vector<Mob>& mobs, float time) {
    if (mobs.empty()) return;
    collect_mob_geometry(mobs, time, 1.0f, false);
    if (batch_.empty()) return;

    shadow_shader_.use();
    shadow_shader_.set_mat4("u_light_space_matrix", glm::value_ptr(light_space_matrix));

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch_.size() * sizeof(Vertex)),
                 batch_.data(), GL_STREAM_DRAW);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch_.size()));
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT); // the shadow pass runs with front-face culling
    glBindVertexArray(0);
}

} // namespace mc
