#include "renderer/ui.hpp"
#include "renderer/ui_theme.hpp"

#include <algorithm>
#include <climits>
#include <cstring>
#include <cmath>

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/logger.hpp"

namespace mc {

namespace {

#include "renderer/font8x8_basic.h"

// Polish diacritics, composed from the base ASCII glyphs plus accent pixels
// (ogonek / acute / dot). Rendered into extra font atlas cells.
struct PolishGlyph {
    unsigned cp;      // Unicode codepoint (UTF-8 decoded)
    char base;        // base ASCII glyph
    uint8_t acc_top;  // pixels OR-ed into row 0 (acute / dot)
    uint8_t acc_mid;  // pixels OR-ed into mid_row (l/L stroke)
    int mid_row;
    uint8_t acc_bot;  // pixels OR-ed into row 6/7 (ogonek)
};

const PolishGlyph kPolish[] = {
    {0x0105, 'a', 0,    0,    -1, 0x18}, // ą
    {0x0107, 'c', 0x30, 0,    -1, 0},    // ć
    {0x0119, 'e', 0,    0,    -1, 0x18}, // ę
    {0x0142, 'l', 0,    0x18, 3,  0},    // ł
    {0x0144, 'n', 0x30, 0,    -1, 0},    // ń
    {0x00F3, 'o', 0x20, 0,    -1, 0},    // ó
    {0x015B, 's', 0x30, 0,    -1, 0},    // ś
    {0x017A, 'z', 0x30, 0,    -1, 0},    // ź
    {0x017C, 'z', 0x18, 0,    -1, 0},    // ż
    {0x0104, 'A', 0,    0,    -1, 0x18}, // Ą
    {0x0106, 'C', 0x30, 0,    -1, 0},    // Ć
    {0x0118, 'E', 0,    0,    -1, 0x18}, // Ę
    {0x0141, 'L', 0,    0x30, 3,  0},    // Ł
    {0x0143, 'N', 0x30, 0,    -1, 0},    // Ń
    {0x00D3, 'O', 0x20, 0,    -1, 0},    // Ó
    {0x015A, 'S', 0x30, 0,    -1, 0},    // Ś
    {0x0179, 'Z', 0x30, 0,    -1, 0},    // Ź
    {0x017B, 'Z', 0x18, 0,    -1, 0},    // Ż
};
constexpr int kPolishCount = static_cast<int>(sizeof(kPolish) / sizeof(kPolish[0]));
constexpr int kPolishGlyphBase = 96; // atlas cells 96..96+kPolishCount-1

int polish_glyph_index(unsigned cp) {
    for (int i = 0; i < kPolishCount; ++i) {
        if (kPolish[i].cp == cp) return kPolishGlyphBase + i;
    }
    return -1;
}

// 8x8 bitmap font pixel accessor
[[nodiscard]] bool font_pixel(char c, int px, int py) {
    if (c < 0 || c > 127) return false;
    if (px < 0 || px >= 8 || py < 0 || py >= 8) return false;

    char row = font8x8_basic[static_cast<int>(c)][py];
    return (row & (1 << px)) != 0;
}

// Compose a Polish glyph's 8 rows from its base glyph + accents.
void polish_glyph_rows(int polish_idx, uint8_t (&rows)[8]) {
    const PolishGlyph& g = kPolish[polish_idx];
    for (int py = 0; py < 8; ++py) {
        uint8_t row = 0;
        for (int px = 0; px < 8; ++px) {
            if (font_pixel(g.base, px, py)) row |= static_cast<uint8_t>(1u << px);
        }
        rows[py] = row;
    }
    rows[0] |= g.acc_top;
    if (g.mid_row >= 0 && g.mid_row < 8) rows[g.mid_row] |= g.acc_mid;
    rows[6] |= g.acc_bot;
    if (g.acc_bot && g.base != 'a' && g.base != 'e') rows[7] |= g.acc_bot; // uppercase ogonek sits lower
}

} // namespace

// ---------------------------------------------------------------------------
// TTF text engine (Silkscreen, OFL). One 10 px raster magnified 5x turned menu
// titles into blur, so we bake a ladder of raster sizes at init and pick the
// smallest one that is at least as tall as the drawn cell — glyphs render at
// (or slightly below) native resolution at every scale the UI uses.
// Layout contract with the rest of the UI is unchanged: monospace advance
// 8 * scale, glyph box 12 * 0.8 * scale.
// ---------------------------------------------------------------------------
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

namespace {
constexpr int kTtfCols = 16; // atlas cells per row

struct TtfRaster {
    bool ok = false;
    uint32_t tex = 0;
    int cell = 0;            // atlas cell size in px
    std::vector<float> uv;   // per glyph tight bbox: u0, v0, u1, v1
    std::vector<float> pos;  // per glyph in-cell rect: gx, gy, gw, gh (px)
};

// Drawn cell height is 9.6 * scale, so HUD scales (1.0–1.6), panels (2.0–3.0)
// and menu titles (5.0) each land on an equal-or-larger raster.
constexpr float kRasterSizes[] = {10, 12, 14, 16, 20, 24, 28, 34, 40, 48};
std::vector<TtfRaster> g_ttf;    // regular ladder
std::vector<TtfRaster> g_ttf_b;  // bold ladder (empty when Bold.ttf missing)
bool g_ttf_ok = false;

std::vector<int> ttf_codepoints() {
    std::vector<int> cps;
    for (int c = 32; c < 128; ++c) cps.push_back(c);
    for (const auto& g : kPolish) cps.push_back(static_cast<int>(g.cp));
    return cps;
}

// Silkscreen's cmap covers Latin-1 only (ó/Ó), so the remaining Polish
// diacritics are composed: rasterize the base ASCII glyph, then stamp accent
// rectangles on the 8px design grid (same idea as polish_glyph_rows for the
// embedded bitmap font). Accent bounds are accumulated into acc_* so the
// recorded tight rect includes them.
void overlay_polish_accents(std::vector<uint8_t>& atlas, int atlas_stride, int cell,
                            const PolishGlyph& g, int gx, int gy, int gw, int gh,
                            float pixel_h, int& acc_x0, int& acc_y0, int& acc_x1, int& acc_y1) {
    const int s = std::max(1, static_cast<int>(std::lround(pixel_h / 8.0f)));
    acc_x0 = acc_y0 = INT_MAX;
    acc_x1 = acc_y1 = INT_MIN;
    auto fill = [&](float x, float y, float w, float hgt) {
        int x0 = static_cast<int>(std::lround(x));
        int y0 = static_cast<int>(std::lround(y));
        int x1 = x0 + static_cast<int>(std::lround(w));
        int y1 = y0 + static_cast<int>(std::lround(hgt));
        acc_x0 = std::min(acc_x0, x0); acc_y0 = std::min(acc_y0, y0);
        acc_x1 = std::max(acc_x1, x1); acc_y1 = std::max(acc_y1, y1);
        for (int yy = y0; yy < y1; ++yy) {
            if (yy < 0 || yy >= cell) continue;
            for (int xx = x0; xx < x1; ++xx) {
                if (xx < 0 || xx >= cell) continue;
                atlas[static_cast<size_t>(yy) * atlas_stride + xx] = 255;
            }
        }
    };
    const bool lower = (g.base >= 'a' && g.base <= 'z');
    const float top_y = gy + (lower ? gh * 0.30f : 0.0f) - s * 1.35f;
    if (g.acc_top) {
        for (int px = 0; px < 8; ++px) {
            if (g.acc_top & (1 << px)) fill(gx + (px - 1) * s, top_y, s, s);
        }
    }
    if (g.mid_row >= 0) {
        // ł / Ł: bar across the stem, slightly wider than the glyph body.
        fill(gx - s * 0.35f, gy + gh * 0.40f, gw + s * 0.7f, s);
    }
    if (g.acc_bot) {
        for (int px = 0; px < 8; ++px) {
            if (g.acc_bot & (1 << px)) {
                fill(gx + (px - 1) * s, gy + gh - s * 0.45f, s, s * 1.15f);
            }
        }
    }
}

bool load_ttf_bytes(const char* path, std::vector<unsigned char>& buf) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    buf.resize(static_cast<size_t>(size));
    size_t rd = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return rd == buf.size() && size > 0;
}

bool build_ttf_ladder(const std::vector<unsigned char>& buf, std::vector<TtfRaster>& ladder) {
    ladder.resize(std::size(kRasterSizes));
    for (size_t li = 0; li < std::size(kRasterSizes); ++li) {
        const float pixel_h = kRasterSizes[li];
        TtfRaster& r = ladder[li];
        stbtt_fontinfo font;
        if (!stbtt_InitFont(&font, const_cast<unsigned char*>(buf.data()), 0)) return false;

        const auto cps = ttf_codepoints();
        const int count = static_cast<int>(cps.size());
        // Cell is tall enough for accents above caps and ogonki below baseline;
        // baseline sits at 2/3 of the cell like the 12px original (8/12).
        const int cell = static_cast<int>(std::lround(pixel_h * 1.35));
        const int baseline = static_cast<int>(std::lround(cell * (2.0 / 3.0)));
        const int pad = std::max(1, cell / 12);

        const int rows = (count + kTtfCols - 1) / kTtfCols;
        std::vector<uint8_t> atlas(static_cast<size_t>(kTtfCols) * cell * rows * cell, 0);
        r.uv.assign(static_cast<size_t>(count) * 4, 0.0f);
        r.pos.assign(static_cast<size_t>(count) * 4, 0.0f);
        r.cell = cell;

        float scale = stbtt_ScaleForPixelHeight(&font, pixel_h);
        for (int gi = 0; gi < count; ++gi) {
            const PolishGlyph* pg = (gi >= kPolishGlyphBase) ? &kPolish[gi - kPolishGlyphBase] : nullptr;
            int w = 0, h = 0, xoff = 0, yoff = 0;
            unsigned char* bmp = stbtt_GetCodepointBitmap(&font, 0, scale, cps[gi],
                                                          &w, &h, &xoff, &yoff);
            // Silkscreen's cmap only covers Latin-1 (ó/Ó). Every other Polish
            // codepoint resolves to the .notdef box (or nothing), so compose
            // those from the base ASCII glyph + stamped accents instead.
            const bool has_native = pg && (cps[gi] == 0x00F3 || cps[gi] == 0x00D3);
            if (pg && !has_native) {
                stbtt_FreeBitmap(bmp, nullptr);
                bmp = stbtt_GetCodepointBitmap(&font, 0, scale, pg->base, &w, &h, &xoff, &yoff);
            }
            int col = gi % kTtfCols, row = gi / kTtfCols;
            int ox = col * cell, oy = row * cell;
            if (bmp) {
                int gx = pad + xoff;
                int gy = baseline + yoff;
                for (int y = 0; y < h; ++y) {
                    int dy = gy + y;
                    if (dy < 0 || dy >= cell) continue;
                    for (int x = 0; x < w; ++x) {
                        int dx = gx + x;
                        if (dx < 0 || dx >= cell) continue;
                        atlas[(static_cast<size_t>(oy + dy) * (kTtfCols * cell)) + ox + dx] =
                            bmp[y * w + x];
                    }
                }
                // Tight rect starts as the base glyph's bbox; Polish accents
                // may extend it below.
                int rx0 = gx, ry0 = gy, rx1 = gx + w, ry1 = gy + h;
                if (pg) {
                    int ax0, ay0, ax1, ay1;
                    overlay_polish_accents(atlas, kTtfCols * cell, cell, *pg,
                                           gx, gy, w, h, pixel_h, ax0, ay0, ax1, ay1);
                    if (ax0 != INT_MAX) {
                        rx0 = std::min(rx0, ax0); ry0 = std::min(ry0, ay0);
                        rx1 = std::max(rx1, ax1); ry1 = std::max(ry1, ay1);
                    }
                }
                float tw = static_cast<float>(kTtfCols * cell);
                float th = static_cast<float>(rows * cell);
                r.uv[static_cast<size_t>(gi) * 4 + 0] = static_cast<float>(ox + rx0) / tw;
                r.uv[static_cast<size_t>(gi) * 4 + 1] = static_cast<float>(oy + ry0) / th;
                r.uv[static_cast<size_t>(gi) * 4 + 2] = static_cast<float>(ox + rx1) / tw;
                r.uv[static_cast<size_t>(gi) * 4 + 3] = static_cast<float>(oy + ry1) / th;
                r.pos[static_cast<size_t>(gi) * 4 + 0] = static_cast<float>(rx0);
                r.pos[static_cast<size_t>(gi) * 4 + 1] = static_cast<float>(ry0);
                r.pos[static_cast<size_t>(gi) * 4 + 2] = static_cast<float>(rx1 - rx0);
                r.pos[static_cast<size_t>(gi) * 4 + 3] = static_cast<float>(ry1 - ry0);
                stbtt_FreeBitmap(bmp, nullptr);
            }
        }

        glGenTextures(1, &r.tex);
        glBindTexture(GL_TEXTURE_2D, r.tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kTtfCols * cell, rows * cell, 0,
                     GL_RED, GL_UNSIGNED_BYTE, atlas.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        r.ok = true;
    }
    return true;
}

// Smallest raster whose cell covers the drawn cell height (12 * 0.8 * scale);
// falls back to the largest available when the ladder tops out.
const TtfRaster* select_raster(float scale, bool bold) {
    const std::vector<TtfRaster>& lad = (bold && !g_ttf_b.empty()) ? g_ttf_b : g_ttf;
    const float drawn = 12.0f * 0.8f * scale;
    const TtfRaster* best = nullptr;
    for (const auto& r : lad) {
        if (!r.ok) continue;
        if (!best || r.cell > best->cell) best = &r;
        if (static_cast<float>(r.cell) >= drawn) break;
    }
    return best;
}

bool load_ttf_font() {
    std::vector<unsigned char> reg, bold;
    if (!load_ttf_bytes("assets/fonts/Silkscreen-Regular.ttf", reg)) return false;
    g_ttf_ok = build_ttf_ladder(reg, g_ttf);
    if (g_ttf_ok && load_ttf_bytes("assets/fonts/Silkscreen-Bold.ttf", bold)) {
        build_ttf_ladder(bold, g_ttf_b);
    }
    return g_ttf_ok;
}

// Decodes one UTF-8 codepoint at i (advances i). Returns U+FFFD on malformed
// sequences; supports 1–4 byte encodings (Polish letters are 2-byte).
unsigned decode_utf8(std::string_view s, size_t& i) {
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) { ++i; return b0; }
    int n; unsigned cp;
    if ((b0 & 0xE0) == 0xC0) { n = 2; cp = b0 & 0x1Fu; }
    else if ((b0 & 0xF0) == 0xE0) { n = 3; cp = b0 & 0x0Fu; }
    else if ((b0 & 0xF8) == 0xF0) { n = 4; cp = b0 & 0x07u; }
    else { ++i; return 0xFFFDu; }
    if (i + static_cast<size_t>(n) > s.size()) { ++i; return 0xFFFDu; }
    for (int k = 1; k < n; ++k) {
        unsigned char b = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
        if ((b & 0xC0) != 0x80) { ++i; return 0xFFFDu; }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    i += static_cast<size_t>(n);
    return cp;
}

void push_utf8(std::string& buf, unsigned cp) {
    if (cp < 0x80) {
        buf.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        buf.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        buf.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        buf.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool ui_accepts_char(unsigned cp) {
    if (cp >= 32 && cp < 127) return true;
    return polish_glyph_index(static_cast<int>(cp)) >= 0;
}
} // namespace

bool UIRenderer::init(int screen_w, int screen_h) {
    screen_w_ = screen_w;
    screen_h_ = screen_h;

    if (!shader_.load_from_files("shaders/ui.vert", "shaders/ui.frag")) {
        MC_LOG_ERROR("Failed to load UI shaders");
        return false;
    }

    gen_font_texture();
    g_ttf_ok = load_ttf_font();
    if (g_ttf_ok) {
        MC_LOG_INFO("UI font: Silkscreen TTF ladder loaded ({} rasters, bold: {})",
                    g_ttf.size(), !g_ttf_b.empty());
    } else {
        MC_LOG_WARN("UI font: assets/fonts/Silkscreen-Regular.ttf missing — bitmap fallback");
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex), (void*)(4 * sizeof(float)));
    glBindVertexArray(0);

    verts_.reserve(4096);
    return true;
}

void UIRenderer::gen_font_texture() {
    const int cols = 16;
    const int cell_w = 8;
    const int cell_h = 8;
    const int total_glyphs = FONT_CHARS + kPolishCount;
    const int tex_w = cols * cell_w;   // 128
    const int tex_h = ((total_glyphs / cols) + 1) * cell_h;

    std::vector<uint8_t> data(tex_w * tex_h, 0);
    for (int c = 0; c < total_glyphs; ++c) {
        int col = c % cols;
        int row = c / cols;
        int ox = col * cell_w;
        int oy = row * cell_h;
        uint8_t rows[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        if (c < FONT_CHARS) {
            char ch = static_cast<char>(c + 32);
            for (int py = 0; py < 8; ++py) {
                for (int px = 0; px < 8; ++px) {
                    if (font_pixel(ch, px, py)) rows[py] |= static_cast<uint8_t>(1u << px);
                }
            }
        } else {
            polish_glyph_rows(c - kPolishGlyphBase, rows);
        }
        for (int py = 0; py < 8; ++py) {
            for (int px = 0; px < 8; ++px) {
                if (rows[py] & (1u << px)) {
                    data[(oy + py) * tex_w + ox + px] = 255;
                }
            }
        }
    }

    glGenTextures(1, &font_tex_);
    glBindTexture(GL_TEXTURE_2D, font_tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tex_w, tex_h, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void UIRenderer::shutdown() {
    shader_.destroy();
    if (font_tex_) glDeleteTextures(1, &font_tex_);
    for (auto& r : g_ttf) {
        if (r.tex) glDeleteTextures(1, &r.tex);
        r.tex = 0;
        r.ok = false;
    }
    for (auto& r : g_ttf_b) {
        if (r.tex) glDeleteTextures(1, &r.tex);
        r.tex = 0;
        r.ok = false;
    }
    g_ttf_ok = false;
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    font_tex_ = vao_ = vbo_ = 0;
}

void UIRenderer::begin_frame() {
    verts_.clear();
    quad_tag_.clear();
    icon_verts_.clear();
    frame_tex_.clear();
    frame_tex_.push_back(font_tex_); // tag 0: embedded bitmap font
}

void UIRenderer::end_frame() {
    flush();
    flush_icons();
    pending_chars_.clear();
}

void UIRenderer::draw_icon_quad(float x, float y, float w, float h,
                                float u0, float v0, float u1, float v1,
                                uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    size_t base = icon_verts_.size();
    icon_verts_.resize(base + 6);
    UiVertex v{};
    v.r = r; v.g = g; v.b = b; v.a = a;
    v.u = u0; v.v = v0; v.x = x;     v.y = y;
    icon_verts_[base + 0] = v;
    v.u = u1; v.v = v0; v.x = x + w; v.y = y;
    icon_verts_[base + 1] = v;
    v.u = u1; v.v = v1; v.x = x + w; v.y = y + h;
    icon_verts_[base + 2] = v;
    v.u = u0; v.v = v0; v.x = x;     v.y = y;
    icon_verts_[base + 3] = v;
    v.u = u1; v.v = v1; v.x = x + w; v.y = y + h;
    icon_verts_[base + 4] = v;
    v.u = u0; v.v = v1; v.x = x;     v.y = y + h;
    icon_verts_[base + 5] = v;
}

void UIRenderer::flush_icons() {
    if (icon_verts_.empty() || icon_tex_ == 0) {
        icon_verts_.clear();
        return;
    }

    shader_.use();
    glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(screen_w_),
                                static_cast<float>(screen_h_), 0.0f, -1.0f, 1.0f);
    shader_.set_mat4("u_proj", glm::value_ptr(proj));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, icon_tex_);
    shader_.set_int("u_atlas", 0);
    shader_.set_int("u_textured", 2); // RGBA sprite pass

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, icon_verts_.size() * sizeof(UiVertex),
                 icon_verts_.data(), GL_STREAM_DRAW);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(icon_verts_.size()));

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    icon_verts_.clear();
}

void UIRenderer::flush() {
    if (verts_.empty()) return;

    shader_.use();
    glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(screen_w_),
                                static_cast<float>(screen_h_), 0.0f, -1.0f, 1.0f);
    shader_.set_mat4("u_proj", glm::value_ptr(proj));

