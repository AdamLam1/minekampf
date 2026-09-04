#version 460 core
in vec2 v_uv;
in float v_layer;
in vec4 v_color;
in float v_dist;

uniform sampler2DArray u_tex;
// Distance fog matching the post pass so far mobs sink into the horizon.
uniform vec3 u_fog_color;
uniform float u_fog_near;
uniform float u_fog_far;

out vec4 frag;

void main() {
    vec4 col;
    if (v_layer < 0.0) {
        col = v_color; // procedural fallback: untextured colored boxes
    } else {
        col = texture(u_tex, vec3(v_uv, v_layer)) * v_color;
        if (col.a < 0.35) discard; // cutout pixels (e.g. painted transparency)
    }
    float fog = clamp((v_dist - u_fog_near) / max(u_fog_far - u_fog_near, 0.001), 0.0, 1.0);
    col.rgb = mix(col.rgb, u_fog_color, fog * 0.85);
    frag = col;
}
