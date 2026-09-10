#include "renderer/item_icons.hpp"

#include <algorithm>
#include <cctype>
#include <type_traits>
#include <cmath>
#include <cstring>

#include <glad/gl.h>

#include <filesystem>

#include "core/logger.hpp"
#include "gameplay/item.hpp"
#include "renderer/texture_atlas.hpp"
#include "world/block.hpp"

#include "third_party/stb_image.h"

namespace mc {

namespace {

constexpr int ICON_PX = ItemIcons::ICON_PX;

// 32x32 RGBA canvas with draw helpers.
struct Canvas {
    std::vector<uint8_t> px{};

    void clear() { px.assign(ICON_PX * ICON_PX * 4, 0); }

    void set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || x >= ICON_PX || y < 0 || y >= ICON_PX) return;
        int i = (y * ICON_PX + x) * 4;
        px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a;
    }

    // Blends a shaded texel over the canvas (for iso faces composited in order).
    void shade_set(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a, float shade) {
        set(x, y, static_cast<uint8_t>(r * shade), static_cast<uint8_t>(g * shade),
            static_cast<uint8_t>(b * shade), a);
    }
};

struct Material {
    uint8_t br, bg, bb;   // base
    uint8_t dr, dg, db;   // dark
    uint8_t lr, lg, lb;   // light
};

constexpr Material kWood = {168,132, 84,110, 84, 50,196,160,108};
constexpr Material kStone = {128,132,138, 88, 92, 98,168,172,178};
constexpr Material kIron = {216,216,222,150,150,158,245,245,250};
constexpr Material kGold = {250,208, 68,180,140, 30,255,238,140};
constexpr Material kDiamond = { 90,230,214, 40,160,150,180,255,245};
constexpr Material kApple = {218, 58, 52,150, 28, 26,245,120,100};
constexpr Material kCoal = { 45, 45, 52, 24, 24, 30, 92, 92,104};
constexpr Material kCharcoal = { 62, 48, 38, 36, 28, 22, 98, 82, 66};
constexpr Material kRawMeat = {228,108,108,178, 60, 60,246,172,172};
constexpr Material kCookedMeat = {168, 92, 50,118, 58, 28,204,132, 82};
constexpr Material kLeather = {146, 96, 54,100, 64, 34,184,134, 88};

// -- Isometric cube ---------------------------------------------------------
// Classic 2:1 item-icon projection. Face corners:
//   top:    L(2,9)  T(16,1)  R(30,9)  B(16,17)
//   left:   L(2,9)  B(16,17) (16,31)  (2,23)
//   right:  B(16,17) R(30,9) (30,23)  (16,31)
// Each face is an affine map p = O + u*e1 + v*e2 into the 16x16 tile.
struct IsoFace {
    float ox, oy, e1x, e1y, e2x, e2y;
};

void draw_iso_face(Canvas& c, const IsoFace& f, const uint8_t* tile, float shade) {
    float det = f.e1x * f.e2y - f.e1y * f.e2x;
    if (std::fabs(det) < 1e-6f) return;
    float minx = std::min({f.ox, f.ox + f.e1x, f.ox + f.e2x, f.ox + f.e1x + f.e2x});
    float maxx = std::max({f.ox, f.ox + f.e1x, f.ox + f.e2x, f.ox + f.e1x + f.e2x});
    float miny = std::min({f.oy, f.oy + f.e1y, f.oy + f.e2y, f.oy + f.e1y + f.e2y});
    float maxy = std::max({f.oy, f.oy + f.e1y, f.oy + f.e2y, f.oy + f.e1y + f.e2y});
    for (int y = static_cast<int>(miny); y <= static_cast<int>(maxy) + 1; ++y) {
        for (int x = static_cast<int>(minx); x <= static_cast<int>(maxx) + 1; ++x) {
            float px = x + 0.5f - f.ox, py = y + 0.5f - f.oy;
            float u = (px * f.e2y - f.e2x * py) / det;
            float v = (f.e1x * py - px * f.e1y) / det;
            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) continue;
            int tx = std::clamp(static_cast<int>(u * TextureAtlas::TILE_PX), 0, TextureAtlas::TILE_PX - 1);
            int ty = std::clamp(static_cast<int>(v * TextureAtlas::TILE_PX), 0, TextureAtlas::TILE_PX - 1);
            const uint8_t* t = tile + ((ty * TextureAtlas::TILE_PX) + tx) * 4;
            c.shade_set(x, y, t[0], t[1], t[2], t[3], shade);
        }
    }
}

