// Offline asset dumper — QA tool for procedural art.
// Generates the texture atlas (and later the item icon atlas) on the CPU and
// writes scaled contact-sheet PNGs so art can be reviewed without launching
// the game or creating a GL context.
//
// Usage: minekampf_assets.exe [output_dir]

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image.h"
#include "renderer/stb_image_write.h"

#include <glm/gtc/matrix_transform.hpp>

#include "gameplay/item.hpp"
#include "renderer/texture_atlas.hpp"
#include "renderer/item_icons.hpp"
#include "renderer/geo_model.hpp"
#include "renderer/mob_rig.hpp"
#include "world/block.hpp"

#include <cmath>

namespace {

// Software-render one Blockbench model to a PNG (pose + box UVs), so geometry
// and texture layout can be reviewed without launching the game.
void render_model_preview(const char* name, const std::string& out_dir) {
    namespace fs = std::filesystem;
    fs::path base = fs::path("assets/models/mobs") / (std::string(name) + ".geo.json");
    std::string err;
    auto model = mc::GeoModel::load_from_file(base.string(), &err);
    if (!model) {
        std::printf("model %s: LOAD FAILED: %s\n", name, err.c_str());
        return;
    }
    // Load texture via texkit-generated PNG (tiny BMP-less stb use).
    int tw = 0, th = 0, comp = 0;
    std::string tex_path = base.string().substr(0, base.string().size() - strlen(".geo.json")) + ".png";
    stbi_uc* tex = stbi_load(tex_path.c_str(), &tw, &th, &comp, 4);
    if (!tex) {
        std::printf("model %s: texture missing %s\n", name, tex_path.c_str());
        return;
    }

    // Camera: orbit at 30 deg elevation, orthographic.
    const int W = 320, H = 320;
    std::vector<uint8_t> img(static_cast<size_t>(W) * H * 3, 40);
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        int i = (y * W + x) * 3;
        img[i] = r; img[i + 1] = g; img[i + 2] = b;
    };
    float yaw = 0.6f;
    auto project = [&](const glm::vec3& p) -> glm::vec2 {
        float cx = std::cos(yaw), sx = std::sin(yaw);
        float x = p.x * cx - p.z * sx;
        float z = p.x * sx + p.z * cx;
        return {W * 0.5f + x * 110.0f, H * 0.88f - p.y * 110.0f + z * 0.0f};
    };

