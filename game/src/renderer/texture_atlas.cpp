#include "renderer/texture_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

#include <glad/gl.h>

#include "third_party/stb_image.h"

namespace mc {

namespace {
struct RGB {
    uint8_t r, g, b;
};
} // namespace

TextureAtlas::TextureAtlas() {
    num_layers_ = MAX_TILES;
}

TextureAtlas::~TextureAtlas() {
    if (gl_albedo_tex_ != 0) glDeleteTextures(1, &gl_albedo_tex_);
    if (gl_normal_tex_ != 0) glDeleteTextures(1, &gl_normal_tex_);
    if (gl_specular_tex_ != 0) glDeleteTextures(1, &gl_specular_tex_);
    if (gl_height_tex_ != 0) glDeleteTextures(1, &gl_height_tex_);
    gl_albedo_tex_ = gl_normal_tex_ = gl_specular_tex_ = gl_height_tex_ = 0;
}

uint8_t TextureAtlas::hash_shade(int x, int y, int seed) {
    uint32_t h = static_cast<uint32_t>(x * 374761393u + y * 668265263u + seed * 2654435761u);
    h = (h ^ (h >> 13u)) * 1274126177u;
    return static_cast<uint8_t>((h >> 16u) & 0xFF);
}

float TextureAtlas::value_noise(float x, float y, int seed) {
    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    auto lattice = [seed](int lx, int ly) {
        return static_cast<float>(hash_shade(lx, ly, seed)) / 255.0f;
    };
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    float u = smooth(x - static_cast<float>(xi));
    float v = smooth(y - static_cast<float>(yi));
    float a = lattice(xi, yi) * (1.0f - u) + lattice(xi + 1, yi) * u;
    float b = lattice(xi, yi + 1) * (1.0f - u) + lattice(xi + 1, yi + 1) * u;
    return (a * (1.0f - v) + b * v) * 2.0f - 1.0f;
}

void TextureAtlas::put_pixel(int tile_id, int px, int py, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int idx = (tile_id * TILE_PX * TILE_PX + py * TILE_PX + px) * 4;
    albedo_pixels_[idx + 0] = r;
    albedo_pixels_[idx + 1] = g;
    albedo_pixels_[idx + 2] = b;
    albedo_pixels_[idx + 3] = a;
}

void TextureAtlas::put_normal_pixel(int tile_id, int px, int py, uint8_t nx, uint8_t ny, uint8_t nz) {
    int idx = (tile_id * TILE_PX * TILE_PX + py * TILE_PX + px) * 4;
    normal_pixels_[idx + 0] = nx;
    normal_pixels_[idx + 1] = ny;
    normal_pixels_[idx + 2] = nz;
    normal_pixels_[idx + 3] = 255;
}

void TextureAtlas::put_specular_pixel(int tile_id, int px, int py, uint8_t roughness, uint8_t metalness, uint8_t ao, uint8_t emissive) {
    int idx = (tile_id * TILE_PX * TILE_PX + py * TILE_PX + px) * 4;
    specular_pixels_[idx + 0] = roughness;
    specular_pixels_[idx + 1] = metalness;
    specular_pixels_[idx + 2] = ao;
    specular_pixels_[idx + 3] = emissive;
}

void TextureAtlas::put_height_pixel(int tile_id, int px, int py, uint8_t height) {
    int idx = (tile_id * TILE_PX * TILE_PX + py * TILE_PX + px) * 4;
    height_pixels_[idx + 0] = height;
    height_pixels_[idx + 1] = height;
    height_pixels_[idx + 2] = height;
    height_pixels_[idx + 3] = 255;
}

void TextureAtlas::fill_tile(Tile tile, uint8_t r, uint8_t g, uint8_t b) {
    fill_tile_alpha(tile, r, g, b, 255);
}

void TextureAtlas::fill_tile_alpha(Tile tile, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int ti = static_cast<int>(tile);
    for (int py = 0; py < TILE_PX; ++py) {
        for (int px = 0; px < TILE_PX; ++px) {
            // Correlated blotches (value noise) plus a faint per-pixel grain —
            // reads as natural surface instead of static noise.
            float blotch = value_noise(px / 4.5f, py / 4.5f, ti) * 7.0f;
            int grain = (hash_shade(px, py, ti) % 7) - 3;
            int v = static_cast<int>(blotch) + grain;
            uint8_t rr = static_cast<uint8_t>(std::clamp<int>(r + v, 0, 255));
            uint8_t gg = static_cast<uint8_t>(std::clamp<int>(g + v, 0, 255));
            uint8_t bb = static_cast<uint8_t>(std::clamp<int>(b + v, 0, 255));
            put_pixel(ti, px, py, rr, gg, bb, a);
        }
    }
}

void TextureAtlas::generate() {
    size_t total_bytes = static_cast<size_t>(num_layers_) * TILE_PX * TILE_PX * 4;
    albedo_pixels_.assign(total_bytes, 0);
    normal_pixels_.assign(total_bytes, 0);
    specular_pixels_.assign(total_bytes, 0);
    height_pixels_.assign(total_bytes, 0);

    for (int ti = 0; ti < num_layers_; ++ti) {
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                put_normal_pixel(ti, px, py, 128, 128, 255);
                put_specular_pixel(ti, px, py, 180, 0, 255, 0);
                put_height_pixel(ti, px, py, 128);
            }
        }
    }

    auto fill = [&](Tile t, RGB c) { fill_tile(t, c.r, c.g, c.b); };

    fill(Tile::Air, {0, 0, 0});
    fill(Tile::Stone, {130, 135, 145});          // Slate stone
    fill(Tile::Dirt, {140, 95, 65});             // Terracotta earth
    // Beach sand — pale, gently rippled; heavy saturation reads as mustard.
    {
        int ti = static_cast<int>(Tile::Sand);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 9) - 4;
                int ripple = ((py + (px / 3)) % 5 == 0) ? -5 : 0;
                put_pixel(ti, px, py, static_cast<uint8_t>(224 + v + ripple),
                          static_cast<uint8_t>(211 + v + ripple),
                          static_cast<uint8_t>(182 + v + ripple), 255);
            }
        }
    }
    fill(Tile::Gravel, {145, 140, 140});
    fill(Tile::Sandstone, {230, 210, 155});
    fill(Tile::SandstoneBottom, {195, 175, 130});
    fill_tile_alpha(Tile::Water, 48, 116, 198, 168); // Lake blue water
    fill(Tile::Lava, {245, 120, 25});
    fill(Tile::Bedrock, {50, 50, 55});
    fill(Tile::Glowstone, {240, 215, 115});
    fill_tile_alpha(Tile::Glass, 210, 240, 250, 90);
    fill(Tile::Snow, {245, 248, 255});
    fill_tile_alpha(Tile::Ice, 160, 215, 245, 180);
    fill(Tile::Clay, {165, 170, 185});
    fill(Tile::Terracotta, {160, 95, 75});
    fill(Tile::Obsidian, {45, 38, 70});

    // Emissive blocks: lava and glowstone glow via specular alpha channel.
    for (int py = 0; py < TILE_PX; ++py) {
        for (int px = 0; px < TILE_PX; ++px) {
            put_specular_pixel(static_cast<int>(Tile::Lava), px, py, 60, 0, 255, 255);
            put_specular_pixel(static_cast<int>(Tile::Glowstone), px, py, 90, 0, 255, 220);
        }
    }

    // 1. Painterly Fantasy Grass Top (Vibrant emerald mint palette)
    {
        int ti = static_cast<int>(Tile::GrassTop);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti) % 100;
                uint8_t r = 98, g = 158, b = 78; // meadow green
                if (s < 30) { r = 82; g = 138, b = 66; }        // shadow tuft
                else if (s > 75) { r = 118, g = 182, b = 96; }  // sunlight highlight
                
                // Add tiny stylized flowers (dandelions / daisies)
                if ((px == 4 && py == 5) || (px == 11 && py == 12)) { r = 250; g = 220; b = 80; } // Yellow dandelion
                if ((px == 8 && py == 2) || (px == 13 && py == 9)) { r = 245; g = 110; b = 140; } // Pink daisy

                put_pixel(ti, px, py, r, g, b, 255);
            }
        }
    }

    // 2. Painterly Grass Side (Terracotta dirt + lush emerald grass tufts)
    {
        int ti = static_cast<int>(Tile::GrassSide);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti) % 100;
                bool is_grass = (py >= 11);
                if (py == 10 && (px % 3 == 0 || px % 5 == 1)) is_grass = true;
                if (py == 9 && (px % 4 == 2)) is_grass = true;

                if (is_grass) {
                    uint8_t r = 98, g = 158, b = 78;
                    if (s < 30) { r = 82; g = 138; b = 66; }
                    else if (s > 75) { r = 118; g = 182; b = 96; }
                    put_pixel(ti, px, py, r, g, b, 255);
                } else {
                    // Terracotta dirt
                    int v = (s % 15) - 7;
                    uint8_t r = static_cast<uint8_t>(std::clamp<int>(140 + v, 0, 255));
                    uint8_t g = static_cast<uint8_t>(std::clamp<int>(95 + v, 0, 255));
                    uint8_t b = static_cast<uint8_t>(std::clamp<int>(65 + v, 0, 255));
                    put_pixel(ti, px, py, r, g, b, 255);
                }
            }
        }
    }

    // 3. Oak Wood Planks
    {
        int ti = static_cast<int>(Tile::Planks);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                bool seam = (py % 4 == 0) || ((px % 8 == 0) && (py / 4 % 2 == 0));
                int s = hash_shade(px, py, ti) % 16 - 8;
                if (seam) {
                    put_pixel(ti, px, py, 120, 80, 45, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(175 + s), static_cast<uint8_t>(130 + s), static_cast<uint8_t>(80 + s), 255);
                }
            }
        }
    }

    // 4. Painterly Foliage Leaves (Lush Deep Emerald Forest)
    {
        int ti = static_cast<int>(Tile::Leaves);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti) % 100;
                uint8_t r = static_cast<uint8_t>(50 + (s % 14));
                uint8_t g = static_cast<uint8_t>(110 + (s % 26));
                uint8_t b = static_cast<uint8_t>(46 + (s % 12));
                put_pixel(ti, px, py, r, g, b, 255);
            }
        }
    }

    // 4b. Acacia log + leaves: gray-brown bark, orange growth rings, olive
    // canopy (distinct from oak so savannas read differently).
    {
        int side = static_cast<int>(Tile::AcaciaLogSide);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, side);
                bool groove = (px % 5 == (s % 3));
                int v = (s % 18) - 9;
                if (groove) {
                    put_pixel(side, px, py, static_cast<uint8_t>(84 + v), static_cast<uint8_t>(78 + v), static_cast<uint8_t>(70 + v), 255);
                } else {
                    put_pixel(side, px, py, static_cast<uint8_t>(118 + v), static_cast<uint8_t>(110 + v), static_cast<uint8_t>(100 + v), 255);
                }
                put_height_pixel(side, px, py, groove ? 90 : 140);
            }
        }
        int top = static_cast<int>(Tile::AcaciaLogTop);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int dx = px - 8, dy = py - 8;
                int ring = static_cast<int>(std::sqrt(dx * dx + dy * dy)) % 3;
                int v = (hash_shade(px, py, top) % 12) - 6;
                uint8_t r = static_cast<uint8_t>(178 + ring * 14 + v);
                uint8_t g = static_cast<uint8_t>(116 + ring * 9 + v);
                uint8_t b = static_cast<uint8_t>(66 + ring * 5 + v);
                put_pixel(top, px, py, r, g, b, 255);
                put_height_pixel(top, px, py, static_cast<uint8_t>(150 - ring * 8));
            }
        }
        int lv = static_cast<int>(Tile::AcaciaLeaves);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, lv) % 100;
                uint8_t r = static_cast<uint8_t>(88 + (s % 16));
                uint8_t g = static_cast<uint8_t>(118 + (s % 24));
                uint8_t b = static_cast<uint8_t>(40 + (s % 12));
                put_pixel(lv, px, py, r, g, b, 255);
            }
        }
    }

    // 4c. Cherry log + leaves: dark mahogany bark, warm pink growth rings,
    // blossom-pink canopy (petal speckle in a rosy base).
    {
        int side = static_cast<int>(Tile::CherryLogSide);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, side);
                bool groove = (py % 5 == (s % 3));
                int v = (s % 14) - 7;
                if (groove) {
                    put_pixel(side, px, py, static_cast<uint8_t>(58 + v), static_cast<uint8_t>(36 + v), static_cast<uint8_t>(34 + v), 255);
                } else {
                    put_pixel(side, px, py, static_cast<uint8_t>(92 + v), static_cast<uint8_t>(58 + v), static_cast<uint8_t>(54 + v), 255);
                }
                put_height_pixel(side, px, py, groove ? 90 : 140);
            }
        }
        int top = static_cast<int>(Tile::CherryLogTop);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int dx = px - 8, dy = py - 8;
                int ring = static_cast<int>(std::sqrt(dx * dx + dy * dy)) % 3;
                int v = (hash_shade(px, py, top) % 12) - 6;
                put_pixel(top, px, py, static_cast<uint8_t>(198 + ring * 10 + v),
                          static_cast<uint8_t>(140 + ring * 8 + v),
                          static_cast<uint8_t>(120 + ring * 6 + v), 255);
                put_height_pixel(top, px, py, static_cast<uint8_t>(150 - ring * 8));
            }
        }
        int lv = static_cast<int>(Tile::CherryLeaves);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, lv) % 100;
                // Rosy base with lighter petal speckles; NOT biome-tinted in
                // the mesher, so the pink stays pink in every biome.
                uint8_t r, g, b;
                if (s < 30) {
                    r = static_cast<uint8_t>(232 + (s % 12));
                    g = static_cast<uint8_t>(168 + (s % 16));
                    b = static_cast<uint8_t>(196 + (s % 10));
                } else {
                    r = static_cast<uint8_t>(206 + (s % 14));
                    g = static_cast<uint8_t>(128 + (s % 18));
                    b = static_cast<uint8_t>(158 + (s % 12));
                }
                put_pixel(lv, px, py, r, g, b, 255);
            }
        }
    }

    // 4d. Jungle log + leaves: green-mossed grey bark, pale rings, deep
    // saturated canopy.
    {
        int side = static_cast<int>(Tile::JungleLogSide);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, side);
                bool moss = (s % 7) < 2;
                int v = (s % 16) - 8;
                if (moss) {
                    put_pixel(side, px, py, static_cast<uint8_t>(86 + v), static_cast<uint8_t>(112 + v), static_cast<uint8_t>(58 + v), 255);
                } else {
                    put_pixel(side, px, py, static_cast<uint8_t>(124 + v), static_cast<uint8_t>(104 + v), static_cast<uint8_t>(72 + v), 255);
                }
                put_height_pixel(side, px, py, moss ? 100 : 145);
            }
        }
        int top = static_cast<int>(Tile::JungleLogTop);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int dx = px - 8, dy = py - 8;
                int ring = static_cast<int>(std::sqrt(dx * dx + dy * dy)) % 3;
                int v = (hash_shade(px, py, top) % 12) - 6;
                put_pixel(top, px, py, static_cast<uint8_t>(196 + ring * 10 + v),
                          static_cast<uint8_t>(168 + ring * 8 + v),
                          static_cast<uint8_t>(118 + ring * 6 + v), 255);
                put_height_pixel(top, px, py, static_cast<uint8_t>(150 - ring * 8));
            }
        }
        int lv = static_cast<int>(Tile::JungleLeaves);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, lv) % 100;
                uint8_t r = static_cast<uint8_t>(44 + (s % 14));
                uint8_t g = static_cast<uint8_t>(118 + (s % 30));
                uint8_t b = static_cast<uint8_t>(38 + (s % 12));
                put_pixel(lv, px, py, r, g, b, 255);
            }
        }
    }

    // 5. Stylized Ores — slate base with shaped crystal veins (seeded blobs
    // grown over a noise-perturbed distance field, not scatter noise).
    auto ore = [&](Tile t, RGB core, RGB highlight, uint8_t metalness) {
        int ti = static_cast<int>(t);
        // 2-3 vein seeds per tile from a stable hash.
        int seed_count = 2 + (hash_shade(3, 7, ti) % 2);
        float sx[3], sy[3], sr[3];
        for (int s = 0; s < seed_count; ++s) {
            sx[s] = 3.0f + static_cast<float>(hash_shade(s * 11 + 1, 5, ti) % 10);
            sy[s] = 3.0f + static_cast<float>(hash_shade(9, s * 13 + 2, ti) % 10);
            sr[s] = 1.8f + static_cast<float>(hash_shade(s + 4, 8, ti) % 3) * 0.5f;
        }
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                // Vein membership: close to any seed, boundary roughened by noise.
                float dist = 99.0f;
                for (int k = 0; k < seed_count; ++k) {
                    float dx = px - sx[k], dy = py - sy[k];
                    dist = std::min(dist, std::sqrt(dx * dx + dy * dy) +
                                              value_noise(px / 2.0f, py / 2.0f, ti + 31) * 1.2f);
                }
                if (dist < 1.0f) {
                    put_pixel(ti, px, py, highlight.r, highlight.g, highlight.b, 255);
                    put_specular_pixel(ti, px, py, 25, metalness, 255, metalness > 150 ? 100 : 0);
                    put_height_pixel(ti, px, py, 185);
                } else if (dist < 2.2f) {
                    int v = (s % 12) - 6;
                    put_pixel(ti, px, py, static_cast<uint8_t>(core.r + v),
                              static_cast<uint8_t>(core.g + v), static_cast<uint8_t>(core.b + v), 255);
                    put_specular_pixel(ti, px, py, 35, metalness, 255, metalness > 150 ? 60 : 0);
                    put_height_pixel(ti, px, py, 165);
                } else {
                    float blotch = value_noise(px / 4.5f, py / 4.5f, ti) * 6.0f;
                    int v = static_cast<int>(blotch) + (s % 7) - 3;
                    uint8_t c = static_cast<uint8_t>(std::clamp<int>(130 + v, 0, 255));
                    put_pixel(ti, px, py, c, static_cast<uint8_t>(c + 5), static_cast<uint8_t>(c + 15), 255);
                    put_specular_pixel(ti, px, py, 200, 0, 255, 0);
                    put_height_pixel(ti, px, py, 120);
                }
            }
        }
    };
    ore(Tile::CoalOre, {38, 38, 44}, {70, 70, 78}, 20);
    ore(Tile::IronOre, {196, 150, 120}, {235, 200, 175}, 180);
    ore(Tile::GoldOre, {235, 190, 50}, {255, 235, 120}, 240);
    ore(Tile::DiamondOre, {45, 200, 190}, {140, 255, 245}, 230);
    ore(Tile::CopperOre, {205, 110, 60}, {245, 165, 110}, 300);
    ore(Tile::RedstoneOre, {190, 30, 30}, {255, 90, 80}, 340);
    ore(Tile::LapisOre, {38, 70, 190}, {90, 130, 240}, 380);

    // 6. Cobblestone — rounded gray stones with dark mortar.
    {
        int ti = static_cast<int>(Tile::Cobblestone);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int cx = (px / 4) * 4 + (s % 2), cy = (py / 4) * 4 + ((s >> 2) % 2);
                int cs = hash_shade(cx, cy, ti + 7);
                bool mortar = (px % 4 == 3) || (py % 4 == 3);
                int v = (cs % 25) - 12;
                if (mortar) {
                    put_pixel(ti, px, py, 85, 85, 90, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(135 + v), static_cast<uint8_t>(135 + v),
                              static_cast<uint8_t>(140 + v), 255);
                }
                put_height_pixel(ti, px, py, mortar ? 100 : 150);
            }
        }
    }

    // 7. Oak log — bark side + growth-ring top.
    {
        int side = static_cast<int>(Tile::LogSide);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, side);
                bool groove = (px % 4 == (s % 2));
                int v = (s % 20) - 10;
                if (groove) {
                    put_pixel(side, px, py, static_cast<uint8_t>(108 + v), static_cast<uint8_t>(80 + v), static_cast<uint8_t>(48 + v), 255);
                } else {
                    put_pixel(side, px, py, static_cast<uint8_t>(142 + v), static_cast<uint8_t>(108 + v), static_cast<uint8_t>(66 + v), 255);
                }
                put_height_pixel(side, px, py, groove ? 90 : 140);
            }
        }
        int top = static_cast<int>(Tile::LogTop);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int dx = px - 8, dy = py - 8;
                int ring = static_cast<int>(std::sqrt(dx * dx + dy * dy)) % 3;
                int v = (hash_shade(px, py, top) % 12) - 6;
                uint8_t r = static_cast<uint8_t>(170 + ring * 12 + v);
                uint8_t g = static_cast<uint8_t>(132 + ring * 9 + v);
                uint8_t b = static_cast<uint8_t>(82 + ring * 6 + v);
                put_pixel(top, px, py, r, g, b, 255);
                put_height_pixel(top, px, py, static_cast<uint8_t>(150 - ring * 10));
            }
        }
    }

    // 8. Stone variants — granite / diorite / andesite.
    auto stone_variant = [&](Tile t, uint8_t r, uint8_t g, uint8_t b, uint8_t sr, uint8_t sg, uint8_t sb, int speck_chance) {
        int ti = static_cast<int>(t);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 14) - 7;
                if ((s % 100) < speck_chance) {
                    put_pixel(ti, px, py, sr, sg, sb, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(r + v), static_cast<uint8_t>(g + v), static_cast<uint8_t>(b + v), 255);
                }
            }
        }
    };
    stone_variant(Tile::Granite, 155, 105, 95, 190, 130, 115, 22);
    stone_variant(Tile::Diorite, 200, 200, 205, 235, 235, 240, 18);
    stone_variant(Tile::Andesite, 130, 135, 140, 100, 105, 112, 15);

    // 9. Bricks — classic red brick bond.
    {
        int ti = static_cast<int>(Tile::Bricks);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int row = py / 4;
                int offset = (row % 2) * 4;
                bool mortar = (py % 4 == 3) || ((px + offset) % 8 == 7);
                int s = hash_shade(px, py, ti);
                int v = (s % 18) - 9;
                if (mortar) {
                    put_pixel(ti, px, py, 190, 185, 175, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(150 + v), static_cast<uint8_t>(72 + v), static_cast<uint8_t>(58 + v), 255);
                }
                put_height_pixel(ti, px, py, mortar ? 100 : 145);
            }
        }
    }

    // 10. Netherrack — dark crimson noise.
    {
        int ti = static_cast<int>(Tile::Netherrack);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 24) - 12;
                put_pixel(ti, px, py, static_cast<uint8_t>(110 + v), static_cast<uint8_t>(42 + v / 2), static_cast<uint8_t>(42 + v / 2), 255);
            }
        }
    }

    // 11. End stone — pale cracked yellow.
    {
        int ti = static_cast<int>(Tile::EndStone);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 16) - 8;
                bool crack = (s > 240);
                if (crack) {
                    put_pixel(ti, px, py, 175, 170, 120, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(221 + v), static_cast<uint8_t>(216 + v), static_cast<uint8_t>(160 + v), 255);
                }
            }
        }
    }

    // 12. Tall grass — dense meadow tuft (cutout, alpha 0 background).
    {
        int ti = static_cast<int>(Tile::TallGrass);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                put_pixel(ti, px, py, 0, 0, 0, 0);
            }
        }
        // Dense blades across the whole tile, taller towards the middle.
        for (int px = 0; px < TILE_PX; ++px) {
            int h = 7 + (hash_shade(px, 1, ti) % 8);          // blade height 7..14
            int lean = (hash_shade(px, 2, ti) % 3) - 1;       // -1..1 tip offset
            for (int py = TILE_PX - 1; py >= TILE_PX - h; --py) {
                int depth = TILE_PX - 1 - py;                 // 0 at bottom
                int x = px + (depth > h / 2 ? lean : 0);
                if (x < 0 || x >= TILE_PX) continue;
                int s = hash_shade(x, py, ti) % 100;
                uint8_t r = static_cast<uint8_t>(76 + (s % 18));
                uint8_t g = static_cast<uint8_t>(136 + (s % 30));
                uint8_t b = static_cast<uint8_t>(64 + (s % 16));
                put_pixel(ti, x, py, r, g, b, 255);
            }
        }
    }

    // 13. Flowers — cutout stem + bloom.
    auto flower = [&](Tile t, uint8_t pr, uint8_t pg, uint8_t pb) {
        int ti = static_cast<int>(t);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                put_pixel(ti, px, py, 0, 0, 0, 0);
            }
        }
        // Stem
        for (int py = 7; py < TILE_PX; ++py) {
            put_pixel(ti, 8, py, 60, 150, 70, 255);
            put_pixel(ti, 7, py, 52, 135, 62, 255);
        }
        // Leaves
        put_pixel(ti, 5, 12, 60, 150, 70, 255);
        put_pixel(ti, 6, 12, 60, 150, 70, 255);
        put_pixel(ti, 10, 10, 60, 150, 70, 255);
        put_pixel(ti, 11, 10, 60, 150, 70, 255);
        // Bloom (5x5 petals + center)
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (std::abs(dx) == 2 && std::abs(dy) == 2) continue;
                put_pixel(ti, 8 + dx, 5 + dy, pr, pg, pb, 255);
            }
        }
        put_pixel(ti, 8, 5, static_cast<uint8_t>(pr / 3 + 110), static_cast<uint8_t>(pg / 3 + 90), 70, 255);
    };
    flower(Tile::FlowerYellow, 250, 220, 60);
    flower(Tile::FlowerRed, 235, 70, 80);

    // 14. Nether portal — swirling violet, semi-transparent, softly emissive.
    {
        int ti = static_cast<int>(Tile::NetherPortal);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int swirl = (px + py * 2 + (s % 5)) % 12;
                uint8_t r = static_cast<uint8_t>(120 + swirl * 6);
                uint8_t g = static_cast<uint8_t>(30 + (s % 25));
                uint8_t b = static_cast<uint8_t>(180 + swirl * 4);
                put_pixel(ti, px, py, r, g, b, 200);
                put_specular_pixel(ti, px, py, 40, 0, 255, 90);
            }
        }
    }

    // 15. End portal frame — end stone base with green eye band on top.
    {
        int ti = static_cast<int>(Tile::EndPortalFrame);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 14) - 7;
                put_pixel(ti, px, py, static_cast<uint8_t>(200 + v), static_cast<uint8_t>(196 + v), static_cast<uint8_t>(148 + v), 255);
            }
        }
        for (int py = 6; py <= 9; ++py) {
            for (int px = 6; px <= 9; ++px) {
                put_pixel(ti, px, py, 40, 110, 80, 255);
                put_specular_pixel(ti, px, py, 60, 0, 255, 60);
            }
        }
    }

    // 16. End portal — starry void, strongly emissive.
    {
        int ti = static_cast<int>(Tile::EndPortal);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                uint8_t r = static_cast<uint8_t>(8 + (s % 10));
                uint8_t g = static_cast<uint8_t>(6 + (s % 8));
                uint8_t b = static_cast<uint8_t>(20 + (s % 18));
                put_pixel(ti, px, py, r, g, b, 255);
                if ((s % 100) > 93) {
                    put_pixel(ti, px, py, 140, 190, 220, 255);
                    put_specular_pixel(ti, px, py, 20, 0, 255, 200);
                } else {
                    put_specular_pixel(ti, px, py, 30, 0, 255, 60);
                }
            }
        }
    }

    // 17. Hand — warm skin tone for the first-person arm.
    {
        int ti = static_cast<int>(Tile::Hand);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 14) - 7;
                put_pixel(ti, px, py, static_cast<uint8_t>(224 + v), static_cast<uint8_t>(176 + v), static_cast<uint8_t>(140 + v), 255);
            }
        }
    }

    // 18. Torch — wooden stick with a glowing ember tip (cutout).
    {
        int ti = static_cast<int>(Tile::Torch);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                put_pixel(ti, px, py, 0, 0, 0, 0);
            }
        }
        for (int py = 6; py < TILE_PX; ++py) {
            put_pixel(ti, 7, py, 110, 82, 48, 255);
            put_pixel(ti, 8, py, 96, 70, 40, 255);
        }
        // Ember head
        for (int py = 3; py <= 5; ++py) {
            for (int px = 6; px <= 9; ++px) {
                put_pixel(ti, px, py, 255, 190, 60, 255);
            }
        }
        put_pixel(ti, 7, 2, 255, 240, 150, 255);
        put_pixel(ti, 8, 2, 255, 220, 100, 255);
        put_specular_pixel(ti, 7, 2, 20, 0, 255, 255);
        put_specular_pixel(ti, 8, 2, 20, 0, 255, 255);
        for (int py = 3; py <= 5; ++py) {
            for (int px = 6; px <= 9; ++px) {
                put_specular_pixel(ti, px, py, 20, 0, 255, 220);
            }
        }
    }

    // 19. Cactus — green ribbed columns with spines.
    {
        int ti = static_cast<int>(Tile::Cactus);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 14) - 7;
                bool rib = (px % 4 == 0);
                bool spine = ((px % 4 == 2) && (py % 5 == 2));
                if (spine) {
                    put_pixel(ti, px, py, 220, 235, 200, 255);
                } else if (rib) {
                    put_pixel(ti, px, py, static_cast<uint8_t>(52 + v), static_cast<uint8_t>(110 + v), static_cast<uint8_t>(48 + v), 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(70 + v), static_cast<uint8_t>(140 + v), static_cast<uint8_t>(62 + v), 255);
                }
                put_height_pixel(ti, px, py, rib ? 110 : 150);
            }
        }
    }

    // 20. Bookshelf — planks frame with rows of colored book spines.
    {
        int ti = static_cast<int>(Tile::Bookshelf);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 14) - 7;
                bool frame = px < 2 || px >= TILE_PX - 2 || py < 2 || py >= TILE_PX - 2 ||
                             (py >= 7 && py <= 8);
                if (frame) {
                    put_pixel(ti, px, py, static_cast<uint8_t>(150 + v), static_cast<uint8_t>(112 + v), static_cast<uint8_t>(64 + v), 255);
                } else {
                    // Book spines: stable per-column hue variation.
                    int col = px % 3;
                    uint8_t r = col == 0 ? uint8_t(140 + v) : col == 1 ? uint8_t(90 + v) : uint8_t(60 + v);
                    uint8_t g = col == 0 ? uint8_t(50 + v) : col == 1 ? uint8_t(70 + v) : uint8_t(90 + v);
                    uint8_t b = col == 0 ? uint8_t(40 + v) : col == 1 ? uint8_t(120 + v) : uint8_t(140 + v);
                    put_pixel(ti, px, py, r, g, b, 255);
                }
                put_height_pixel(ti, px, py, frame ? 140 : 110);
            }
        }
    }

    // 21. Enchanting table top — dark obsidian surface with glowing runes.
    {
        int ti = static_cast<int>(Tile::EnchantingTableTop);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 10) - 5;
                put_pixel(ti, px, py, static_cast<uint8_t>(40 + v), static_cast<uint8_t>(26 + v), static_cast<uint8_t>(70 + v), 255);
                bool rune = ((px * 7 + py * 13) % 31) < 3;
                if (rune) {
                    put_pixel(ti, px, py, 220, 90, 255, 255);
                    put_specular_pixel(ti, px, py, 20, 0, 255, 220);
                }
                put_height_pixel(ti, px, py, 130);
            }
        }
    }

    // 22. Enchanting table side — obsidian base with rune band.
    {
        int ti = static_cast<int>(Tile::EnchantingTableSide);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 10) - 5;
                put_pixel(ti, px, py, static_cast<uint8_t>(28 + v), static_cast<uint8_t>(20 + v), static_cast<uint8_t>(40 + v), 255);
                if (py >= 4 && py <= 6) {
                    bool rune = ((px * 11) % 17) < 4;
                    if (rune) put_pixel(ti, px, py, 180, 70, 220, 255);
                }
                put_height_pixel(ti, px, py, 120);
            }
        }
    }

    // 23. Lever — cobblestone base with a wooden handle.
    {
        int ti = static_cast<int>(Tile::Lever);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                put_pixel(ti, px, py, 0, 0, 0, 0);
            }
        }
        // Base plate
        for (int py = TILE_PX - 4; py < TILE_PX; ++py) {
            for (int px = 4; px < TILE_PX - 4; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 14) - 7;
                put_pixel(ti, px, py, static_cast<uint8_t>(110 + v), static_cast<uint8_t>(110 + v), static_cast<uint8_t>(110 + v), 255);
            }
        }
        // Handle (diagonal-ish stick)
        for (int py = 2; py < TILE_PX - 4; ++py) {
            int px = TILE_PX / 2 - 1 + (py < TILE_PX / 2 ? 1 : 0);
            put_pixel(ti, px, py, 110, 82, 48, 255);
            put_pixel(ti, px + 1, py, 96, 70, 40, 255);
        }
        // Red tip (signals the toggle)
        for (int px = 6; px <= 9; ++px) put_pixel(ti, px, 2, 230, 60, 50, 255);
    }

    // 24. Pressure plate — thin stone slab outline.
    {
        int ti = static_cast<int>(Tile::PressurePlate);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 12) - 6;
                bool border = px < 2 || px >= TILE_PX - 2 || py < 2 || py >= TILE_PX - 2;
                uint8_t g = static_cast<uint8_t>(border ? 96 + v : 130 + v);
                put_pixel(ti, px, py, g, g, g, 255);
                put_height_pixel(ti, px, py, border ? 110 : 140);
            }
        }
    }

    // 25. Redstone lamp off / on — lattice pattern, dim vs glowing.
    for (int variant = 0; variant < 2; ++variant) {
        int ti = static_cast<int>(variant ? Tile::LampOn : Tile::LampOff);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 10) - 5;
                bool border = px < 2 || px >= TILE_PX - 2 || py < 2 || py >= TILE_PX - 2;
                if (variant) {
                    uint8_t r = border ? uint8_t(160 + v) : uint8_t(255);
                    uint8_t g = border ? uint8_t(110 + v) : uint8_t(215);
                    uint8_t b = border ? uint8_t(60 + v) : uint8_t(120);
                    put_pixel(ti, px, py, r, g, b, 255);
                    put_specular_pixel(ti, px, py, 20, 0, 255, border ? 60 : 230);
                } else {
                    uint8_t r = static_cast<uint8_t>(90 + v);
                    uint8_t g = static_cast<uint8_t>(62 + v);
                    uint8_t b = static_cast<uint8_t>(36 + v);
                    put_pixel(ti, px, py, r, g, b, 255);
                }
                put_height_pixel(ti, px, py, 130);
            }
        }
    }

    // 26. Repeater — stone slab with a redstone torch mark.
    {
        int ti = static_cast<int>(Tile::Repeater);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 12) - 6;
                put_pixel(ti, px, py, static_cast<uint8_t>(120 + v), static_cast<uint8_t>(120 + v), static_cast<uint8_t>(120 + v), 255);
                bool mark = (py == TILE_PX / 2 || py == TILE_PX / 2 + 1) && px >= 4 && px < TILE_PX - 4;
                if (mark) put_pixel(ti, px, py, 220, 50, 40, 255);
                put_height_pixel(ti, px, py, 135);
            }
        }
    }

    // 27. Redstone wire — cutout cross with a power node; lit variant glows.
    for (int variant = 0; variant < 2; ++variant) {
        int ti = static_cast<int>(variant ? Tile::RedstoneWireOn : Tile::RedstoneWireOff);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                put_pixel(ti, px, py, 0, 0, 0, 0);
            }
        }
        auto wire_px = [&](int px, int py) {
            if (variant) {
                put_pixel(ti, px, py, 255, 60, 40, 255);
                put_specular_pixel(ti, px, py, 20, 0, 255, 220);
            } else {
                put_pixel(ti, px, py, 110, 20, 16, 255);
            }
        };
        for (int p = 5; p <= 10; ++p) { // cross arms
            wire_px(p, 7); wire_px(p, 8);
            wire_px(7, p); wire_px(8, p);
        }
        // Center node, brighter when powered.
        for (int dy = -1; dy <= 2; ++dy) {
            for (int dx = -1; dx <= 2; ++dx) {
                wire_px(7 + dx, 7 + dy);
            }
        }
        if (variant) { // hot core
            for (int dy = 0; dy <= 1; ++dy) {
                for (int dx = 0; dx <= 1; ++dx) {
                    put_pixel(ti, 7 + dx, 7 + dy, 255, 160, 90, 255);
                }
            }
        }
    }

    // 28. Redstone torch — stick with a glowing red head (cutout).
    {
        int ti = static_cast<int>(Tile::RedstoneTorch);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) put_pixel(ti, px, py, 0, 0, 0, 0);
        }
        for (int py = 6; py < TILE_PX; ++py) {
            put_pixel(ti, 7, py, 110, 82, 48, 255);
            put_pixel(ti, 8, py, 96, 70, 40, 255);
        }
        for (int py = 3; py <= 5; ++py) {
            for (int px = 6; px <= 9; ++px) put_pixel(ti, px, py, 235, 50, 35, 255);
        }
        put_pixel(ti, 7, 4, 255, 120, 80, 255);
        put_pixel(ti, 8, 4, 255, 120, 80, 255);
        for (int py = 3; py <= 5; ++py) {
            for (int px = 6; px <= 9; ++px) put_specular_pixel(ti, px, py, 20, 0, 255, 230);
        }
    }

    // 29. Redstone block — deep red mineral slab with darker joints.
    {
        int ti = static_cast<int>(Tile::RedstoneBlock);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                float blotch = value_noise(px / 4.0f, py / 4.0f, ti) * 10.0f;
                int v = static_cast<int>(blotch) + (s % 8) - 4;
                bool joint = (px % 8 == 7) || (py % 8 == 7);
                if (joint) {
                    put_pixel(ti, px, py, 105, 18, 14, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(168 + v), static_cast<uint8_t>(34 + v / 2),
                              static_cast<uint8_t>(26 + v / 2), 255);
                }
                put_height_pixel(ti, px, py, joint ? 110 : 150);
            }
        }
    }

    // 30. Crafting table — top: work grid with tool marks; side: planks with
    // a tool window (saw + hammer silhouettes).
    {
        int ti = static_cast<int>(Tile::CraftingTableTop);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 12) - 6;
                bool border = px < 2 || px >= TILE_PX - 2 || py < 2 || py >= TILE_PX - 2;
                bool grid = (px == 8 || px == 9 || py == 8 || py == 9);
                if (border) {
                    put_pixel(ti, px, py, static_cast<uint8_t>(118 + v), static_cast<uint8_t>(86 + v), static_cast<uint8_t>(50 + v), 255);
                } else if (grid) {
                    put_pixel(ti, px, py, 96, 68, 40, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(176 + v), static_cast<uint8_t>(134 + v), static_cast<uint8_t>(82 + v), 255);
                }
                put_height_pixel(ti, px, py, (border || grid) ? 115 : 150);
            }
        }
        int side = static_cast<int>(Tile::CraftingTableSide);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, side);
                int v = (s % 12) - 6;
                bool plank_seam = (py == 5 || py == 10);
                bool window = px >= 3 && px <= 12 && py >= 6 && py <= 9;
                if (window) {
                    // Tool silhouettes over a dark backing.
                    put_pixel(side, px, py, 58, 42, 26, 255);
                    bool saw = (py == 7 && px >= 4 && px <= 8) || (py == 8 && px == 6);
                    bool hammer = (py == 7 && px >= 10 && px <= 11) || (py == 8 && px == 10) || (py == 6 && px == 10);
                    if (saw) put_pixel(side, px, py, 190, 190, 198, 255);
                    if (hammer) put_pixel(side, px, py, 150, 116, 70, 255);
                } else if (plank_seam) {
                    put_pixel(side, px, py, 120, 80, 45, 255);
                } else {
                    put_pixel(side, px, py, static_cast<uint8_t>(168 + v), static_cast<uint8_t>(126 + v), static_cast<uint8_t>(76 + v), 255);
                }
                put_height_pixel(side, px, py, window ? 105 : 145);
            }
        }
    }

    // 31. Furnace — cobble body, dark mouth with grate; lit mouth glows.
    for (int variant = 0; variant < 2; ++variant) {
        int ti = static_cast<int>(variant ? Tile::FurnaceFrontLit : Tile::FurnaceFront);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 14) - 7;
                bool mouth = px >= 4 && px <= 11 && py >= 6 && py <= 13;
                bool grate = mouth && (py == 9 || py == 12);
                if (mouth && variant && !grate) {
                    // Fire: bright core low, animated-look licks.
                    uint8_t fr = 255, fg = py >= 10 ? 170 : 120, fb = 40;
                    if (py >= 11 && (px % 3 != 2)) { fg = 220; }
                    put_pixel(ti, px, py, fr, fg, fb, 255);
                    put_specular_pixel(ti, px, py, 30, 0, 255, 255);
                } else if (mouth) {
                    put_pixel(ti, px, py, 22, 20, 20, 255);
                } else if (grate) {
                    put_pixel(ti, px, py, 70, 68, 66, 255);
                } else {
                    put_pixel(ti, px, py, static_cast<uint8_t>(128 + v), static_cast<uint8_t>(126 + v), static_cast<uint8_t>(124 + v), 255);
                }
                put_height_pixel(ti, px, py, mouth ? 90 : 145);
            }
        }
    }

    // 32. Quest NPC — villager-like face: skin, unibrow, eyes, long nose.
    {
        int ti = static_cast<int>(Tile::QuestNpc);
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) {
                int s = hash_shade(px, py, ti);
                int v = (s % 10) - 5;
                put_pixel(ti, px, py, static_cast<uint8_t>(198 + v), static_cast<uint8_t>(150 + v), static_cast<uint8_t>(110 + v), 255);
            }
        }
        // Robe collar at the bottom.
        for (int py = 13; py < TILE_PX; ++py) {
            for (int px = 2; px <= 13; ++px) put_pixel(ti, px, py, 90, 60, 110, 255);
        }
        // Hair top.
        for (int py = 0; py < 3; ++py) {
            for (int px = 2; px <= 13; ++px) put_pixel(ti, px, py, 80, 58, 40, 255);
        }
        // Unibrow.
        for (int px = 4; px <= 11; ++px) put_pixel(ti, px, 5, 80, 58, 40, 255);
        // Eyes (green like villager).
        for (int py = 6; py <= 7; ++py) {
            put_pixel(ti, 4, py, 60, 120, 70, 255);
            put_pixel(ti, 5, py, 60, 120, 70, 255);
            put_pixel(ti, 10, py, 60, 120, 70, 255);
            put_pixel(ti, 11, py, 60, 120, 70, 255);
        }
        // Long nose.
        for (int py = 7; py <= 11; ++py) {
            put_pixel(ti, 7, py, 175, 128, 92, 255);
            put_pixel(ti, 8, py, 175, 128, 92, 255);
        }
        // Mouth.
        for (int px = 6; px <= 9; ++px) put_pixel(ti, px, 12, 120, 80, 60, 255);
    }

    // 33. Mining destroy stages — growing crack web, cutout overlay.
    for (int stage = 0; stage < 10; ++stage) {
        int ti = static_cast<int>(Tile::DestroyStage0) + stage;
        for (int py = 0; py < TILE_PX; ++py) {
            for (int px = 0; px < TILE_PX; ++px) put_pixel(ti, px, py, 0, 0, 0, 0);
        }
        // Crack count and length grow with the stage; random walks from the
        // center are stable per stage so the animation reads as deepening.
        int cracks = 2 + stage;
        for (int c = 0; c < cracks; ++c) {
            float x = 8.0f, y = 8.0f;
            float ang = static_cast<float>(hash_shade(c * 17 + 3, stage, 91)) * 6.2831f / 255.0f;
            int len = 3 + (stage * (2 + (hash_shade(c, stage, 55) % 3))) / 2;
            for (int step = 0; step < len; ++step) {
                int px = static_cast<int>(x), py = static_cast<int>(y);
                if (px >= 0 && px < TILE_PX && py >= 0 && py < TILE_PX) {
                    put_pixel(ti, px, py, 10, 10, 12, 200);
                }
                ang += (static_cast<float>(hash_shade(px * 7 + step, py * 5 + c, ti)) / 255.0f - 0.5f) * 1.4f;
                x += std::cos(ang);
                y += std::sin(ang);
            }
        }
    }

    // 34. Texture overrides — assets/textures/<TileName>.png replaces the
    // procedural tile (art can be swapped without touching code). Any source
    // resolution is accepted and box-downsampled to 16x16.
    namespace fs = std::filesystem;
    const fs::path override_dir{"assets/textures"};
    if (fs::exists(override_dir)) {
        for (const auto& entry : fs::directory_iterator(override_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".png") continue;
            const std::string stem = entry.path().stem().string();
            for (size_t ti = 0; ti < TILE_NAMES.size(); ++ti) {
                if (stem != TILE_NAMES[ti]) continue;
                int w = 0, h = 0, comp = 0;
                stbi_uc* src = stbi_load(entry.path().string().c_str(), &w, &h, &comp, 4);
                if (!src) break;
                for (int py = 0; py < TILE_PX; ++py) {
                    for (int px = 0; px < TILE_PX; ++px) {
                        int sx = std::min(w - 1, px * w / TILE_PX);
                        int sy = std::min(h - 1, py * h / TILE_PX);
                        const stbi_uc* s = src + ((sy * w) + sx) * 4;
                        put_pixel(static_cast<int>(ti), px, py, s[0], s[1], s[2], s[3]);
                    }
                }
                stbi_image_free(src);
                break;
            }
        }
    }
}