// Samples a tile's albedo straight out of the atlas CPU buffer.
const uint8_t* tile_pixels(const TextureAtlas& atlas, Tile tile) {
    int ti = static_cast<int>(tile);
    return atlas.albedo_data().data() + static_cast<size_t>(ti) * TextureAtlas::TILE_PX *
                                             TextureAtlas::TILE_PX * 4;
}

void draw_block_icon(Canvas& c, const TextureAtlas& atlas, BlockId block) {
    const BlockProperties& props = BLOCK_PROPERTIES_TABLE[block];
    bool is_cube = props.full_cube;
    if (is_cube) {
        IsoFace top {2.f,  9.f, 14.f, -8.f, 14.f, 8.f};   // L -> T, L -> B
        IsoFace left{2.f,  9.f, 14.f,  8.f,  0.f, 14.f};  // L -> B, L -> down
        IsoFace right{16.f,17.f, 14.f, -8.f,  0.f, 14.f}; // B -> R, B -> down
        draw_iso_face(c, right, tile_pixels(atlas, props.tile_side), 0.62f);
        draw_iso_face(c, left,  tile_pixels(atlas, props.tile_side), 0.80f);
        draw_iso_face(c, top,   tile_pixels(atlas, props.tile_top),  1.0f);
    } else {
        // Flat sprite (plants, torches, wire): tile scaled 2x, centered.
        const uint8_t* t = tile_pixels(atlas, props.tile_side);
        for (int py = 0; py < TextureAtlas::TILE_PX; ++py) {
            for (int px = 0; px < TextureAtlas::TILE_PX; ++px) {
                const uint8_t* s = t + ((py * TextureAtlas::TILE_PX) + px) * 4;
                if (s[3] == 0) continue;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        c.set(px * 2 + dx, py * 2 + dy, s[0], s[1], s[2], s[3]);
                    }
                }
            }
        }
    }
}

// -- 16x16 pixel-art helpers (drawn 2x into the 32x32 cell) ------------------
struct Art {
    uint8_t px[16 * 16]{};

    void set(int x, int y, uint8_t v = 1) {
        if (x < 0 || x >= 16 || y < 0 || y >= 16) return;
        px[y * 16 + x] = v;
    }
    void rect(int x0, int y0, int x1, int y1, uint8_t v = 1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) set(x, y, v);
    }
};

// Value per pixel: 0 = empty, 1 = base, 2 = dark, 3 = light, 4 = accent.
constexpr uint8_t kBase = 1, kDark = 2, kLight = 3, kAccent = 4;

void blit_art(Canvas& c, const Art& a, const Material& m, uint8_t ar, uint8_t ag, uint8_t ab) {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint8_t v = a.px[y * 16 + x];
            if (v == 0) continue;
            uint8_t r = 0, g = 0, b = 0;
            switch (v) {
                case kBase:   r = m.br; g = m.bg; b = m.bb; break;
                case kDark:   r = m.dr; g = m.dg; b = m.db; break;
                case kLight:  r = m.lr; g = m.lg; b = m.lb; break;
                case kAccent: r = ar;   g = ag;   b = ab;   break;
            }
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    c.set(x * 2 + dx, y * 2 + dy, r, g, b);
                }
            }
        }
    }
}