    // Pose: rest pose, unit rig root.
    glm::mat4 root(1.0f);
    std::vector<glm::mat4> mats(model->bones.size(), root);
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < model->bones.size(); ++i) {
            const auto& bone = model->bones[i];
            glm::mat4 parent = bone.parent >= 0 ? mats[bone.parent] : root;
            glm::mat4 m = glm::translate(parent, bone.pivot * (1.0f / 16.0f));
            if (bone.base_rot_x != 0.0f)
                m = glm::rotate(m, bone.base_rot_x, glm::vec3(1, 0, 0));
            mats[i] = m;
        }
    }

    struct Tri { glm::vec2 a, b, c, d; float depth; uint8_t r, g, bl; float u0, v0, u1, v1; };
    std::vector<Tri> tris;
    for (size_t i = 0; i < model->bones.size(); ++i) {
        const auto& bone = model->bones[i];
        for (const auto& cube : bone.cubes) {
            glm::vec3 center = cube.origin + cube.size * 0.5f - bone.pivot;
            glm::mat4 t = glm::scale(glm::translate(mats[i], center * (1.0f / 16.0f)),
                                     cube.size * (1.0f / 16.0f));
            float u0 = cube.uv.x, v0 = cube.uv.y;
            float w = std::abs(cube.size.x), h = std::abs(cube.size.y), d = std::abs(cube.size.z);
            glm::vec2 uv_south{u0 + d + w, v0 + d}, uv_north{u0 + d, v0 + d};
            glm::vec2 uv_down{u0 + d + w, v0}, uv_up{u0 + d, v0};
            glm::vec2 uv_west{u0 + d + w + d, v0 + d}, uv_east{u0, v0 + d};
            glm::vec2 uvs[6] = {uv_south, uv_north, uv_down, uv_up, uv_west, uv_east};
            const glm::vec3 corners[8] = {
                {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}};
            const int faces[6][4] = {{5,4,7,6},{1,0,3,2},{4,0,1,5},{6,7,3,2},{0,4,7,3},{1,5,6,2}};
            for (int f = 0; f < 6; ++f) {
                glm::vec2 uv = uvs[f];
                glm::vec2 quad[4];
                float depth = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    glm::vec3 wp = glm::vec3(t * glm::vec4(corners[faces[f][k]], 1.0f));
                    quad[k] = project(wp);
                    depth += wp.x * std::sin(yaw) + wp.z * std::cos(yaw);
                }
                uint8_t sh = static_cast<uint8_t>(150 + f * 18);
                tris.push_back({quad[0], quad[1], quad[2], quad[3], depth, sh, sh, sh,
                                uv.x / tw, uv.y / th, (uv.x + (f < 2 ? w : (f < 4 ? w : d))) / tw,
                                (uv.y + (f < 2 || f >= 4 ? h : d)) / th});
            }
        }
    }
    std::sort(tris.begin(), tris.end(), [](const Tri& a, const Tri& b) { return a.depth < b.depth; });

    for (const auto& t : tris) {
        // Rasterize the quad's bounding box; point-in-parallelogram (bilinear).
        float minx = std::min({t.a.x, t.b.x, t.c.x, t.d.x});
        float maxx = std::max({t.a.x, t.b.x, t.c.x, t.d.x});
        float miny = std::min({t.a.y, t.b.y, t.c.y, t.d.y});
        float maxy = std::max({t.a.y, t.b.y, t.c.y, t.d.y});
        // Parallelogram from a: e1 = b-a, e2 = d-a.
        glm::vec2 e1 = t.b - t.a, e2 = t.d - t.a;
        float det = e1.x * e2.y - e1.y * e2.x;
        if (std::fabs(det) < 1e-6f) continue;
        for (int y = static_cast<int>(miny); y <= static_cast<int>(maxy); ++y) {
            for (int x = static_cast<int>(minx); x <= static_cast<int>(maxx); ++x) {
                glm::vec2 p{x + 0.5f - t.a.x, y + 0.5f - t.a.y};
                float u = (p.x * e2.y - e2.x * p.y) / det;
                float v = (e1.x * p.y - p.x * e1.y) / det;
                if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
                int tx = std::min(tw - 1, static_cast<int>((t.u0 + u * (t.u1 - t.u0)) * tw));
                int ty = std::min(th - 1, static_cast<int>((t.v0 + v * (t.v1 - t.v0)) * th));
                const stbi_uc* px = tex + ((ty * tw) + tx) * 4;
                if (px[3] < 40) continue;
                put(x, y, px[0], px[1], px[2]);
            }
        }
    }
    std::printf("  dbg: %zu tris, tex %dx%d\n", tris.size(), tw, th);
    if (!tris.empty()) {
        std::printf("  dbg: sample uv %f..%f / %f..%f\n", tris.front().u0, tris.front().u1,
                    tris.front().v0, tris.front().v1);
        int tx0 = std::min(tw - 1, static_cast<int>(tris.front().u0 * tw));
        int ty0 = std::min(th - 1, static_cast<int>(tris.front().v0 * th));
        const stbi_uc* px = tex + ((ty0 * tw) + tx0) * 4;
        std::printf("  dbg: tex corner rgba = %d,%d,%d,%d\n", px[0], px[1], px[2], px[3]);
    }
    stbi_image_free(tex);
    std::string out = out_dir + "/model_" + name + ".png";
    stbi_write_png(out.c_str(), W, H, 3, img.data(), W * 3);
    std::printf("model %s: %zu bones -> %s\n", name, model->bones.size(), out.c_str());
}


