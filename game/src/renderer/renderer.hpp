#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>

#include <GLFW/glfw3.h>

#include "core/types.hpp"
#include "renderer/camera.hpp"
#include "renderer/chunk_mesh.hpp"
#include "renderer/mob_renderer.hpp"
#include "renderer/shader.hpp"
#include "gameplay/item.hpp"
#include "renderer/texture_atlas.hpp"

#include "gameplay/entity.hpp"

namespace mc {

class ItemIcons;

// OpenGL renderer facade (PHASE3). Owns the window, GL context, shader
// program, texture atlas, and the per-chunk GPU meshes. The gameplay layer
// drives mesh uploads; the renderer draws the visible set each frame.
class Renderer {
public:
    bool init(int width, int height, std::string_view title);
    void shutdown();

    [[nodiscard]] bool should_close() const;
    void poll_events();
    void begin_frame(const Camera& camera);
    void end_frame();
    void present(); // Just swap buffers for menus

    // Upload (or replace) a chunk's mesh built by a worker.
    void upload_mesh(ChunkPos pos, const ChunkMeshData& data);
    void remove_mesh(ChunkPos pos);
    void clear_meshes();
    [[nodiscard]] bool has_mesh(ChunkPos pos) const;

    // Draw all visible chunk meshes (frustum-culled).
    void render_opaque(const Camera& camera);
    void render_transparent(const Camera& camera);
    void render_mobs(const Camera& camera, const std::vector<Mob>& mobs, float time);
    // Mobs (and their animation time) to render into the shadow map during
    // the next render_opaque call. Pass nullptr to disable. The pointed
    // vector must stay alive until render_opaque returns.
    void set_shadow_casters(const std::vector<Mob>* mobs, float time);
    void draw_projectiles(const Camera& camera, const std::vector<Projectile>& projectiles);