    glActiveTexture(GL_TEXTURE0);
    shader_.set_int("u_atlas", 0);
    shader_.set_int("u_textured", 1); // font mask pass

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(UiVertex), verts_.data(), GL_STREAM_DRAW);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Quads carry a texture tag (bitmap font / one per TTF raster); solid
    // rects are wildcards. Draw coalesced runs in insertion order so z-like
    // layering (panel bg, then text, then overlay rect) is preserved even
    // though consecutive runs may bind different textures.
    auto bind_tag = [&](uint8_t tag) {
        uint32_t tex = tag < frame_tex_.size() ? frame_tex_[tag] : frame_tex_[0];
        glBindTexture(GL_TEXTURE_2D, tex);
    };
    const size_t quads = quad_tag_.size();
    size_t start = 0;
    uint8_t running = kTagSolid;
    for (size_t q = 0; q < quads; ++q) {
        uint8_t tag = quad_tag_[q];
        if (tag == kTagSolid && q > 0) tag = running;
        if (q == 0) {
            running = tag;
            bind_tag(tag);
        } else if (tag != running) {
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(start),
                         static_cast<GLsizei>(q * 6 - start));
            start = q * 6;
            running = tag;
            bind_tag(tag);
        }
    }
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(start),
                 static_cast<GLsizei>(verts_.size() - start));

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    verts_.clear();
    quad_tag_.clear();
}