constexpr int kScale = 8;                 // each 16px tile becomes a 128px cell
constexpr int kCellPx = mc::TextureAtlas::TILE_PX * kScale;
constexpr int kCols = 16;

// 3x5 digit font for tile-index labels.
constexpr uint8_t kDigits[10] = {
    0b111101101101111, // 0
    0b010110010010111, // 1
    0b111001111100111, // 2
    0b111001111001111, // 3
    0b101101111001001, // 4
    0b111100111001111, // 5
    0b111100111101111, // 6
    0b111001010010010, // 7
    0b111101111101111, // 8
    0b111101111001111, // 9
};

void draw_digit(std::vector<uint8_t>& img, int w, int d, int x, int y) {
    if (d < 0 || d > 9) return;
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (kDigits[d] & (1u << (row * 3 + col))) {
                int px = x + col, py = y + row;
                int idx = (py * w + px) * 3;
                img[idx] = img[idx + 1] = img[idx + 2] = 255 - img[idx];
            }
        }
    }
}

void draw_number(std::vector<uint8_t>& img, int w, int n, int x, int y) {
    char buf[8];
    int len = std::snprintf(buf, sizeof(buf), "%d", n);
    for (int i = 0; i < len; ++i) draw_digit(img, w, buf[i] - '0', x + i * 4, y);
}

// Composes a contact sheet: checkerboard background (shows alpha), each
// cell nearest-neighbor upscaled, index number in the top-left corner.
// `pixels` is a grid of atlas_cols cells per row, each cell cell_px square.
std::vector<uint8_t> compose_sheet(const std::vector<uint8_t>& pixels, int cells,
                                   int cell_px, int scale, bool contiguous) {
    int cell_out = cell_px * scale;
    int rows = (cells + kCols - 1) / kCols;
    std::vector<uint8_t> img(static_cast<size_t>(rows * cell_out) * kCols * cell_out * 3, 0);
    const int sheet_w = kCols * cell_out;
    const int checker = std::max(2, cell_out / 16);
    // Source layout: the tile atlas is layer-major contiguous (cell_px * cell_px
    // pixels per tile); the icon atlas is a row-major grid of ATLAS_COLS cells.
    const int atlas_cols = contiguous ? 1 : mc::ItemIcons::ATLAS_COLS;
    const int atlas_pitch = contiguous ? cell_px : atlas_cols * cell_px;
    (void)atlas_cols;

    for (int cell = 0; cell < rows * kCols; ++cell) {
        int cx = (cell % kCols) * cell_out;
        int cy = (cell / kCols) * cell_out;
        for (int py = 0; py < cell_out; ++py) {
            for (int px = 0; px < cell_out; ++px) {
                bool checker_cell = ((px / checker) + (py / checker)) % 2 == 0;
                uint8_t bg = checker_cell ? 70 : 50;
                int dst = ((cy + py) * sheet_w + (cx + px)) * 3;
                img[dst] = bg;
                img[dst + 1] = bg;
                img[dst + 2] = bg;
            }
        }
    }

    for (int layer = 0; layer < cells; ++layer) {
        int cx = (layer % kCols) * cell_out;
        int cy = (layer / kCols) * cell_out;
        int ax = contiguous ? 0 : (layer % atlas_cols) * cell_px;
        int ay = contiguous ? layer * cell_px : (layer / atlas_cols) * cell_px;
        for (int py = 0; py < cell_px; ++py) {
            for (int px = 0; px < cell_px; ++px) {
                int src = ((ay + py) * atlas_pitch + ax + px) * 4;
                uint8_t r = pixels[src], g = pixels[src + 1], b = pixels[src + 2], a = pixels[src + 3];
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        int dst = ((cy + py * scale + sy) * sheet_w + cx + px * scale + sx) * 3;
                        for (int c = 0; c < 3; ++c) {
                            uint8_t rgb[3] = {r, g, b};
                            img[dst + c] = static_cast<uint8_t>((rgb[c] * a + img[dst + c] * (255 - a)) / 255);
                        }
                    }
                }
            }
        }
        draw_number(img, sheet_w, layer, cx + 3, cy + 3);
    }
    return img;
}

