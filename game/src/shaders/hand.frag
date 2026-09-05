#version 460 core
in vec3 v_uv;
in float v_brightness;
in float v_shade;

uniform sampler2DArray u_atlas;
uniform sampler2D u_icon;    // item icon atlas (held tool sprite)
uniform float u_use_icon;    // 1.0 when the hand renders an item sprite

out vec4 frag;

void main() {
    vec4 tex = (u_use_icon > 0.5) ? texture(u_icon, v_uv.xy)
                                  : texture(u_atlas, v_uv);
    if (tex.a < 0.1) discard;
    vec3 col = tex.rgb * v_brightness * v_shade;
    
    frag = vec4(col, tex.a);
}
