#include "renderer/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <glad/gl.h>

#include "core/config.hpp"
#include "core/logger.hpp"
#include "core/profiler.hpp"
#include "renderer/item_icons.hpp"
#include "renderer/mob_rig.hpp"
#include "world/block.hpp"

namespace mc {

struct HandVertex {
    float x, y, z;
    float u, v, w;
    float shade;
};

namespace {
void gl_debug_callback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*) {
    if (severity == GL_DEBUG_SEVERITY_HIGH || type == GL_DEBUG_TYPE_ERROR) {
        MC_LOG_WARN("GL: {}", message);
    }
}
} // namespace

bool Renderer::init(int width, int height, std::string_view title) {
    if (!glfwInit()) {
        MC_LOG_ERROR("Failed to init GLFW");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(width, height, std::string(title).c_str(), nullptr, nullptr);
    if (!window_) {
        MC_LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    // Procedural grass-block window icon (branding without a binary asset).
    {
        const int S = 32;
        static uint8_t icon_px[S * S * 4];
        for (int y = 0; y < S; ++y) {
            for (int x = 0; x < S; ++x) {
                bool edge = x == 0 || y == 0 || x == S - 1 || y == S - 1;
                uint8_t r, g, b;
                if (edge) {
                    r = 40; g = 26; b = 18;
                } else if (y < S * 2 / 5) {
                    int n = ((x * 7 + y * 13) % 5) - 2;
                    r = static_cast<uint8_t>(86 + n);
                    g = static_cast<uint8_t>(150 + n);
                    b = static_cast<uint8_t>(70 + n);
                } else {
                    int n = ((x * 11 + y * 5) % 5) - 2;
                    r = static_cast<uint8_t>(134 + n);
                    g = static_cast<uint8_t>(96 + n);
                    b = static_cast<uint8_t>(67 + n);
                }
                int i = (y * S + x) * 4;
                icon_px[i] = r; icon_px[i + 1] = g; icon_px[i + 2] = b; icon_px[i + 3] = 255;
            }
        }
        GLFWimage icon{S, S, icon_px};
        glfwSetWindowIcon(window_, 1, &icon);
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // VSync

    int version = gladLoadGL(glfwGetProcAddress);
    if (version == 0) {
        MC_LOG_ERROR("Failed to load OpenGL functions");
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
        return false;
    }
    MC_LOG_INFO("OpenGL {}.{} loaded", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(gl_debug_callback, nullptr);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    atlas_.generate();
    atlas_.upload();

    // Shaders are copied next to the binary (see src/CMakeLists.txt).
    if (!shader_.load_from_files("shaders/chunk.vert", "shaders/chunk.frag")) {
        MC_LOG_ERROR("Failed to load chunk shaders");
        shutdown();
        return false;
    }
    if (!sky_shader_.load_from_files("shaders/sky.vert", "shaders/sky.frag")) {
        MC_LOG_ERROR("Failed to load sky shaders");
        shutdown();
        return false;
    }
    if (!hand_shader_.load_from_files("shaders/hand.vert", "shaders/hand.frag")) {
        MC_LOG_ERROR("Failed to load hand shaders");
        shutdown();
        return false;
    }
    if (!line_shader_.load_from_files("shaders/line.vert", "shaders/line.frag")) {
        MC_LOG_ERROR("Failed to load line shaders");
        shutdown();
        return false;
    }
    if (!shadow_shader_.load_from_files("shaders/shadow.vert", "shaders/shadow.frag")) {
        MC_LOG_ERROR("Failed to load shadow shaders");
        shutdown();
        return false;
    }
    if (!post_shader_.load_from_files("shaders/post.vert", "shaders/post.frag")) {
        MC_LOG_ERROR("Failed to load post shaders");
        shutdown();
        return false;
    }
    
    glGenFramebuffers(1, &shadow_fbo_);
    glGenTextures(1, &shadow_depth_map_);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_map_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 4096, 4096, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    float border_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);

    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_depth_map_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    init_sky();
    init_hand();
    init_line_drawing();
    init_mob_renderer();
    mob_renderer_.init();
    init_post_process();

    glfwGetFramebufferSize(window_, &width_, &height_);
    return true;
}

void Renderer::shutdown() {
    for (auto& [pos, mesh] : meshes_) {
        mesh.opaque.destroy();
        mesh.transparent.destroy();
    }
    meshes_.clear();
    mob_renderer_.shutdown();
    shader_.destroy();
    sky_shader_.destroy();
    hand_shader_.destroy();
    line_shader_.destroy();
    if (sky_vao_) glDeleteVertexArrays(1, &sky_vao_);
    if (sky_vbo_) glDeleteBuffers(1, &sky_vbo_);
    if (hand_vao_) glDeleteVertexArrays(1, &hand_vao_);
    if (hand_vbo_) glDeleteBuffers(1, &hand_vbo_);
    if (line_vao_) glDeleteVertexArrays(1, &line_vao_);
    if (crosshair_vao_) glDeleteVertexArrays(1, &crosshair_vao_);
    if (crosshair_vbo_) glDeleteBuffers(1, &crosshair_vbo_);
    if (line_vbo_) glDeleteBuffers(1, &line_vbo_);
    if (sun_vao_) glDeleteVertexArrays(1, &sun_vao_);
    if (sun_vbo_) glDeleteBuffers(1, &sun_vbo_);
    if (sun_ebo_) glDeleteBuffers(1, &sun_ebo_);
    if (mob_vao_) glDeleteVertexArrays(1, &mob_vao_);
    if (mob_vbo_) glDeleteBuffers(1, &mob_vbo_);
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
    }
}

void Renderer::set_vsync(bool on) {
    glfwSwapInterval(on ? 1 : 0);
}

void Renderer::set_fullscreen(bool on) {
    if (fullscreen_ == on) return;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) return;
    if (on) {
        glfwGetWindowPos(window_, &windowed_x_, &windowed_y_);
        glfwGetWindowSize(window_, &windowed_w_, &windowed_h_);
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(window_, nullptr, windowed_x_, windowed_y_, windowed_w_, windowed_h_, 0);
    }
    fullscreen_ = on;
}

bool Renderer::should_close() const { return glfwWindowShouldClose(window_); }

void Renderer::poll_events() { glfwPollEvents(); }

void Renderer::begin_frame(const Camera& camera) {
    float sx = std::cos(sun_angle_);
    float sy = std::sin(sun_angle_);
    float sz = 0.3f;
    sun_dir_ = glm::normalize(glm::vec3(sx, sy, sz));
    camera_pos_ = camera.position;
    proj_ = camera.projection();
    view_ = camera.view();
    inv_proj_ = glm::inverse(proj_);
    inv_view_ = glm::inverse(view_);
    view_proj_ = camera.view_projection();
    inv_view_proj_ = glm::inverse(view_proj_);

    // ---- SHADOW PASS ----
    if (shadows_enabled_) {
        glm::mat4 light_proj = glm::ortho(-64.0f, 64.0f, -64.0f, 64.0f, 1.0f, 300.0f);
        glm::vec3 light_center = camera.position + camera.forward() * 24.0f;
        glm::vec3 light_pos = light_center + sun_dir_ * 128.0f;
        glm::mat4 light_view = glm::lookAt(light_pos, light_center, glm::vec3(0.0f, 1.0f, 0.0f));
        light_space_matrix_ = light_proj * light_view;

        glViewport(0, 0, shadow_map_size_, shadow_map_size_);
        glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
        glClear(GL_DEPTH_BUFFER_BIT);
        shadow_shader_.use();
        shadow_shader_.set_mat4("u_light_space_matrix", glm::value_ptr(light_space_matrix_));

        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT); // Fix peter panning
        for (const auto& [pos, mesh] : meshes_) {
            float dist = glm::distance(glm::vec2(camera.position.x, camera.position.z), glm::vec2(pos.x * 16.0f, pos.z * 16.0f));
            if (dist < shadow_radius_) { // Render chunks within radius to shadow map
                mesh.opaque.draw();
            }
        }
        // Mobs cast shadows on the terrain too.
        if (shadow_casters_ != nullptr) {
            mob_renderer_.draw_depth(light_space_matrix_, *shadow_casters_,
                                     shadow_casters_time_);
        }
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ---- MAIN PASS ----
    glfwGetFramebufferSize(window_, &width_, &height_);
    static int last_w = 0, last_h = 0;
    if (width_ != last_w || height_ != last_h) {
        resize_fbos(width_, height_);
        last_w = width_;
        last_h = height_;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, main_fbo_);
    glViewport(0, 0, width_, height_);
    glClearColor(0.62f, 0.80f, 0.96f, 1.0f); // sky blue
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    shader_.use();
    shader_.set_mat4("u_view_proj", glm::value_ptr(view_proj_));
    shader_.set_float("u_sky_brightness", sky_brightness_);
    
    shader_.set_float("u_time", static_cast<float>(glfwGetTime()));
    shader_.set_vec3("u_camera_pos", camera_pos_.x, camera_pos_.y, camera_pos_.z);
    
    shader_.set_vec3("u_sun_dir", sun_dir_.x, sun_dir_.y, sun_dir_.z);
    shader_.set_mat4("u_light_space_matrix", glm::value_ptr(light_space_matrix_));
    shader_.set_vec2("u_near_far", camera.near_plane, camera.far_plane);
    
    // Bind 4 PBR texture arrays to slots 0, 1, 2, 3
    atlas_.bind_all(0);
    shader_.set_int("u_albedo_map", 0);
    shader_.set_int("u_normal_map", 1);
    shader_.set_int("u_specular_map", 2);
    shader_.set_int("u_height_map", 3);
    
    // Bind shadow map to texture unit 4
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_map_);
    shader_.set_int("u_shadow_map", 4);
    
    // Same sky/fog colors the skybox uses — the water Fresnel path must
    // reflect the actual sky, not a hardcoded tint. Day zenith is a deeper
    // saturated blue than the pale horizon so terrain silhouettes pop.
    glm::vec3 chunk_sky = glm::vec3(0.36f, 0.56f, 0.94f) * sky_brightness_;
    glm::vec3 chunk_fog = glm::vec3(0.60f, 0.77f, 0.95f) * sky_brightness_;
    shader_.set_vec3("u_sky_color", chunk_sky.x, chunk_sky.y, chunk_sky.z);
    shader_.set_vec3("u_fog_color", chunk_fog.x, chunk_fog.y, chunk_fog.z);
    shader_.set_float("u_fog_start", 110.0f);
    shader_.set_float("u_fog_end", static_cast<float>(camera.far_plane * 1.25f));
    shader_.set_float("u_fog_density", fog_density_);
    shader_.set_int("u_fog_mode", fog_mode_);
    shader_.set_float("u_shadows_on", shadows_enabled_ ? 1.0f : 0.0f);
    shader_.set_float("u_shadow_soft", shadow_soft_);
    // Texel size follows the quality-preset shadow map resolution (the old
    // shader hardcoded 1/4096 and smeared at 2048).
    shader_.set_float("u_shadow_texel", 1.0f / static_cast<float>(shadow_map_size_));
    shader_.set_float("u_pom_dist", pom_dist_);
    shader_.set_float("u_ssr", ssr_);
}