void art_sword(Art& a, bool guard_accent) {
    // Blade: 2px diagonal from (4,11) up to (12,3); light edge on top side.
    for (int i = 0; i < 9; ++i) {
        int x = 4 + i, y = 11 - i;
        a.set(x, y, kBase);
        a.set(x + 1, y, kDark);
        a.set(x, y - 1, kLight);
    }
    a.set(13, 2, kLight); // tip glint
    // Guard: perpendicular 3px bar.
    a.set(3, 9, guard_accent ? kAccent : kDark);
    a.set(4, 10, guard_accent ? kAccent : kDark);
    a.set(5, 9, guard_accent ? kAccent : kDark);
    a.set(4, 12, guard_accent ? kAccent : kDark);
    // Handle: short diagonal grip.
    a.set(2, 12, kDark);
    a.set(3, 11, kDark);
    a.set(1, 13, kDark);
    a.set(2, 13, kBase);
}

void art_pickaxe(Art& a) {
    // Head: arc across the top.
    a.rect(3, 2, 12, 2, kBase);
    a.set(2, 3, kBase); a.set(13, 3, kBase);
    a.set(1, 4, kBase); a.set(14, 4, kBase);
    a.set(1, 5, kDark); a.set(14, 5, kDark);
    a.rect(4, 3, 11, 3, kLight);
    // Handle: diagonal stick.
    for (int i = 0; i < 9; ++i) {
        a.set(7 - (i < 3 ? 0 : (i - 3) / 3), 4 + i, kDark);
        a.set(8 - (i < 3 ? 0 : (i - 3) / 3), 4 + i, kBase);
    }
}

void art_ingot(Art& a) {
    // Isometric bar with beveled highlight.
    a.rect(3, 8, 11, 11, kBase);
    a.rect(5, 6, 13, 9, kBase);
    a.rect(5, 6, 13, 6, kLight);
    a.rect(3, 8, 11, 8, kLight);
    a.rect(11, 9, 13, 9, kLight);
    a.rect(3, 11, 11, 11, kDark);
    a.rect(12, 10, 13, 10, kDark);
}

void art_diamond(Art& a) {
    // Gem: rhombus with faceted shading.
    for (int y = 4; y <= 11; ++y) {
        int w = y <= 7 ? (y - 4) * 2 + 2 : (11 - y) * 2 + 2;
        for (int x = 8 - w / 2; x < 8 - w / 2 + w; ++x) a.set(x, y, kBase);
    }
    a.rect(5, 5, 10, 6, kLight);
    a.set(5, 5, kLight);
    a.set(8, 8, kDark); a.set(9, 9, kDark); a.set(8, 10, kDark);
    a.set(6, 7, kLight); // sparkle
}

void art_lump_impl(Art& a, bool charred_brown) {
    // Coal/charcoal chunk: irregular blob with facets.
    a.rect(5, 6, 10, 11, kBase);
    a.set(4, 7, kBase); a.set(4, 8, kBase);
    a.set(11, 7, kBase); a.set(11, 10, kBase);
    a.set(6, 5, kBase); a.set(9, 5, kBase);
    a.set(5, 10, kDark); a.rect(7, 10, 10, 11, kDark);
    a.set(6, 6, charred_brown ? kLight : kDark);
    a.set(9, 8, kLight);
}
void art_lump(Art& a, bool charred_brown = false) { art_lump_impl(a, charred_brown); }

void art_stick(Art& a) {
    for (int i = 0; i < 10; ++i) {
        a.set(4 + i / 3, 13 - i, kDark);
        a.set(5 + i / 3, 13 - i, kBase);
    }
}

void art_apple(Art& a) {
    for (int y = 6; y <= 12; ++y) {
        int w = (y == 6 || y == 12) ? 4 : (y == 7 || y == 11) ? 7 : 8;
        for (int x = 8 - w / 2; x < 8 - w / 2 + w; ++x) a.set(x, y, kBase);
    }
    a.set(5, 7, kLight); a.set(6, 7, kLight); a.set(5, 8, kLight);
    a.set(8, 4, kDark);  a.set(8, 5, kDark);              // stem
    a.set(9, 4, kAccent); a.set(10, 4, kAccent); a.set(10, 5, kAccent); // leaf
}

