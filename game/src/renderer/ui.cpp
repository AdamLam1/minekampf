#include "renderer/ui.hpp"
#include "renderer/ui_theme.hpp"

#include <algorithm>
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
// TTF text engine (Silkscreen, OFL) — metrics-compatible drop-in for font8x8:
// the per-character advance stays 8 * scale so all existing layout math is
// unchanged. Falls back to the embedded bitmap font when the TTF is missing.
// ---------------------------------------------------------------------------
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

namespace {
constexpr int kTtfCols = 16;   // atlas cells per row
constexpr int kTtfCell = 12;   // cell size in px (baseline at row 8)
constexpr float kTtfPixelH = 10.0f;
bool g_ttf_ok = false;
uint32_t g_ttf_tex = 0;
int g_ttf_glyph_count = 0;
std::vector<float> g_ttf_uv; // per glyph: u0, v0, u1, v1

// Atlas cell index for a codepoint (matches push_text_char's font8x8 mapping).
int ttf_cell_for_cp(int cp) {
    if (cp >= 32 && cp < 128) return cp - 32;
    extern int ttf_polish_slot(int); // defined below (ui.cpp scope)
    return ttf_polish_slot(cp);
}

bool load_ttf_font() {
    FILE* f = std::fopen("assets/fonts/Silkscreen-Regular.ttf", "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> buf(size);
    size_t rd = std::fread(buf.data(), 1, size, f);
    std::fclose(f);
    if (rd != static_cast<size_t>(size) || size < 1) return false;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, buf.data(), 0)) return false;

    // Codepoints: ASCII 32..127 then the Polish set (same order as kPolish).
    std::vector<int> cps;
    for (int c = 32; c < 128; ++c) cps.push_back(c);
    for (const auto& g : kPolish) cps.push_back(static_cast<int>(g.cp));
    g_ttf_glyph_count = static_cast<int>(cps.size());

    const int rows = (g_ttf_glyph_count + kTtfCols - 1) / kTtfCols;
    std::vector<uint8_t> atlas(kTtfCols * kTtfCell * rows * kTtfCell, 0);
    g_ttf_uv.assign(static_cast<size_t>(g_ttf_glyph_count) * 4, 0.0f);

    float scale = stbtt_ScaleForPixelHeight(&font, kTtfPixelH);
    int ascent = 0, descent = 0, line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    int baseline = 8; // cell row that acts as the text baseline

    for (int gi = 0; gi < g_ttf_glyph_count; ++gi) {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmap(&font, 0, scale, cps[gi], &w, &h,
                                                      &xoff, &yoff);
        int col = gi % kTtfCols, row = gi / kTtfCols;
        int ox = col * kTtfCell, oy = row * kTtfCell;
        if (bmp) {
            // Place so the glyph's baseline lands on the cell baseline row.
            for (int y = 0; y < h; ++y) {
                int dy = baseline + yoff + y;
                if (dy < 0 || dy >= kTtfCell) continue;
                for (int x = 0; x < w; ++x) {
                    int dx = 2 + xoff + x;
                    if (dx < 0 || dx >= kTtfCell) continue;
                    atlas[(oy + dy) * kTtfCols * kTtfCell + ox + dx] = bmp[y * w + x];
                }
            }
            stbtt_FreeBitmap(bmp, nullptr);
        }
        float tw = static_cast<float>(kTtfCols * kTtfCell);
        float th = static_cast<float>(rows * kTtfCell);
        g_ttf_uv[gi * 4 + 0] = static_cast<float>(ox) / tw;
        g_ttf_uv[gi * 4 + 1] = static_cast<float>(oy) / th;
        g_ttf_uv[gi * 4 + 2] = static_cast<float>(ox + kTtfCell) / tw;
        g_ttf_uv[gi * 4 + 3] = static_cast<float>(oy + kTtfCell) / th;
    }

    glGenTextures(1, &g_ttf_tex);
    glBindTexture(GL_TEXTURE_2D, g_ttf_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kTtfCols * kTtfCell, rows * kTtfCell, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

int ttf_polish_slot(int cp) {
    for (int i = 0; i < kPolishCount; ++i) {
        if (kPolish[i].cp == static_cast<unsigned>(cp)) return 96 + i;
    }
    return -1;
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
        MC_LOG_INFO("UI font: Silkscreen TTF loaded ({} glyphs)", g_ttf_glyph_count);
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
    if (g_ttf_tex) glDeleteTextures(1, &g_ttf_tex);
    g_ttf_tex = 0;
    g_ttf_ok = false;
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    font_tex_ = vao_ = vbo_ = 0;
}

void UIRenderer::begin_frame() {
    verts_.clear();
    icon_verts_.clear();
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
    glBindTexture(GL_TEXTURE_2D, g_ttf_ok ? g_ttf_tex : font_tex_);
    shader_.set_int("u_atlas", 0);
    shader_.set_int("u_textured", 1); // font mask pass

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts_.size() * sizeof(UiVertex), verts_.data(), GL_STREAM_DRAW);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    verts_.clear();
}

void UIRenderer::push_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
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

void UIRenderer::push_text_char(int cp, float x, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (g_ttf_ok) {
        // TTF path: cell index matches the bitmap font's (ASCII then Polish).
        int idx;
        if (cp >= 128) {
            for (int i = 0; i < kPolishCount; ++i) {
                if (kPolish[i].cp == static_cast<unsigned>(cp)) {
                    idx = 96 + i;
                    goto have_idx;
                }
            }
            return;
        } else {
            idx = cp - 32;
            if (idx < 0 || idx >= FONT_CHARS) return;
        }
    have_idx:
        if (idx >= g_ttf_glyph_count) return;
        // Cell is 12px drawn at 0.8 * scale so the 8px advance grid holds.
        float k = 0.8f * scale;
        const float* uv = &g_ttf_uv[static_cast<size_t>(idx) * 4];
        push_quad(x, y, kTtfCell * k, kTtfCell * k, uv[0], uv[1], uv[2], uv[3], r, g, b, a);
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

    push_quad(x, y, 8.0f * scale, 8.0f * scale, u0, v0, u1, v1, r, g, b, a);
}

void UIRenderer::draw_text(std::string_view text, float x, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float cx = x;
    float shadow = std::max(1.0f, scale);
    uint8_t sa = static_cast<uint8_t>(a * ui::kTextShadowAlpha / 255);
    for (size_t i = 0; i < text.size(); ) {
        unsigned char b0 = static_cast<unsigned char>(text[i]);
        if (b0 < 0x80) {
            push_text_char(b0, cx + shadow, y + shadow, scale, 0, 0, 0, sa);
            push_text_char(b0, cx, y, scale, r, g, b, a);
            cx += FONT_W * scale;
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
            unsigned cp = (static_cast<unsigned>(b0 & 0x1F) << 6) |
                          (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            push_text_char(static_cast<int>(cp), cx + shadow, y + shadow, scale, 0, 0, 0, sa);
            push_text_char(static_cast<int>(cp), cx, y, scale, r, g, b, a);
            cx += FONT_W * scale;
            i += 2;
        } else {
            i += 1; // unsupported sequence — skip
        }
    }
}

void UIRenderer::draw_text_centered(std::string_view text, float cx, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float w = text_width(text, scale);
    draw_text(text, cx - w * 0.5f, y, scale, r, g, b, a);
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
                if (!buffer.empty()) buffer.pop_back();
                result.changed = true;
            } else if (ch >= 32 && ch < 127 && buffer.size() < 64) {
                buffer.push_back(static_cast<char>(ch));
                result.changed = true;
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