void TextureAtlas::upload() {
    auto upload_array = [](uint32_t& tex_id, int layers, const std::vector<uint8_t>& buf, bool is_srgb) {
        std::vector<uint8_t> flipped(buf.size());
        for (int layer = 0; layer < layers; ++layer) {
            for (int py = 0; py < TILE_PX; ++py) {
                std::memcpy(
                    flipped.data() + (layer * TILE_PX * TILE_PX + (TILE_PX - 1 - py) * TILE_PX) * 4,
                    buf.data() + (layer * TILE_PX * TILE_PX + py * TILE_PX) * 4,
                    TILE_PX * 4
                );
            }
        }

        glGenTextures(1, &tex_id);
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex_id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        GLenum internal_fmt = is_srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internal_fmt, TILE_PX, TILE_PX, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data());
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

        float max_aniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &max_aniso);
        if (max_aniso > 1.0f) {
            glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, std::min(max_aniso, 8.0f));
        }
    };

    upload_array(gl_albedo_tex_, num_layers_, albedo_pixels_, true);
    upload_array(gl_normal_tex_, num_layers_, normal_pixels_, false);
    upload_array(gl_specular_tex_, num_layers_, specular_pixels_, false);
    upload_array(gl_height_tex_, num_layers_, height_pixels_, false);
}

void TextureAtlas::bind(uint32_t slot) const {
    bind_all(slot);
}

void TextureAtlas::bind_all(uint32_t base_slot) const {
    glActiveTexture(GL_TEXTURE0 + base_slot + 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gl_albedo_tex_);

    glActiveTexture(GL_TEXTURE0 + base_slot + 1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gl_normal_tex_);

    glActiveTexture(GL_TEXTURE0 + base_slot + 2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gl_specular_tex_);

    glActiveTexture(GL_TEXTURE0 + base_slot + 3);
    glBindTexture(GL_TEXTURE_2D_ARRAY, gl_height_tex_);
}

} // namespace mc
