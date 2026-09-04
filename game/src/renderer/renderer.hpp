#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>

#include <GLFW/glfw3.h>

#include "core/types.hpp"
#include "renderer/camera.hpp"
#include "renderer/chunk_mesh.hpp"
#include "renderer/mob_renderer.hpp"
#include "renderer/shader.hpp"
#include "renderer/texture_atlas.hpp"

#include "gameplay/entity.hpp"

namespace mc {

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
    void draw_projectiles(const Camera& camera, const std::vector<Projectile>& projectiles);

    [[nodiscard]] GLFWwindow* window() const { return window_; }
    [[nodiscard]] TextureAtlas& atlas() { return atlas_; }
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    size_t debug_mesh_count() const { return meshes_.size(); }

    // Save current frame to a BMP file (reads front/back buffer).
    bool save_screen_bmp(const std::string& path) const;

    void set_sky_brightness(float b) { sky_brightness_ = b; }
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
    std::unordered_map<ChunkPos, ChunkMesh> meshes_;
    std::vector<std::pair<float, ChunkPos>> visible_cache_; // reused per frame
    int width_ = 0;
    int height_ = 0;
    float sky_brightness_ = 1.0f;
    float sun_angle_ = 0.0f;
    int fog_mode_ = 0;
    float fog_density_ = 0.0f;
    BlockId held_block_ = BLOCK_AIR;
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