void Renderer::end_frame() { 
    // ---- POST PROCESSING PASS ----
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    post_shader_.use();
    post_shader_.set_vec3("u_sun_dir", sun_dir_.x, sun_dir_.y, sun_dir_.z);
    // Same horizon color the sky shader uses — distant water fogs toward it,
    // so any mismatch seams the ocean against the sky.
    glm::vec3 post_fog = glm::vec3(0.60f, 0.77f, 0.95f) * sky_brightness_;
    post_shader_.set_vec3("u_fog_color", post_fog.x, post_fog.y, post_fog.z);
    post_shader_.set_mat4("u_view_proj", glm::value_ptr(view_proj_));
    post_shader_.set_mat4("u_inv_view_proj", glm::value_ptr(inv_view_proj_));
    post_shader_.set_mat4("u_proj", glm::value_ptr(proj_));
    post_shader_.set_mat4("u_view", glm::value_ptr(view_));
    post_shader_.set_mat4("u_inv_proj", glm::value_ptr(inv_proj_));
    post_shader_.set_mat4("u_inv_view", glm::value_ptr(inv_view_));
    post_shader_.set_vec3("u_camera_pos", camera_pos_.x, camera_pos_.y, camera_pos_.z);
    post_shader_.set_mat4("u_light_space_matrix", glm::value_ptr(light_space_matrix_));
    post_shader_.set_vec2("u_resolution", static_cast<float>(width_), static_cast<float>(height_));
    post_shader_.set_float("u_time", static_cast<float>(glfwGetTime()));
    post_shader_.set_float("u_is_underwater", underwater_ ? 1.0f : 0.0f);
    post_shader_.set_float("u_shadows_on", shadows_enabled_ ? 1.0f : 0.0f);
    post_shader_.set_float("u_sky_brightness", sky_brightness_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, main_color_tex_);
    post_shader_.set_int("u_color_tex", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, main_depth_tex_);
    post_shader_.set_int("u_depth_tex", 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadow_depth_map_);
    post_shader_.set_int("u_shadow_map", 2);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, bloom_tex_[0]);
    post_shader_.set_int("u_bloom_tex0", 3);

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, bloom_tex_[1]);
    post_shader_.set_int("u_bloom_tex1", 4);

    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer::present() {
    glfwSwapBuffers(window_);
}

