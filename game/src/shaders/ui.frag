#version 460 core
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_atlas;
// 0 = solid color quad, 1 = bitmap font mask (tex.r = coverage),
// 2 = RGBA sprite (icons, HUD hearts) — tex multiplied by vertex color.
uniform int u_textured;

out vec4 frag;

void main() {
    vec4 tex = texture(u_atlas, v_uv);
    if (u_textured == 2) {
        frag = tex * v_color;
    } else if (u_textured == 1 && v_uv.x >= 0.0 && v_uv.y >= 0.0) {
        // Text: alpha is font weight; tint by color
        frag = vec4(v_color.rgb, tex.r * v_color.a);
    } else {
        // Solid color quad (negative UV signals non-textured)
        frag = v_color;
    }
}