    [[nodiscard]] GLFWwindow* window() const { return window_; }
    [[nodiscard]] TextureAtlas& atlas() { return atlas_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    size_t debug_mesh_count() const { return meshes_.size(); }

    // Save current frame to a BMP file (reads front/back buffer).
    bool save_screen_bmp(const std::string& path) const;

    void set_sky_brightness(float b) { sky_brightness_ = b; }
    // GPU identifier captured once at init (perf reports / automation).
    [[nodiscard]] const std::string& gpu_name() const { return gpu_name_; }

    // OptiFine-style render scale: world FBOs render at scale * window size,
    // then the post pass upscales to the full window (UI stays native).
    // 0.5 = quarter fragment cost for every fragment-bound effect.
    void set_render_scale(float s) {
        render_scale_ = std::clamp(s, 0.4f, 1.0f);
        fbo_dims_dirty_ = true;
    }
    [[nodiscard]] float render_scale() const { return render_scale_; }
    [[nodiscard]] int fbo_width() const { return fbo_w_; }
    [[nodiscard]] int fbo_height() const { return fbo_h_; }

    // GPU-side pass timings (GL timer queries, one frame lag; ms, last value).
    enum class GpuSection : uint8_t { Shadow, Opaque, Sky, Transparent, Post, Count };
    [[nodiscard]] float gpu_ms(GpuSection s) const { return gpu_ms_[static_cast<size_t>(s)]; }

    // Mesh stats for perf tooling: total indices across loaded chunk meshes
    // and how many chunks the last shadow pass actually drew.
    [[nodiscard]] uint64_t total_mesh_indices() const;
    [[nodiscard]] int last_shadow_chunks() const { return last_shadow_chunks_; }
    // Weather gray-out: 0 = clear, 1 = full storm. Sky/fog colors mix toward
    // flat grey-blue in addition to the brightness scale, so rain reads as
    // overcast rather than a slightly dimmer sunny day.
    void set_weather_darkness(float d) { weather_darkness_ = d; }
    // Mining crack overlay: draw destroy-stage cracks on the block being mined
    // (progress 0..1 picks the stage tile; progress <= 0 disables the overlay).
    void set_mining_overlay(BlockPos pos, float progress) {
        mining_pos_ = pos;
        mining_progress_ = progress;
    }
    void set_wireframe(bool on);
    // Graphics quality presets (low/medium/high). Controls shadow map size,
    // shadow chunk radius, soft-PCF taps, POM distance, SSR and clouds.
    enum class QualityPreset { Low, Medium, High };
    void set_quality(QualityPreset q);
    QualityPreset quality() const { return quality_; }
    void set_held_block(BlockId b) { held_block_ = b; }
    // Item (non-block) currently held, rendered as an extruded icon sprite.
    // Set to ITEM_AIR when a block or nothing is held.
    void set_held_item(ItemId id) { held_item_ = id; }
    // Item icon atlas used to texture the held sprite. Not owned.
    void set_item_icons(const ItemIcons* icons) { item_icons_ = icons; }
    void set_hand_swing(float s) { hand_swing_ = s; }
    void set_hand_bobbing(float walk_dist, float bob_amp) {
        hand_walk_dist_ = walk_dist;
        hand_bob_amp_ = bob_amp;
    }
    void set_fog_mode(int mode, float density) { fog_mode_ = mode; fog_density_ = density; }
    void set_sun_angle(float angle) { sun_angle_ = angle; }
    void set_underwater(bool u) { underwater_ = u; }
    void set_vsync(bool on);
    void set_shadows_enabled(bool on) { shadows_enabled_ = on; }
    [[nodiscard]] bool shadows_enabled() const { return shadows_enabled_; }
    void set_fullscreen(bool on);
    [[nodiscard]] bool is_fullscreen() const { return fullscreen_; }
    void draw_selection_box(const Camera& camera, BlockPos pos);

private:
    void draw_pass(const Camera& camera, bool transparent);
    void init_sky();
    void draw_sky(const Camera& camera);
    void init_hand();
    void draw_hand();
    void draw_crosshair();
    void init_line_drawing();
    void init_post_process();
    void resize_fbos(int w, int h);

    GLFWwindow* window_ = nullptr;
    Shader shader_;
    Shader sky_shader_;
    Shader hand_shader_;
    Shader line_shader_;
    Shader shadow_shader_;
    Shader post_shader_;
    
    uint32_t main_fbo_ = 0;
    uint32_t opaque_fbo_ = 0; // used for blitting
    uint32_t main_color_tex_ = 0;
    uint32_t main_depth_tex_ = 0;
    uint32_t opaque_color_tex_ = 0;
    uint32_t opaque_depth_tex_ = 0;

    uint32_t bloom_fbo_[2] = {0, 0};
    uint32_t bloom_tex_[2] = {0, 0};

    uint32_t shadow_fbo_ = 0;
    uint32_t shadow_depth_map_ = 0;
    int shadow_map_size_ = 4096;
    float shadow_radius_ = 128.0f;
    float clouds_ = 1.0f;    // quality preset: cloud layer on/off
    float pom_dist_ = 20.0f; // quality preset: POM active distance
    float ssr_ = 1.0f;       // quality preset: water SSR on/off
    float shadow_soft_ = 1.0f; // quality preset: soft-PCF taps on/off
    QualityPreset quality_ = QualityPreset::High;
    glm::mat4 light_space_matrix_;
    
    uint32_t quad_vao_ = 0;
    uint32_t quad_vbo_ = 0;
    uint32_t sky_vao_ = 0;
    uint32_t sky_vbo_ = 0;
    uint32_t hand_vao_ = 0;
    uint32_t hand_vbo_ = 0;
    uint32_t mob_vao_ = 0;
    uint32_t mob_vbo_ = 0;
    uint32_t sun_vao_ = 0;
    uint32_t sun_vbo_ = 0;
    uint32_t sun_ebo_ = 0;
    uint32_t line_vao_ = 0;
    uint32_t line_vbo_ = 0;
    uint32_t crosshair_vao_ = 0;
    uint32_t crosshair_vbo_ = 0;
    void init_mob_renderer();
    void init_sun_quad();
    
    glm::vec3 camera_pos_;
    glm::mat4 view_proj_;
    glm::mat4 inv_view_proj_;
    glm::mat4 proj_;
    glm::mat4 view_;
    glm::mat4 inv_proj_;
    glm::mat4 inv_view_;
    glm::vec3 sun_dir_;
    
    TextureAtlas atlas_;
    MobRenderer mob_renderer_;
    const std::vector<Mob>* shadow_casters_ = nullptr;
    float shadow_casters_time_ = 0.0f;
    std::unordered_map<ChunkPos, ChunkMesh> meshes_;
    std::vector<std::pair<float, ChunkPos>> visible_cache_; // reused per frame
    int width_ = 0;
    int height_ = 0;
    float sky_brightness_ = 1.0f;
    std::string gpu_name_;
    float render_scale_ = 1.0f;
    int fbo_w_ = 0;
    int fbo_h_ = 0;
    bool fbo_dims_dirty_ = true;

    struct GpuSlot {
        // Ring of queries (shadow pass runs every N frames, so results lag
        // multiple frames; poll the whole ring, keep the freshest value).
        unsigned int q[4] = {0, 0, 0, 0};
        uint32_t head = 0;
        float last_ms = 0.0f;
    };
    std::array<GpuSlot, static_cast<size_t>(GpuSection::Count)> gpu_slots_{};
    std::array<float, static_cast<size_t>(GpuSection::Count)> gpu_ms_{};
    int last_shadow_chunks_ = 0;
    // Shadow-map update throttling: the sun and terrain move slowly, so the
    // depth map is re-rendered only every N frames (or immediately after a
    // large camera jump). light_space_matrix_ stays paired with the map.
    // Half-extent (blocks) of the shadow ortho box around its center. Small
    // extent = fewer chunk draws per refresh = smaller frame-time spike; the
    // 4096² map over ~96 blocks is plenty of texel density.
    float shadow_extent_ = 48.0f;
    // Smooth shadow updates: the depth map is a persistent target redrawn
    // incrementally (max shadow_draw_budget_ chunks per frame, cycling
    // through the candidate list). A full clear+restart happens only when
    // the quantized sun direction or the camera anchor moves materially —
    // the map then refills over the next few frames. This keeps per-frame
    // driver draw cost bounded (AMD GL: ~0.2 ms/draw) so no frame spikes.
    std::vector<ChunkPos> shadow_queue_;
    bool shadow_cycle_active_ = false;
    int shadow_draw_budget_ = 64;
    float last_shadow_sun_angle_ = 1e9f;
    glm::vec3 last_shadow_anchor_ = glm::vec3(0.0f);
    glm::vec3 last_shadow_camera_ = glm::vec3(0.0f);
    void gpu_begin(GpuSection s);
    void gpu_end(GpuSection s);
    float weather_darkness_ = 0.0f;
    float sun_angle_ = 0.0f;
    int fog_mode_ = 0;
    float fog_density_ = 0.0f;
    BlockId held_block_ = BLOCK_AIR;
    ItemId held_item_ = ITEM_AIR;   // 0 = none (block or bare arm path)
    const ItemIcons* item_icons_ = nullptr;
    bool wireframe_ = false;
    float hand_swing_ = 0.0f;
    float hand_walk_dist_ = 0.0f;
    float hand_bob_amp_ = 0.0f;
    bool underwater_ = false;
    bool shadows_enabled_ = true;
    bool fullscreen_ = false;
    int windowed_x_ = 100, windowed_y_ = 100, windowed_w_ = 1280, windowed_h_ = 720;
    BlockPos mining_pos_{0, -1, 0};
    float mining_progress_ = 0.0f;
    void draw_mining_overlay(const Camera& camera);
};

} // namespace mc