void Renderer::upload_mesh(ChunkPos pos, const ChunkMeshData& data) {
    ChunkMesh& mesh = meshes_[pos];
    mesh.opaque.upload(data.opaque_verts, data.opaque_idx);
    mesh.transparent.upload(data.trans_verts, data.trans_idx);
    mesh.uploaded = true;
}

void Renderer::remove_mesh(ChunkPos pos) {
    auto it = meshes_.find(pos);
    if (it == meshes_.end()) return;
    it->second.opaque.destroy();
    it->second.transparent.destroy();
    meshes_.erase(it);
}

void Renderer::clear_meshes() {
    for (auto& [pos, mesh] : meshes_) {
        mesh.opaque.destroy();
        mesh.transparent.destroy();
    }
    meshes_.clear();
    visible_cache_.clear();
}

bool Renderer::has_mesh(ChunkPos pos) const { return meshes_.contains(pos); }

void Renderer::render_opaque(const Camera& camera) {
    ZoneScoped;
    draw_sky(camera);
    draw_pass(camera, false); // opaque, front-to-back
    draw_mining_overlay(camera);

    // Blit main FBO to opaque FBO for effects requiring opaque-only data (like Refraction)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, main_fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, opaque_fbo_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, main_fbo_); // Bind back to main FBO for transparent pass
}