void art_meat_impl(Art& a, bool cooked) {
    a.rect(3, 6, 11, 11, kBase);
    a.set(4, 5, kBase); a.set(5, 5, kBase); a.set(10, 12, kBase); a.set(11, 12, kBase);
    if (cooked) {
        a.rect(4, 7, 10, 7, kDark); a.rect(4, 10, 10, 10, kDark); // grill marks
        a.rect(4, 6, 10, 6, kLight);
    } else {
        a.rect(5, 8, 9, 10, kLight); // fat marbling
        a.rect(3, 6, 3, 11, kAccent); a.rect(11, 6, 11, 11, kAccent); // raw edges
    }
}
void art_meat(Art& a, bool cooked = false) { art_meat_impl(a, cooked); }
void art_meat_cooked(Art& a) { art_meat_impl(a, true); }

void art_bow(Art& a) {
    // Arc + string.
    a.set(4, 2, kBase); a.set(5, 2, kBase);
    a.set(3, 3, kBase); a.set(3, 4, kBase);
    a.set(2, 5, kBase); a.set(2, 6, kBase); a.set(2, 7, kBase); a.set(2, 8, kBase);
    a.set(3, 9, kBase); a.set(3, 10, kBase);
    a.set(4, 11, kBase); a.set(5, 11, kBase);
    a.rect(6, 2, 12, 2, kLight); // string
    a.set(12, 3, kLight);
    a.rect(12, 4, 13, 9, kLight);
    a.set(12, 10, kLight); a.set(6, 11, kLight);
    a.rect(6, 10, 12, 10, kLight);
    // Grip.
    a.set(4, 6, kDark); a.set(4, 7, kDark);
}

void art_arrow(Art& a) {
    for (int i = 0; i < 9; ++i) a.set(4 + i, 11 - i, kBase); // shaft
    a.set(13, 2, kLight); a.set(12, 3, kLight); a.set(13, 3, kLight); // head
    a.set(11, 2, kLight);
    a.set(3, 12, kAccent); a.set(2, 13, kAccent); a.set(4, 13, kAccent); // fletching
    a.set(3, 11, kAccent); a.set(2, 12, kAccent);
}

void art_book(Art& a) {
    a.rect(3, 4, 11, 12, kBase);       // cover
    a.rect(4, 4, 11, 4, kDark);
    a.rect(12, 5, 13, 11, kLight);     // pages
    a.rect(3, 12, 11, 12, kDark);
    a.set(6, 8, kAccent); a.set(7, 8, kAccent); a.set(8, 8, kAccent); // clasp
}

// -- HUD stat sprites --------------------------------------------------------
// Palette: 5 = outline, 6 = fill, 7 = highlight, 8 = shadow.
constexpr uint8_t kOutline = 5, kFill = 6, kHi = 7, kShade = 8;

void blit_sprite(Canvas& c, const Art& a, uint8_t or_, uint8_t og, uint8_t ob,
                 uint8_t fr, uint8_t fg, uint8_t fb) {
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint8_t v = a.px[y * 16 + x];
            if (v == 0) continue;
            uint8_t r, g, b;
            switch (v) {
                case kOutline: r = or_;   g = og;   b = ob;   break;
                case kFill:    r = fr;    g = fg;    b = fb;    break;
                case kHi:      r = static_cast<uint8_t>(fr * 0.6f + 255 * 0.4f);
                               g = static_cast<uint8_t>(fg * 0.6f + 255 * 0.4f);
                               b = static_cast<uint8_t>(fb * 0.6f + 255 * 0.4f); break;
                case kShade:   r = static_cast<uint8_t>(fr * 0.55f);
                               g = static_cast<uint8_t>(fg * 0.55f);
                               b = static_cast<uint8_t>(fb * 0.55f); break;
                default:       r = fr;    g = fg;    b = fb;    break;
            }
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    c.set(x * 2 + dx, y * 2 + dy, r, g, b);
        }
    }
}

