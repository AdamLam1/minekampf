#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "renderer/shader.hpp"

namespace mc {

struct UiVertex {
    float x, y;
    float u, v;
    uint8_t r, g, b, a;
};

// Immediate-mode UI renderer. Renders quads (backgrounds, buttons) and text
// using the embedded bitmap font (96-char ASCII, 8x16 px) or, when available,
// a ladder of Silkscreen TTF rasters (see ui.cpp). Text is UTF-8 (ASCII +
// Polish diacritics). All drawing happens in screen-space pixel coordinates
// (origin top-left).
class UIRenderer {
public:
    static constexpr int FONT_W = 8;
    static constexpr int FONT_H = 8;
    static constexpr int FONT_CHARS = 96; // space..DEL
    // Texture tag meaning "solid rect — bind whatever the current run binds".
    static constexpr uint8_t kTagSolid = 255;

    bool init(int screen_w, int screen_h);
    void shutdown();
    void resize(int w, int h) { screen_w_ = w; screen_h_ = h; }

    void begin_frame();
    void end_frame(); // uploads + draws batched vertices

    // -- Primitives --
    void draw_rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void draw_glass_panel(float x, float y, float w, float h);
    void draw_hotbar_bg(float x, float y, float w, float h);
    // `bold` selects the Bold.ttf ladder (menus/titles); falls back to regular.
    void draw_text(std::string_view text, float x, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255, bool bold = false);
    void draw_text_centered(std::string_view text, float cx, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255, bool bold = false);

    // -- Icon atlas path --
    // The game binds its ItemIcons texture each frame; icon quads are batched
    // separately and flushed with that texture after the font pass.
    void set_icon_texture(uint32_t tex) { icon_tex_ = tex; }
    void draw_icon_quad(float x, float y, float w, float h,
                        float u0, float v0, float u1, float v1,
                        uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255);

    // -- Immediate-mode widgets. Returns true on click/press this frame.
    bool button(std::string_view label, float x, float y, float w, float h);
    bool button(std::string_view label, float x, float y, float w, float h, float font_scale);
    // Button with a dark label — for buttons sitting on light panels.
    bool button_dark(std::string_view label, float x, float y, float w, float h, float font_scale);

    // TextInput: returns true if text changed. `buffer` is in/out.
    struct TextInputResult {
        bool changed = false;
        bool enter_pressed = false;
    };
    TextInputResult text_input(std::string& buffer, float x, float y, float w, float h, bool& active, float font_scale = 1.0f);

    // -- Button helpers --
    void draw_button_bg(float x, float y, float w, float h, bool hovered, bool pressed);

    // Text measurement (UTF-8 aware — counts codepoints)
    [[nodiscard]] float text_width(std::string_view text, float scale) const {
        size_t codepoints = 0;
        for (unsigned char b : text) {
            if ((b & 0xC0) != 0x80) ++codepoints; // skip UTF-8 continuation bytes
        }
        return static_cast<float>(codepoints) * FONT_W * scale;
    }

    // Mouse state (set by Game before UI calls)
    void set_mouse(float mx, float my, bool click, bool down) {
        mouse_x_ = mx; mouse_y_ = my; mouse_click_ = click; mouse_down_ = down;
    }
    [[nodiscard]] float mouse_x() const { return mouse_x_; }
    [[nodiscard]] float mouse_y() const { return mouse_y_; }
    [[nodiscard]] bool mouse_down() const { return mouse_down_; }
    [[nodiscard]] bool mouse_inside(float x, float y, float w, float h) const {
        return mouse_x_ >= x && mouse_x_ <= x + w && mouse_y_ >= y && mouse_y_ <= y + h;
    }

    // Keyboard for text inputs
    void set_key(int key, int scancode, int action, int mods);
    void set_char(unsigned int ch);

    [[nodiscard]] int screen_width() const { return screen_w_; }
    [[nodiscard]] int screen_height() const { return screen_h_; }

private:
    void gen_font_texture();
    void push_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t tag = kTagSolid);
    void push_text_char(int codepoint, float x, float y, float scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool bold = false);
    void flush();
    void flush_icons();

    Shader shader_;
    uint32_t font_tex_ = 0;
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    std::vector<UiVertex> verts_;
    // One tag per quad in verts_: kTagSolid (wildcard) or a frame-texture
    // index into frame_tex_ (0 = bitmap font, then one entry per TTF raster
    // used this frame). flush() coalesces runs with equal tags in order.
    std::vector<uint8_t> quad_tag_;
    std::vector<uint32_t> frame_tex_;
    std::vector<UiVertex> icon_verts_;
    uint32_t icon_tex_ = 0;
    int screen_w_ = 1280;
    int screen_h_ = 720;

    float mouse_x_ = 0, mouse_y_ = 0;
    bool mouse_click_ = false;
    bool mouse_down_ = false;

    // Pending char input (consumed by text_input)
    std::vector<unsigned int> pending_chars_;
    int pending_key_ = 0;
    int pending_key_action_ = 0;
};

} // namespace mc