// Destroy-stage cracks on the block being mined, drawn slightly inflated so
// the overlay never z-fights with the block's own faces.
void Renderer::draw_mining_overlay(const Camera& camera) {
    if (mining_progress_ <= 0.0f || mining_progress_ >= 1.0f) return;
    if (mining_pos_.y < 0) return;

    int stage = std::clamp(static_cast<int>(mining_progress_ * 10.0f), 0, 9);
    Tile tile = static_cast<Tile>(static_cast<int>(Tile::DestroyStage0) + stage);

    glm::mat4 model = glm::translate(glm::mat4(1.0f),
        glm::vec3(mining_pos_.x + 0.5f, mining_pos_.y + 0.5f, mining_pos_.z + 0.5f));
    model = glm::scale(model, glm::vec3(1.004f));
    glm::mat4 vp = camera.projection() * camera.view();
    glm::mat4 mvp = vp * model;

    hand_shader_.use();
    hand_shader_.set_mat4("u_mvp", glm::value_ptr(mvp));
    hand_shader_.set_float("u_brightness", 1.0f);

    const float s = 0.5f;
    const glm::vec3 corners[8] = {
        {-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},{-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s}};
    const int faces[6][4] = {
        {5,4,7,6},{1,0,3,2},{6,7,3,2},{4,0,1,5},{0,4,7,3},{1,5,6,2}};
    float w = static_cast<float>(tile);

    HandVertex verts[36];
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        const int* q = faces[f];
        glm::vec2 uv[4] = {{0,1},{1,1},{1,0},{0,0}};
        const int tris[6] = {0,1,2,0,2,3};
        for (int k = 0; k < 6; ++k) {
            const glm::vec3& p = corners[q[tris[k]]];
            verts[vi++] = {p.x, p.y, p.z, uv[tris[k]].x, uv[tris[k]].y, w, 1.0f};
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(hand_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hand_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    atlas_.bind(0);
    hand_shader_.set_int("u_atlas", 0);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::render_transparent(const Camera& camera) {
    ZoneScoped;
    shader_.use();
    
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, opaque_color_tex_);
    shader_.set_int("u_opaque_color", 5);
    
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, opaque_depth_tex_);
    shader_.set_int("u_opaque_depth", 6);
    
    shader_.set_vec2("u_resolution", static_cast<float>(width_), static_cast<float>(height_));

    draw_pass(camera, true);  // transparent, back-to-front
    draw_hand();
    draw_crosshair();
}

void Renderer::draw_pass(const Camera& camera, bool transparent) {
    shader_.use();
    if (transparent) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
    }

    auto& visible = visible_cache_;
    visible.clear();
    visible.reserve(meshes_.size());
    for (const auto& [pos, mesh] : meshes_) {
        Vec3 min_pos(pos.x * CHUNK_SIZE, MIN_Y, pos.z * CHUNK_SIZE);
        Vec3 max_pos(pos.x * CHUNK_SIZE + CHUNK_SIZE, MAX_Y, pos.z * CHUNK_SIZE + CHUNK_SIZE);
        if (!camera.aabb_in_frustum(min_pos, max_pos)) continue;
        const GpuMesh& g = transparent ? mesh.transparent : mesh.opaque;
        if (g.index_count == 0) continue;
        Vec3 center(pos.x * CHUNK_SIZE + 8.0f, (MIN_Y + MAX_Y) * 0.5f, pos.z * CHUNK_SIZE + 8.0f);
        Vec3 to_c = center - camera.position;
        float dist = to_c.x * to_c.x + to_c.y * to_c.y + to_c.z * to_c.z;
        visible.emplace_back(dist, pos);
    }
    if (visible.empty()) {
        if (transparent) { glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDepthFunc(GL_LESS); }
        return;
    }

    if (transparent) {
        std::sort(visible.begin(), visible.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
    } else {
        std::sort(visible.begin(), visible.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    for (const auto& [dist, pos] : visible) {
        auto it = meshes_.find(pos);
        if (it == meshes_.end()) continue;
        const ChunkMesh& mesh = it->second;
        if (transparent) {
            if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            mesh.transparent.draw();
            if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        } else {
            if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            mesh.opaque.draw();
            if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }

    if (transparent) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDepthFunc(GL_LESS);
    }
}

void Renderer::resize_fbos(int w, int h) {
    if (w == 0 || h == 0) return;
    
    auto create_tex = [](uint32_t& tex, GLenum internal_fmt, GLenum format, GLenum type, int width, int height) {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, width, height, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    };

    create_tex(main_color_tex_, GL_RGBA16F, GL_RGBA, GL_FLOAT, w, h);
    create_tex(main_depth_tex_, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, w, h);
    
    create_tex(opaque_color_tex_, GL_RGBA16F, GL_RGBA, GL_FLOAT, w, h);
    create_tex(opaque_depth_tex_, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, w, h);

    if (!main_fbo_) glGenFramebuffers(1, &main_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, main_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_color_tex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, main_depth_tex_, 0);

    if (!opaque_fbo_) glGenFramebuffers(1, &opaque_fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, opaque_fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, opaque_color_tex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, opaque_depth_tex_, 0);

    // Bloom ping-pong (half res)
    int bw = w / 2;
    int bh = h / 2;
    for (int i = 0; i < 2; ++i) {
        create_tex(bloom_tex_[i], GL_RGBA16F, GL_RGBA, GL_FLOAT, bw, bh);
        if (!bloom_fbo_[i]) glGenFramebuffers(1, &bloom_fbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom_tex_[i], 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::init_post_process() {
    float quad_verts[] = {
        // pos        uv
        -1.0f,  1.0f, 0.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f
    };
    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);
    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), &quad_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
}

bool Renderer::save_screen_bmp(const std::string& path) const {
    const int w = width_;
    const int h = height_;
    std::vector<uint8_t> pixels(static_cast<std::size_t>(w * h * 3));
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glReadBuffer(GL_BACK);

    // BMP needs BGR and padding each row to 4 bytes.
    int row_size = w * 3;
    int pad = (4 - row_size % 4) % 4;
    int data_size = (row_size + pad) * h;
    int file_size = 14 + 40 + data_size;

    std::vector<uint8_t> bmp(file_size);
    int off = 0;

    // BITMAPFILEHEADER (14 bytes)
    bmp[off++] = 'B'; bmp[off++] = 'M';
    bmp[off++] = static_cast<uint8_t>(file_size & 0xFF);
    bmp[off++] = static_cast<uint8_t>((file_size >> 8) & 0xFF);
    bmp[off++] = static_cast<uint8_t>((file_size >> 16) & 0xFF);
    bmp[off++] = static_cast<uint8_t>((file_size >> 24) & 0xFF);
    off += 4; // reserved
    bmp[off++] = 54; bmp[off++] = 0; bmp[off++] = 0; bmp[off++] = 0; // offset

    // BITMAPINFOHEADER (40 bytes)
    bmp[off++] = 40; bmp[off++] = 0; bmp[off++] = 0; bmp[off++] = 0; // header size
    bmp[off++] = static_cast<uint8_t>(w & 0xFF);
    bmp[off++] = static_cast<uint8_t>((w >> 8) & 0xFF);
    bmp[off++] = static_cast<uint8_t>((w >> 16) & 0xFF);
    bmp[off++] = static_cast<uint8_t>((w >> 24) & 0xFF);
    bmp[off++] = static_cast<uint8_t>(h & 0xFF);
    bmp[off++] = static_cast<uint8_t>((h >> 8) & 0xFF);
    bmp[off++] = static_cast<uint8_t>((h >> 16) & 0xFF);
    bmp[off++] = static_cast<uint8_t>((h >> 24) & 0xFF);
    bmp[off++] = 1; bmp[off++] = 0; // planes
    bmp[off++] = 24; bmp[off++] = 0; // bit count
    off += 24; // compression=0, size=0, res=0, colors=0

    // Pixel data (bottom-up, BGR, padded)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int src = (y * w + x) * 3;
            // OpenGL gives RGB, BMP expects BGR
            bmp[off++] = pixels[src + 2]; // B
            bmp[off++] = pixels[src + 1]; // G
            bmp[off++] = pixels[src + 0]; // R
        }
        for (int p = 0; p < pad; ++p) bmp[off++] = 0;
    }

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(bmp.data(), 1, bmp.size(), f);
    fclose(f);
    return true;
}

void Renderer::set_wireframe(bool on) {
    wireframe_ = on;
    // Note: applied per-draw above (glPolygonMode).
}

// Graphics quality presets. Shadow map reallocates via glTexImage2D (the FBO
// attachment follows the texture object); everything else is uniform-driven.
void Renderer::set_quality(QualityPreset q) {
    quality_ = q;
    int map_size;
    switch (q) {
        case QualityPreset::Low:
            map_size = 2048;
            shadow_radius_ = 72.0f;
            clouds_ = 0.0f;
            pom_dist_ = 0.0f;
            ssr_ = 0.0f;
            shadow_soft_ = 0.0f;
            break;
        case QualityPreset::Medium:
            map_size = 2048;
            shadow_radius_ = 96.0f;
            clouds_ = 1.0f;
            pom_dist_ = 12.0f;
            ssr_ = 1.0f;
            shadow_soft_ = 1.0f;
            break;
        default:
            map_size = 4096;
            shadow_radius_ = 128.0f;
            clouds_ = 1.0f;
            pom_dist_ = 20.0f;
            ssr_ = 1.0f;
            shadow_soft_ = 1.0f;
            break;
    }
    if (map_size != shadow_map_size_) {
        shadow_map_size_ = map_size;
        glBindTexture(GL_TEXTURE_2D, shadow_depth_map_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, map_size, map_size,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    MC_LOG_INFO("Quality preset: {} (shadow map {}, radius {})",
                q == QualityPreset::Low ? "low" : q == QualityPreset::Medium ? "medium" : "high",
                shadow_map_size_, shadow_radius_);
}

void Renderer::init_hand() {
    glGenVertexArrays(1, &hand_vao_);
    glGenBuffers(1, &hand_vbo_);

    glBindVertexArray(hand_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hand_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 36 * sizeof(HandVertex), nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HandVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(HandVertex), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(HandVertex), (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
}

void Renderer::draw_hand() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); // screen-space rects wind CW under the y-flip
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    hand_shader_.use();

    float aspect = static_cast<float>(width_) / static_cast<float>(height_ ? height_ : 1);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
    
    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f * aspect, -0.6f, -1.8f));
    view = glm::rotate(view, glm::radians(25.0f), glm::vec3(1, 0, 0));
    view = glm::rotate(view, glm::radians(-45.0f), glm::vec3(0, 1, 0));

    if (hand_swing_ > 0.0f) {
        float swing_angle = std::sin(hand_swing_ * 3.14159f);
        view = glm::rotate(view, glm::radians(-60.0f * swing_angle), glm::vec3(1, 0, 0));
        view = glm::rotate(view, glm::radians(20.0f * swing_angle), glm::vec3(0, 1, 0));
        view = glm::translate(view, glm::vec3(0.0f, -0.2f * swing_angle, 0.2f * swing_angle));
    }

    if (hand_bob_amp_ > 0.0f) {
        float bob_x = std::sin(hand_walk_dist_ * 3.14159f) * hand_bob_amp_ * 0.1f;
        float bob_y = std::abs(std::cos(hand_walk_dist_ * 3.14159f)) * hand_bob_amp_ * 0.1f - (hand_bob_amp_ * 0.05f);
        float bob_rot_z = std::sin(hand_walk_dist_ * 3.14159f) * hand_bob_amp_ * 5.0f;
        float bob_rot_x = std::abs(std::cos(hand_walk_dist_ * 3.14159f)) * hand_bob_amp_ * 5.0f;
        view = glm::translate(view, glm::vec3(bob_x, bob_y, 0.0f));
        view = glm::rotate(view, glm::radians(bob_rot_z), glm::vec3(0, 0, 1));
        view = glm::rotate(view, glm::radians(bob_rot_x), glm::vec3(1, 0, 0));
    }

    const bool item_mode = held_item_ != ITEM_AIR && item_icons_ != nullptr;
    glm::mat4 model;
    if (item_mode) {
        // Held tool/item: extruded icon sprite, tilted like the classic
        // first-person item pose.
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(-0.12f, -0.12f, -0.1f));
        model = glm::rotate(model, glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(15.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(1.15f));
    } else if (held_block_ != BLOCK_AIR) {
        model = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, -0.5f, -0.5f));
    } else {
        model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(-0.2f, -1.0f, -0.5f));
        model = glm::scale(model, glm::vec3(0.4f, 1.2f, 0.4f));
    }
    glm::mat4 mvp = proj * view * model;

    hand_shader_.set_mat4("u_mvp", glm::value_ptr(mvp));
    hand_shader_.set_float("u_brightness", sky_brightness_);
    hand_shader_.set_float("u_use_icon", item_mode ? 1.0f : 0.0f);

    HandVertex verts[36];
    int v_idx = 0;

    if (item_mode) {
        // Extruded icon sprite: front + back + 4 border strips (36 verts).
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        item_icons_->uv_for(held_item_, u0, v0, u1, v1);
        const float s = 0.22f;   // sprite half-size
        const float t = 0.05f;   // extrusion thickness (half)
        // Corners are (x, y, z, u, v).
        auto quad = [&](const float a[5], const float b[5], const float c[5],
                        const float d[5], float shade) {
            verts[v_idx++] = {a[0], a[1], a[2], a[3], a[4], 0.0f, shade};
            verts[v_idx++] = {b[0], b[1], b[2], b[3], b[4], 0.0f, shade};
            verts[v_idx++] = {c[0], c[1], c[2], c[3], c[4], 0.0f, shade};
            verts[v_idx++] = {a[0], a[1], a[2], a[3], a[4], 0.0f, shade};
            verts[v_idx++] = {c[0], c[1], c[2], c[3], c[4], 0.0f, shade};
            verts[v_idx++] = {d[0], d[1], d[2], d[3], d[4], 0.0f, shade};
        };
        // Front (+Z): full icon, v flipped so the icon's top row sits at the
        // top edge (the icons atlas is not row-flipped on upload).
        const float ftl[5] = {-s,  s,  t, u0, v0}, ftr[5] = { s,  s,  t, u1, v0};
        const float fbr[5] = { s, -s,  t, u1, v1}, fbl[5] = {-s, -s,  t, u0, v1};
        const float btl[5] = {-s,  s, -t, u0, v0}, btr[5] = { s,  s, -t, u1, v0};
        const float bbr[5] = { s, -s, -t, u1, v1}, bbl[5] = {-s, -s, -t, u0, v1};
        quad(ftl, ftr, fbr, fbl, 1.0f);            // front
        quad(btr, btl, bbl, bbr, 0.55f);           // back (dimmer)
        quad(btl, ftl, ftr, btr, 0.8f);            // top edge (v = v0 row)
        quad(bbl, fbl, fbr, bbr, 0.55f);           // bottom edge (v = v1 row)
        quad(btl, bbl, fbl, ftl, 0.7f);            // left edge (u = u0 column)
        quad(ftr, fbr, bbr, btr, 0.7f);            // right edge (u = u1 column)
        glBindVertexArray(hand_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, hand_vbo_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        item_icons_->bind(0);
        hand_shader_.set_int("u_icon", 0);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        return;
    }
    
    auto add_face = [&](Tile tile, const std::vector<glm::vec3>& pos, float shade) {
        float uv00 = 0.0f, v00 = 1.0f;
        float uv10 = 1.0f, v10 = 1.0f;
        float uv11 = 1.0f, v11 = 0.0f;
        float uv01 = 0.0f, v01 = 0.0f;
        float w = static_cast<float>(tile);
        
        verts[v_idx++] = {pos[0].x, pos[0].y, pos[0].z, uv00, v00, w, shade};
        verts[v_idx++] = {pos[1].x, pos[1].y, pos[1].z, uv10, v10, w, shade};
        verts[v_idx++] = {pos[2].x, pos[2].y, pos[2].z, uv11, v11, w, shade};
        
        verts[v_idx++] = {pos[0].x, pos[0].y, pos[0].z, uv00, v00, w, shade};
        verts[v_idx++] = {pos[2].x, pos[2].y, pos[2].z, uv11, v11, w, shade};
        verts[v_idx++] = {pos[3].x, pos[3].y, pos[3].z, uv01, v01, w, shade};
    };
    
    const float s = 0.25f;
    mc::BlockId target_block = held_block_;
    // Bare arm: skin tile, not a random block.
    Tile hand_tiles[6] = {Tile::Hand, Tile::Hand, Tile::Hand,
                          Tile::Hand, Tile::Hand, Tile::Hand};
    Tile block_tiles[6] = {
        tile_for_face(target_block, Direction::North),
        tile_for_face(target_block, Direction::South),
        tile_for_face(target_block, Direction::Up),
        tile_for_face(target_block, Direction::Down),
        tile_for_face(target_block, Direction::West),
        tile_for_face(target_block, Direction::East),
    };
    Tile* tiles = (held_block_ != mc::BLOCK_AIR) ? block_tiles : hand_tiles;

    // CCW when viewed from outside the block
    // North (-Z)
    add_face(tiles[0], {{s,0,0}, {0,0,0}, {0,s,0}, {s,s,0}}, 0.8f);
    // South (+Z)
    add_face(tiles[1], {{0,0,s}, {s,0,s}, {s,s,s}, {0,s,s}}, 0.8f);
    // Up (+Y)
    add_face(tiles[2], {{0,s,s}, {s,s,s}, {s,s,0}, {0,s,0}}, 1.0f);
    // Down (-Y)
    add_face(tiles[3], {{0,0,0}, {s,0,0}, {s,0,s}, {0,0,s}}, 0.5f);
    // West (-X)
    add_face(tiles[4], {{0,0,0}, {0,0,s}, {0,s,s}, {0,s,0}}, 0.8f);
    // East (+X)
    add_face(tiles[5], {{s,0,s}, {s,0,0}, {s,s,0}, {s,s,s}}, 0.8f);

    glBindVertexArray(hand_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, hand_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    atlas_.bind(0);
    hand_shader_.set_int("u_atlas", 0);

    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::init_sky() {
    float skybox_verts[] = {
        // positions (size 1.0)
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };
    
    glGenVertexArrays(1, &sky_vao_);
    glGenBuffers(1, &sky_vbo_);
    glBindVertexArray(sky_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, sky_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skybox_verts), &skybox_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Renderer::draw_sky(const Camera& camera) {
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    
    sky_shader_.use();
    
    // Remove translation from the view matrix
    glm::mat4 view = glm::mat4(glm::mat3(camera.view()));
    glm::mat4 projection = camera.projection();
    glm::mat4 vp_no_trans = projection * view;
    
    sky_shader_.set_mat4("u_view_proj_no_translation", glm::value_ptr(vp_no_trans));
    
    // Sky color matches day/night, fog matches horizon
    // Deep day zenith, paler horizon (see main-pass comment on chunk_sky).
    glm::vec3 sky_color = glm::vec3(0.36f, 0.56f, 0.94f) * sky_brightness_;
    glm::vec3 fog_color = glm::vec3(0.60f, 0.77f, 0.95f) * sky_brightness_;
    
    sky_shader_.set_vec3("u_sky_color", sky_color.x, sky_color.y, sky_color.z);
    sky_shader_.set_vec3("u_fog_color", fog_color.x, fog_color.y, fog_color.z);
    sky_shader_.set_vec3("u_sun_dir", sun_dir_.x, sun_dir_.y, sun_dir_.z);
    sky_shader_.set_vec2("u_resolution", static_cast<float>(width_), static_cast<float>(height_));
    sky_shader_.set_float("u_time", static_cast<float>(glfwGetTime()));
    sky_shader_.set_float("u_clouds", clouds_);
    sky_shader_.set_vec3("u_sun_dir", sun_dir_.x, sun_dir_.y, sun_dir_.z);
    
    glBindVertexArray(sky_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    // The sky shader draws a soft round sun disc from u_sun_dir — no billboard.
    glDisable(GL_BLEND);
    glUseProgram(0);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
}

void Renderer::init_sun_quad() {
    float w = static_cast<float>(Tile::Glowstone);
    float sun_verts[] = {
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, w, 1.0f,
         0.5f, -0.5f, 0.0f,  1.0f, 0.0f, w, 1.0f,
         0.5f,  0.5f, 0.0f,  1.0f, 1.0f, w, 1.0f,
        -0.5f,  0.5f, 0.0f,  0.0f, 1.0f, w, 1.0f,
    };
    uint32_t sun_idx[] = {0, 1, 2, 0, 2, 3};

    glGenVertexArrays(1, &sun_vao_);
    glGenBuffers(1, &sun_vbo_);
    glGenBuffers(1, &sun_ebo_);
    glBindVertexArray(sun_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, sun_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sun_verts), sun_verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sun_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(sun_idx), sun_idx, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(6 * sizeof(float)));
    glBindVertexArray(0);
}

void Renderer::init_line_drawing() {
    float vertices[] = {
        // Bottom face
        0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,  1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f,

        // Top face
        0.0f, 1.0f, 0.0f,  1.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f,  0.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 1.0f,  0.0f, 1.0f, 0.0f,

        // Vertical edges
        0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,
        1.0f, 0.0f, 1.0f,  1.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 1.0f
    };

    glGenVertexArrays(1, &crosshair_vao_);
    glGenBuffers(1, &crosshair_vbo_);
    glBindVertexArray(crosshair_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, crosshair_vbo_);
    glBufferData(GL_ARRAY_BUFFER, 24 * 3 * sizeof(float), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glGenVertexArrays(1, &line_vao_);
    glGenBuffers(1, &line_vbo_);
    glBindVertexArray(line_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, line_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Renderer::draw_selection_box(const Camera& camera, BlockPos pos) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);

    line_shader_.use();
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x - 0.001f, pos.y - 0.001f, pos.z - 0.001f));
    model = glm::scale(model, glm::vec3(1.002f));
    glm::mat4 mvp = camera.projection() * camera.view() * model;

    line_shader_.set_mat4("u_mvp", glm::value_ptr(mvp));
    line_shader_.set_vec4("u_color", 0.0f, 0.0f, 0.0f, 0.4f);

    glBindVertexArray(line_vao_);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
}

void Renderer::draw_crosshair() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); // screen-space rects wind CW under the y-flip
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    line_shader_.use();

    glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(width_),
                                static_cast<float>(height_), 0.0f, -1.0f, 1.0f);
    line_shader_.set_mat4("u_mvp", glm::value_ptr(proj));
    line_shader_.set_vec4("u_color", 1.0f, 1.0f, 1.0f, 1.0f);

    float cx = width_ * 0.5f;
    float cy = height_ * 0.5f;
    float L = 9.0f;  // half length
    float t = 1.5f;  // half thickness

    // Two rects (horizontal + vertical), shadow pass then white core.
    float verts[24 * 3];
    int vi = 0;
    auto push_rect = [&](float x0, float y0, float x1, float y1) {
        float q[4][3] = {{x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}};
        const int tris[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; ++k) {
            verts[vi++] = q[tris[k]][0];
            verts[vi++] = q[tris[k]][1];
            verts[vi++] = q[tris[k]][2];
        }
    };
    push_rect(cx - L + 1.0f, cy - t + 1.0f, cx + L + 1.0f, cy + t + 1.0f);
    push_rect(cx - t + 1.0f, cy - L + 1.0f, cx + t + 1.0f, cy + L + 1.0f);
    line_shader_.set_vec4("u_color", 0.0f, 0.0f, 0.0f, 0.55f);
    glBindVertexArray(crosshair_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, crosshair_vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 12);

    vi = 0;
    push_rect(cx - L, cy - t, cx + L, cy + t);
    push_rect(cx - t, cy - L, cx + t, cy + L);
    line_shader_.set_vec4("u_color", 1.0f, 1.0f, 1.0f, 0.9f);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 12);

    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::init_mob_renderer() {
    float cube_verts[] = {
        // Front
        -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,
        // Back
        -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
        // Left
        -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
        // Right
         0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
        // Top
        -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
        // Bottom
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f
    };

    glGenVertexArrays(1, &mob_vao_);
    glGenBuffers(1, &mob_vbo_);
    glBindVertexArray(mob_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, mob_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube_verts), cube_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Renderer::render_mobs(const Camera& camera, const std::vector<Mob>& mobs, float time) {
    mob_renderer_.draw(camera, mobs, time, sky_brightness_, atlas_);
}

void Renderer::set_shadow_casters(const std::vector<Mob>* mobs, float time) {
    shadow_casters_ = mobs;
    shadow_casters_time_ = time;
}

void Renderer::draw_projectiles(const Camera& camera, const std::vector<Projectile>& projectiles) {
    if (projectiles.empty()) return;

    line_shader_.use();
    glBindVertexArray(mob_vao_);

    glm::mat4 vp = camera.projection() * camera.view();
    for (const auto& p : projectiles) {
        glm::mat4 m = glm::translate(glm::mat4(1.0f),
                                     glm::vec3(p.pos.x, p.pos.y, p.pos.z));
        m = glm::scale(m, glm::vec3(0.12f, 0.12f, 0.35f)); // elongated: arrow shaft
        glm::mat4 mvp = vp * m;
        line_shader_.set_mat4("u_mvp", glm::value_ptr(mvp));
        line_shader_.set_vec4("u_color", 0.85f, 0.78f, 0.60f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindVertexArray(0);
}

} // namespace mc
