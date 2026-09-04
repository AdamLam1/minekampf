#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "world/block.hpp"

namespace mc {

// Multi-Layered PBR Texture Atlas.
// Manages 4 GL_TEXTURE_2D_ARRAY textures:
//  - Albedo (sRGB color + Alpha)
//  - Normal (Tangent-space normals for relief mapping)
//  - Specular (R: Roughness, G: Metalness, B: Pre-baked AO, A: Emissive)
//  - Height (Per-pixel height map for Parallax Occlusion Mapping)
class TextureAtlas {
public:
    static constexpr int TILE_PX = 16;
    static constexpr int MAX_TILES = 256;

    TextureAtlas();
    ~TextureAtlas();

    // Generate procedural HD fallback buffers for all tiles.
    void generate();

    // Upload all 4 texture arrays to the GPU. Requires active GL context.
    void upload();

    // Bind texture arrays starting from base_slot (slot 0: Albedo, 1: Normal, 2: Specular, 3: Height).
    void bind(uint32_t slot = 0) const;
    void bind_all(uint32_t base_slot = 0) const;

    [[nodiscard]] int num_layers() const { return num_layers_; }
    [[nodiscard]] int width() const { return TILE_PX; }
    [[nodiscard]] int height() const { return TILE_PX; }

    [[nodiscard]] uint32_t gl_albedo_texture() const { return gl_albedo_tex_; }
    [[nodiscard]] uint32_t gl_normal_texture() const { return gl_normal_tex_; }
    [[nodiscard]] uint32_t gl_specular_texture() const { return gl_specular_tex_; }
    [[nodiscard]] uint32_t gl_height_texture() const { return gl_height_tex_; }

    // CPU-side generated buffers (offline asset dumping / QA tools).
    // Each is num_layers() * TILE_PX * TILE_PX RGBA8 pixels.
    [[nodiscard]] const std::vector<uint8_t>& albedo_data() const { return albedo_pixels_; }
    [[nodiscard]] const std::vector<uint8_t>& normal_data() const { return normal_pixels_; }
    [[nodiscard]] const std::vector<uint8_t>& specular_data() const { return specular_pixels_; }
    [[nodiscard]] const std::vector<uint8_t>& height_data() const { return height_pixels_; }

private:
    void fill_tile(Tile tile, uint8_t r, uint8_t g, uint8_t b);
    void fill_tile_alpha(Tile tile, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void put_pixel(int tile_id, int px, int py, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void put_normal_pixel(int tile_id, int px, int py, uint8_t nx, uint8_t ny, uint8_t nz);
    void put_specular_pixel(int tile_id, int px, int py, uint8_t roughness, uint8_t metalness, uint8_t ao, uint8_t emissive);
    void put_height_pixel(int tile_id, int px, int py, uint8_t height);

    static uint8_t hash_shade(int x, int y, int seed);
    // Smooth (bilinear-interpolated) value noise in [-1, 1] — correlated
    // per-tile patterning instead of independent per-pixel jitter.
    static float value_noise(float x, float y, int seed);

    std::vector<uint8_t> albedo_pixels_;
    std::vector<uint8_t> normal_pixels_;
    std::vector<uint8_t> specular_pixels_;
    std::vector<uint8_t> height_pixels_;

    int num_layers_ = MAX_TILES;
    uint32_t gl_albedo_tex_ = 0;
    uint32_t gl_normal_tex_ = 0;
    uint32_t gl_specular_tex_ = 0;
    uint32_t gl_height_tex_ = 0;
};

} // namespace mc