void sprite_heart(Art& a, int fill) { // 0 outline-only, 1 half (left lobe), 2 full
    auto put = [&](int x, int y, uint8_t v) {
        if (fill == 1 && x > 7 && v != kOutline) return; // half: right side empty
        a.set(x, y, v);
    };
    // Outline lobes.
    put(4, 4, kOutline); put(5, 3, kOutline); put(6, 3, kOutline);
    put(10, 3, kOutline); put(11, 3, kOutline); put(12, 4, kOutline);
    put(3, 4, kOutline); put(13, 4, kOutline);
    put(3, 5, kOutline); put(13, 5, kOutline);
    put(4, 6, kOutline); put(12, 6, kOutline);
    put(5, 7, kOutline); put(11, 7, kOutline);
    put(6, 8, kOutline); put(10, 8, kOutline);
    put(7, 9, kOutline); put(9, 9, kOutline);
    put(8, 10, kOutline);
    // Fill interior rows.
    for (int x = 4; x <= 6; ++x) put(x, 4, kFill);
    for (int x = 10; x <= 12; ++x) put(x, 4, kFill);
    for (int y = 5; y <= 5; ++y) for (int x = 4; x <= 12; ++x) put(x, y, kFill);
    for (int x = 5; x <= 11; ++x) put(x, 6, kFill);
    for (int x = 6; x <= 10; ++x) put(x, 7, kFill);
    for (int x = 7; x <= 9; ++x) put(x, 8, kFill);
    put(8, 9, kFill);
    // Highlight pixel (upper-left lobe).
    put(5, 4, kHi);
    put(4, 5, kHi);
}

void sprite_drumstick(Art& a, int fill) {
    auto put = [&](int x, int y, uint8_t v) {
        if (fill == 1 && x > 7 && v != kOutline) return;
        a.set(x, y, v);
    };
    // Meat blob (top-right), bone toward bottom-left.
    put(9, 3, kOutline); put(10, 3, kOutline); put(11, 3, kOutline);
    put(8, 4, kOutline); put(12, 4, kOutline);
    put(8, 5, kOutline); put(13, 5, kOutline);
    put(7, 6, kOutline); put(13, 6, kOutline);
    put(7, 7, kOutline); put(12, 7, kOutline);
    put(8, 8, kOutline);
    for (int x = 9; x <= 11; ++x) put(x, 4, kFill);
    for (int x = 9; x <= 12; ++x) put(x, 5, kFill);
    for (int x = 8; x <= 12; ++x) put(x, 6, kFill);
    for (int x = 8; x <= 11; ++x) put(x, 7, kFill);
    put(9, 6, kHi);
    // Bone.
    put(6, 9, kFill); put(7, 8, kFill);
    put(5, 10, kFill); put(6, 10, kFill);
    put(4, 11, kFill); put(5, 11, kFill);
    put(3, 12, kFill); put(4, 12, kFill);
    put(3, 13, kFill); put(4, 13, kFill);
}

void sprite_armor(Art& a, int fill) {
    auto put = [&](int x, int y, uint8_t v) {
        if (fill == 1 && x > 7 && v != kOutline) return;
        a.set(x, y, v);
    };
    // Chestplate silhouette.
    put(5, 3, kOutline); put(6, 3, kOutline); put(9, 3, kOutline); put(10, 3, kOutline);
    put(4, 4, kOutline); put(7, 4, kOutline); put(8, 4, kOutline); put(11, 4, kOutline);
    put(3, 5, kOutline); put(12, 5, kOutline);
    put(3, 6, kOutline); put(12, 6, kOutline);
    put(4, 7, kOutline); put(11, 7, kOutline);
    put(4, 8, kOutline); put(11, 8, kOutline);
    put(4, 9, kOutline); put(11, 9, kOutline);
    put(5, 10, kOutline); put(10, 10, kOutline);
    put(6, 11, kOutline); put(9, 11, kOutline);
    for (int y = 4; y <= 10; ++y) {
        for (int x = 5; x <= 10; ++x) {
            if (y == 4 && (x == 7 || x == 8)) continue; // neck gap
            put(x, y, kFill);
        }
    }
    put(5, 5, kHi);
}