void UIRenderer::push_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t tag) {
    UiVertex v{};
    v.u = u0; v.v = v0; v.r = r; v.g = g; v.b = b; v.a = a;
    v.x = x;       v.y = y;
    verts_.push_back(v);
    v.x = x + w;   v.y = y;       v.u = u1; v.v = v0;
    verts_.push_back(v);
    v.x = x;       v.y = y + h;   v.u = u0; v.v = v1;
    verts_.push_back(v);
    v.x = x;       v.y = y + h;   v.u = u0; v.v = v1;
    verts_.push_back(v);
    v.x = x + w;   v.y = y;       v.u = u1; v.v = v0;
    verts_.push_back(v);
    v.x = x + w;   v.y = y + h;   v.u = u1; v.v = v1;
    verts_.push_back(v);
    quad_tag_.push_back(tag);
}

void UIRenderer::draw_rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    push_quad(x, y, w, h, -1.0f, -1.0f, -1.0f, -1.0f, r, g, b, a);
}

void UIRenderer::draw_glass_panel(float x, float y, float w, float h) {
    // Classic Minecraft panel: drop shadow, light body, white/dark bevels,
    // thin dark outline.
    draw_rect(x + 4, y + 4, w, h, 0, 0, 0, ui::kAlphaSoft);
    draw_rect(x - 1, y - 1, w + 2, h + 2, 0, 0, 0, 255);                       // outline
    draw_rect(x, y, w, h, ui::kPanelR, ui::kPanelG, ui::kPanelB, ui::kPanelA); // body
    draw_rect(x, y, w, 2, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 255);
    draw_rect(x, y, 2, h, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 255);
    draw_rect(x, y + h - 2, w, 2, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
    draw_rect(x + w - 2, y, 2, h, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
}

// Dark translucent HUD band with a gray frame (hotbar).
void UIRenderer::draw_hotbar_bg(float x, float y, float w, float h) {
    draw_rect(x + 2, y + 3, w, h, 0, 0, 0, ui::kAlphaSoft);
    draw_rect(x, y, w, h, ui::kHotbarR, ui::kHotbarG, ui::kHotbarB, ui::kHotbarA);
    draw_rect(x, y, w, 1, ui::kHotbarFrameR, ui::kHotbarFrameG, ui::kHotbarFrameB, 180);
    draw_rect(x, y + h - 1, w, 1, ui::kHotbarFrameR, ui::kHotbarFrameG, ui::kHotbarFrameB, 180);
    draw_rect(x, y, 1, h, ui::kHotbarFrameR, ui::kHotbarFrameG, ui::kHotbarFrameB, 180);
    draw_rect(x + w - 1, y, 1, h, ui::kHotbarFrameR, ui::kHotbarFrameG, ui::kHotbarFrameB, 180);
}

void UIRenderer::push_text_char(int cp, float x, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool bold) {
    if (g_ttf_ok) {
        // Cell index matches the bitmap font's (ASCII then Polish).
        int idx;
        if (cp >= 128) {
            bool found = false;
            for (int i = 0; i < kPolishCount; ++i) {
                if (kPolish[i].cp == static_cast<unsigned>(cp)) {
                    idx = 96 + i;
                    found = true;
                    break;
                }
            }
            if (!found) return;
        } else {
            idx = cp - 32;
            if (idx < 0 || idx >= FONT_CHARS) return;
        }
        const TtfRaster* ras = select_raster(scale, bold);
        if (!ras) return;
        if (idx >= static_cast<int>(ras->uv.size() / 4)) return;

        uint8_t tag = 0;
        for (size_t i = 1; i < frame_tex_.size(); ++i) {
            if (frame_tex_[i] == ras->tex) { tag = static_cast<uint8_t>(i); break; }
        }
        if (tag == 0) {
            frame_tex_.push_back(ras->tex);
            tag = static_cast<uint8_t>(frame_tex_.size() - 1);
        }

        // Glyph quads use the tight rasterized bbox scaled so the raster's
        // cell lands exactly on the shared 12 * 0.8 * scale cell — baselines
        // and advances line up across raster sizes.
        const float* pos = &ras->pos[static_cast<size_t>(idx) * 4];
        const float* uv = &ras->uv[static_cast<size_t>(idx) * 4];
        const float k = (12.0f * 0.8f * scale) / static_cast<float>(ras->cell);
        push_quad(x + pos[0] * k, y + pos[1] * k, pos[2] * k, pos[3] * k,
                  uv[0], uv[1], uv[2], uv[3], r, g, b, a, tag);
        return;
    }

    int idx;
    if (cp >= 128) {
        idx = polish_glyph_index(static_cast<unsigned>(cp));
        if (idx < 0) return;
    } else {
        idx = cp - 32;
        if (idx < 0 || idx >= FONT_CHARS) return;
    }

    const int cols = 16;
    const int cell_w = 8;
    const int cell_h = 8;
    const int total_glyphs = FONT_CHARS + kPolishCount;
    const int tex_w = cols * cell_w;
    const int tex_h = ((total_glyphs / cols) + 1) * cell_h;

    int col = idx % cols;
    int row = idx / cols;
    float u0 = static_cast<float>(col * cell_w) / tex_w;
    float v0 = static_cast<float>(row * cell_h) / tex_h;
    float u1 = static_cast<float>(col * cell_w + 8) / tex_w;
    float v1 = static_cast<float>(row * cell_h + 8) / tex_h;

    push_quad(x, y, 8.0f * scale, 8.0f * scale, u0, v0, u1, v1, r, g, b, a, 0);
}

void UIRenderer::draw_text(std::string_view text, float x, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool bold) {
    float cx = x;
    float shadow = std::max(1.0f, scale);
    uint8_t sa = static_cast<uint8_t>(a * ui::kTextShadowAlpha / 255);
    for (size_t i = 0; i < text.size(); ) {
        unsigned cp = decode_utf8(text, i);
        if (cp == 0xFFFDu) continue; // malformed or unsupported — skip
        push_text_char(static_cast<int>(cp), cx + shadow, y + shadow, scale, 0, 0, 0, sa, bold);
        push_text_char(static_cast<int>(cp), cx, y, scale, r, g, b, a, bold);
        cx += FONT_W * scale;
    }
}

void UIRenderer::draw_text_centered(std::string_view text, float cx, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool bold) {
    float w = text_width(text, scale);
    draw_text(text, cx - w * 0.5f, y, scale, r, g, b, a, bold);
}

void UIRenderer::draw_button_bg(float x, float y, float w, float h, bool hovered, bool pressed) {
    // Classic Minecraft button: gray slab, light/dark bevel, black outline.
    draw_rect(x + 2, y + 3, w, h, 0, 0, 0, ui::kAlphaSoft); // drop shadow
    uint8_t body_r = hovered ? ui::kBtnHoverR : ui::kBtnR;
    uint8_t body_g = hovered ? ui::kBtnHoverG : ui::kBtnG;
    uint8_t body_b = hovered ? ui::kBtnHoverB : ui::kBtnB;
    if (pressed) {
        body_r = ui::kBtnPressR; body_g = ui::kBtnPressG; body_b = ui::kBtnPressB;
    }

    draw_rect(x - 1, y - 1, w + 2, h + 2, 0, 0, 0, 255); // black outline
    draw_rect(x, y, w, h, body_r, body_g, body_b, 255);
    if (pressed) {
        // Inverted bevel while pressed.
        draw_rect(x, y, w, 2, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
        draw_rect(x, y, 2, h, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
        draw_rect(x, y + h - 2, w, 2, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 255);
        draw_rect(x + w - 2, y, 2, h, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 255);
    } else {
        draw_rect(x, y, w, 2, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 255);
        draw_rect(x, y, 2, h, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 255);
        draw_rect(x, y + h - 2, w, 2, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
        draw_rect(x + w - 2, y, 2, h, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
    }
}

bool UIRenderer::button(std::string_view label, float x, float y, float w, float h) {
    return button(label, x, y, w, h, 1.5f);
}

bool UIRenderer::button(std::string_view label, float x, float y, float w, float h, float font_scale) {
    bool hovered = mouse_x_ >= x && mouse_x_ <= x + w && mouse_y_ >= y && mouse_y_ <= y + h;
    bool pressed = hovered && mouse_down_;
    bool clicked = hovered && mouse_click_;
    if (clicked) {
        MC_LOG_TRACE("Button '{}' clicked (x={}, y={}, w={}, h={})", label, x, y, w, h);
    }

    draw_button_bg(x, y, w, h, hovered, pressed);

    float text_w = text_width(label, font_scale);
    float text_h = FONT_H * font_scale * 0.5f;
    if (hovered) {
        draw_text(label, x + (w - text_w) * 0.5f, y + (h - text_h) * 0.5f - (pressed ? 1.0f : 0.0f),
                  font_scale, ui::kTextHoverR, ui::kTextHoverG, ui::kTextHoverB);
    } else {
        draw_text(label, x + (w - text_w) * 0.5f, y + (h - text_h) * 0.5f, font_scale,
                  ui::kTextLight, ui::kTextLight, ui::kTextLight);
    }
    return clicked;
}

bool UIRenderer::button_dark(std::string_view label, float x, float y, float w, float h, float font_scale) {
    bool hovered = mouse_x_ >= x && mouse_x_ <= x + w && mouse_y_ >= y && mouse_y_ <= y + h;
    bool pressed = hovered && mouse_down_;
    bool clicked = hovered && mouse_click_;
    draw_button_bg(x, y, w, h, hovered, pressed);
    float text_w = text_width(label, font_scale);
    float text_h = FONT_H * font_scale * 0.5f;
    const uint8_t cr = hovered ? 30 : 55, cg = hovered ? 34 : 58, cb = hovered ? 48 : 72;
    draw_text(label, x + (w - text_w) * 0.5f, y + (h - text_h) * 0.5f - (pressed ? 1.0f : 0.0f),
              font_scale, cr, cg, cb);
    return clicked;
}

UIRenderer::TextInputResult UIRenderer::text_input(std::string& buffer, float x, float y, float w, float h, bool& active, float font_scale) {
    TextInputResult result;
    bool hovered = mouse_x_ >= x && mouse_x_ <= x + w && mouse_y_ >= y && mouse_y_ <= y + h;
    if (mouse_click_) active = hovered;

    // Drop shadow
    draw_rect(x + 2, y + 2, w, h, 0, 0, 0, 70);
    // Body (dark, like MC text fields)
    draw_rect(x, y, w, h, 12, 12, 14, 220);
    // Borders
    if (active) {
        draw_rect(x - 1, y - 1, w + 2, h + 2, 255, 255, 255, 230);
        draw_rect(x, y, w, h, 12, 12, 14, 220);
    } else {
        draw_rect(x, y, w, 1, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
        draw_rect(x, y, 1, h, ui::kBevelDark, ui::kBevelDark, ui::kBevelDark, 255);
        draw_rect(x, y + h - 1, w, 1, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 200);
        draw_rect(x + w - 1, y, 1, h, ui::kBevelLight, ui::kBevelLight, ui::kBevelLight, 200);
    }

    if (active && !pending_chars_.empty()) {
        for (unsigned int ch : pending_chars_) {
            if (ch == '\b') {
                // Pop one UTF-8 codepoint (buffer is UTF-8, not ASCII).
                while (!buffer.empty() &&
                       (static_cast<unsigned char>(buffer.back()) & 0xC0) == 0x80) {
                    buffer.pop_back();
                }
                if (!buffer.empty()) buffer.pop_back();
                result.changed = true;
            } else if (ui_accepts_char(ch) && buffer.size() < 64) {
                // Full sequence must fit; never split a codepoint at the cap.
                std::string enc;
                push_utf8(enc, ch);
                if (buffer.size() + enc.size() <= 64) {
                    buffer += enc;
                    result.changed = true;
                }
            }
        }
        pending_chars_.clear();
    }

    if (active && pending_key_action_ == GLFW_PRESS && pending_key_ == GLFW_KEY_ENTER) {
        result.enter_pressed = true;
        pending_key_action_ = 0;
    }

    draw_text(buffer, x + 4, y + (h - FONT_H * font_scale * 0.5f) * 0.5f, font_scale, 255, 255, 255);
    if (active) {
        float cursor_x = x + 4 + text_width(buffer, font_scale);
        draw_rect(cursor_x, y + 2, 2, h - 4, 200, 220, 255, 255);
    }
    return result;
}

void UIRenderer::set_key(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    pending_key_ = key;
    pending_key_action_ = action;
    if (key == GLFW_KEY_BACKSPACE && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        pending_chars_.push_back('\b');
    }
}

void UIRenderer::set_char(unsigned int ch) {
    pending_chars_.push_back(ch);
}

} // namespace mc