bool write_png(const std::string& path, const std::vector<uint8_t>& rgb, int w, int h) {
    return stbi_write_png(path.c_str(), w, h, 3, rgb.data(), w * 3) != 0;
}

// Grayscale view of one RGBA channel (for normal/specular/height sheets).
std::vector<uint8_t> to_gray(const std::vector<uint8_t>& rgba, int channel) {
    std::vector<uint8_t> out(rgba.size());
    for (size_t i = 0; i < rgba.size(); i += 4) {
        out[i] = out[i + 1] = out[i + 2] = rgba[i + channel];
        out[i + 3] = rgba[i + 3];
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::string out_dir = argc > 1 ? argv[1] : ".";
    auto out_path = [&](const char* name) { return out_dir + "/" + name; };

    mc::TextureAtlas atlas;
    atlas.generate();

    int layers = atlas.num_layers();
    int tile = mc::TextureAtlas::TILE_PX;
    int rows = (layers + kCols - 1) / kCols;
    int sheet_w = kCols * kCellPx, sheet_h = rows * kCellPx;

    auto ok = true;
    // Albedo RGB plus grayscale channel views (roughness, emissive, height).
    ok &= write_png(out_path("atlas_albedo.png"), compose_sheet(atlas.albedo_data(), layers, tile, kScale, true), sheet_w, sheet_h);
    ok &= write_png(out_path("atlas_roughness.png"), compose_sheet(to_gray(atlas.specular_data(), 0), layers, tile, kScale, true), sheet_w, sheet_h);
    ok &= write_png(out_path("atlas_emissive.png"), compose_sheet(to_gray(atlas.specular_data(), 3), layers, tile, kScale, true), sheet_w, sheet_h);
    ok &= write_png(out_path("atlas_height.png"), compose_sheet(to_gray(atlas.height_data(), 0), layers, tile, kScale, true), sheet_w, sheet_h);

    // Item icon atlas: 32px cells at 4x -> 128px cells.
    mc::ItemIcons icons;
    icons.generate(atlas);
    int icon_cells = static_cast<int>(mc::BLOCK_COUNT) + (static_cast<int>(mc::ITEM_COUNT) - 256);
    int irows = (icon_cells + kCols - 1) / kCols;
    int isheet_w = kCols * mc::ItemIcons::ICON_PX * 4;
    int isheet_h = irows * mc::ItemIcons::ICON_PX * 4;
    ok &= write_png(out_path("icons.png"),
                    compose_sheet(icons.rgba_data(), icon_cells, mc::ItemIcons::ICON_PX, 4, false),
                    isheet_w, isheet_h);

    // Coverage debug: opaque-pixel count per icon cell (low = painting bug).
    const auto& ip = icons.rgba_data();
    int ipx = mc::ItemIcons::ICON_PX;
    int ipitch = mc::ItemIcons::ATLAS_COLS * ipx;
    for (int cell = 0; cell < icon_cells; ++cell) {
        int ax = (cell % mc::ItemIcons::ATLAS_COLS) * ipx;
        int ay = (cell / mc::ItemIcons::ATLAS_COLS) * ipx;
        int cov = 0;
        for (int py = 0; py < ipx; ++py) {
            for (int px = 0; px < ipx; ++px) {
                if (ip[((ay + py) * ipitch + ax + px) * 4 + 3] > 0) ++cov;
            }
        }
        if (cov > 0 && cov < ipx * ipx / 4)
            std::printf("cell %d coverage %d/%d\n", cell, cov, ipx * ipx);
    }

    // Blockbench model previews (posed + box-UV software render).
    for (const char* sp : {"zombie", "skeleton", "cow", "pig"}) {
        render_model_preview(sp, out_dir);
    }

    std::printf("%s: %d tiles + icon atlas -> sheets in %s\n", ok ? "OK" : "FAILED", layers,
                out_dir.c_str());
    return ok ? 0 : 1;
}