void sprite_bubble(Art& a) {
    // Circle outline with water fill and a highlight arc.
    a.set(7, 3, kOutline); a.set(8, 3, kOutline);
    a.set(6, 4, kOutline); a.set(9, 4, kOutline);
    a.set(5, 5, kOutline); a.set(10, 5, kOutline);
    for (int y = 6; y <= 9; ++y) { a.set(4, y, kOutline); a.set(11, y, kOutline); }
    a.set(5, 10, kOutline); a.set(10, 10, kOutline);
    a.set(6, 11, kOutline); a.set(9, 11, kOutline);
    a.set(7, 12, kOutline); a.set(8, 12, kOutline);
    for (int y = 5; y <= 10; ++y)
        for (int x = 5; x <= 10; ++x)
            if (a.px[y * 16 + x] == 0) a.set(x, y, kFill);
    a.set(6, 5, kHi); a.set(5, 6, kHi);
}

} // namespace

void ItemIcons::generate(const TextureAtlas& atlas) {
    int max_cell = static_cast<int>(kSpriteHeartFull) + kSpriteCount; // items + HUD sprites
    rows_ = (max_cell + ATLAS_COLS - 1) / ATLAS_COLS;
    pixels_.assign(static_cast<size_t>(rows_) * ATLAS_COLS * ICON_PX * ICON_PX * 4, 0);

    auto cell_canvas = [&](int cell) {
        Canvas c;
        c.px.assign(ICON_PX * ICON_PX * 4, 0);
        int ox = (cell % ATLAS_COLS) * ICON_PX;
        int oy = (cell / ATLAS_COLS) * ICON_PX;
        return std::pair<Canvas, std::pair<int, int>>{std::move(c), {ox, oy}};
    };
    auto commit = [&](Canvas& c, int ox, int oy) {
        for (int y = 0; y < ICON_PX; ++y) {
            std::memcpy(pixels_.data() + ((oy + y) * ATLAS_COLS * ICON_PX + ox) * 4,
                        c.px.data() + (y * ICON_PX) * 4, ICON_PX * 4);
        }
    };

    auto paint = [&](int cell, auto&& fn) {
        auto [c, pos] = cell_canvas(cell);
        fn(c);
        commit(c, pos.first, pos.second);
    };

    // Block items (ids 1..BLOCK_COUNT-1): iso cubes / flat sprites.
    for (int id = 1; id < BLOCK_COUNT; ++id) {
        paint(icon_cell(static_cast<ItemId>(id)), [&](Canvas& c) {
            draw_block_icon(c, atlas, static_cast<BlockId>(id));
        });
    }

    // Registry items (256..): hand-drawn pixel art.
    auto with_mat = [&](ItemId id, const Material& m, auto&& art, uint8_t ar = 0,
                        uint8_t ag = 0, uint8_t ab = 0) {
        paint(icon_cell(id), [&](Canvas& c) {
            Art a;
            if constexpr (std::is_invocable_v<decltype(art), Art&>) {
                art(a);
            } else {
                art(a, false);
            }
            blit_art(c, a, m, ar, ag, ab);
        });
    };

    with_mat(ITEM_IRON_INGOT, kIron, art_ingot);
    with_mat(ITEM_GOLD_INGOT, kGold, art_ingot);
    with_mat(ITEM_DIAMOND, kDiamond, art_diamond);
    with_mat(ITEM_STICK, kWood, art_stick);
    with_mat(ITEM_COAL, kCoal, [](Art& a) { art_lump(a); }, 120, 120, 130);
    with_mat(ITEM_RAW_COPPER, {205, 110, 60, 150, 75, 40, 245, 170, 120},
             [](Art& a) { art_lump(a); }, 150, 120, 130);
    with_mat(ITEM_REDSTONE, {190, 30, 30, 130, 15, 15, 255, 90, 80},
             [](Art& a) { art_lump(a); }, 130, 110, 120);
    with_mat(ITEM_LAPIS, {40, 70, 190, 25, 45, 130, 100, 140, 240},
             [](Art& a) { art_lump(a); }, 120, 130, 160);
    with_mat(ITEM_CHARCOAL, kCharcoal, [](Art& a) { art_lump_impl(a, true); }, 140, 118, 96);
    with_mat(ITEM_APPLE, kApple, art_apple, 90, 160, 60);
    with_mat(ITEM_RAW_MEAT, kRawMeat, [](Art& a) { art_meat(a); }, 246, 240, 228);
    with_mat(ITEM_COOKED_MEAT, kCookedMeat, art_meat_cooked, 90, 50, 26);

    // Material variants need the material inside the art call — draw directly.
    auto tool = [&](ItemId id, const Material& m, bool sword) {
        paint(icon_cell(id), [&](Canvas& c) {
            Art a;
            if (sword) art_sword(a, true);
            else art_pickaxe(a);
            blit_art(c, a, m, 110, 80, 48); // leather guard/handle accents
        });
    };
    tool(ITEM_WOODEN_SWORD, kWood, true);
    tool(ITEM_STONE_SWORD, kStone, true);
    tool(ITEM_IRON_SWORD, kIron, true);
    tool(ITEM_DIAMOND_SWORD, kDiamond, true);
    tool(ITEM_WOODEN_PICKAXE, kWood, false);
    tool(ITEM_STONE_PICKAXE, kStone, false);
    tool(ITEM_IRON_PICKAXE, kIron, false);
    tool(ITEM_DIAMOND_PICKAXE, kDiamond, false);

    with_mat(ITEM_BOW, kWood, art_bow, 228, 228, 234);
    with_mat(ITEM_ARROW, kWood, art_arrow, 232, 232, 238);
    with_mat(ITEM_BOOK, kLeather, art_book, 200, 60, 60);

    // Custom icon overrides: assets/icons/<name>.png replaces the icon of
    // the item or block whose registry name matches the file stem
    // (case-insensitive). Any PNG resolution; box-downsampled to the cell.
    {
        std::error_code ec;
        const std::filesystem::path dir = "assets/icons";
        if (std::filesystem::exists(dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (ext != ".png") continue;
                std::string stem = entry.path().stem().string();
                for (char& ch : stem) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                ItemId id = ItemRegistry::id_from_name(stem);
                if (id == ITEM_AIR) {
                    MC_LOG_WARN("ItemIcons: no item named '{}' — ignoring assets/icons/{}",
                                stem, entry.path().filename().string());
                    continue;
                }
                int w = 0, h = 0, comp = 0;
                stbi_uc* p = stbi_load(entry.path().string().c_str(), &w, &h, &comp, 4);
                if (!p) {
                    MC_LOG_WARN("ItemIcons: failed to decode {}", entry.path().string());
                    continue;
                }
                int cell = icon_cell(id);
                int ox = (cell % ATLAS_COLS) * ICON_PX;
                int oy = (cell / ATLAS_COLS) * ICON_PX;
                // Center-crop to a square, then box-downsample to 32x32.
                int side = std::min(w, h);
                int sx0 = (w - side) / 2, sy0 = (h - side) / 2;
                for (int cy = 0; cy < ICON_PX; ++cy) {
                    int py0 = sy0 + cy * side / ICON_PX;
                    int py1 = sy0 + (cy + 1) * side / ICON_PX;
                    if (py1 <= py0) py1 = py0 + 1;
                    for (int cx = 0; cx < ICON_PX; ++cx) {
                        int px0 = sx0 + cx * side / ICON_PX;
                        int px1 = sx0 + (cx + 1) * side / ICON_PX;
                        if (px1 <= px0) px1 = px0 + 1;
                        int r = 0, g = 0, b = 0, a = 0, n = 0;
                        for (int py = py0; py < py1; ++py) {
                            for (int px = px0; px < px1; ++px) {
                                const stbi_uc* s = p + (static_cast<size_t>(py) * w + px) * 4;
                                r += s[0]; g += s[1]; b += s[2]; a += s[3]; ++n;
                            }
                        }
                        int i = ((oy + cy) * ATLAS_COLS * ICON_PX + ox + cx) * 4;
                        pixels_[i] = static_cast<uint8_t>(r / n);
                        pixels_[i + 1] = static_cast<uint8_t>(g / n);
                        pixels_[i + 2] = static_cast<uint8_t>(b / n);
                        pixels_[i + 3] = static_cast<uint8_t>(a / n);
                    }
                }
                stbi_image_free(p);
                MC_LOG_INFO("ItemIcons: custom icon for '{}' from {}", stem,
                            entry.path().filename().string());
            }
        }
    }

    // HUD stat sprites (UiSprite cells).
    auto heart = [&](int cell, int fill, uint8_t fr, uint8_t fg, uint8_t fb) {
        paint(cell, [&](Canvas& c) {
            Art a;
            sprite_heart(a, fill);
            blit_sprite(c, a, 24, 20, 26, fr, fg, fb);
        });
    };
    heart(kSpriteHeartFull, 2, 235, 40, 40);
    heart(kSpriteHeartHalf, 1, 235, 40, 40);
    heart(kSpriteHeartEmpty, 2, 62, 62, 70);
    auto drumstick = [&](int cell, int fill, uint8_t fr, uint8_t fg, uint8_t fb) {
        paint(cell, [&](Canvas& c) {
            Art a;
            sprite_drumstick(a, fill);
            blit_sprite(c, a, 24, 20, 26, fr, fg, fb);
        });
    };
    drumstick(kSpriteHungerFull, 2, 196, 116, 42);
    drumstick(kSpriteHungerHalf, 1, 196, 116, 42);
    drumstick(kSpriteHungerEmpty, 2, 62, 62, 70);
    auto chest = [&](int cell, int fill) {
        paint(cell, [&](Canvas& c) {
            Art a;
            sprite_armor(a, fill);
            blit_sprite(c, a, 24, 20, 26, 205, 205, 214);
        });
    };
    chest(kSpriteArmorFull, 2);
    chest(kSpriteArmorHalf, 1);
    chest(kSpriteArmorEmpty, 0);
    paint(kSpriteBubble, [&](Canvas& c) {
        Art a;
        sprite_bubble(a);
        blit_sprite(c, a, 24, 34, 54, 120, 190, 240);
    });
}

void ItemIcons::upload() {
    shutdown();
    glGenTextures(1, &gl_tex_);
    glBindTexture(GL_TEXTURE_2D, gl_tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ATLAS_COLS * ICON_PX, rows_ * ICON_PX, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ItemIcons::shutdown() {
    if (gl_tex_ != 0) glDeleteTextures(1, &gl_tex_);
    gl_tex_ = 0;
}

void ItemIcons::bind(uint32_t slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, gl_tex_);
}

void ItemIcons::uv_for_cell(int cell, float& u0, float& v0, float& u1, float& v1) const {
    int cx = cell % ATLAS_COLS, cy = cell / ATLAS_COLS;
    float w = static_cast<float>(ATLAS_COLS * ICON_PX);
    float h = static_cast<float>(rows_ * ICON_PX);
    u0 = static_cast<float>(cx * ICON_PX) / w;
    v0 = static_cast<float>(cy * ICON_PX) / h;
    u1 = static_cast<float>((cx + 1) * ICON_PX) / w;
    v1 = static_cast<float>((cy + 1) * ICON_PX) / h;
}

void ItemIcons::uv_for(ItemId id, float& u0, float& v0, float& u1, float& v1) const {
    uv_for_cell(icon_cell(id), u0, v0, u1, v1);
}

} // namespace mc
