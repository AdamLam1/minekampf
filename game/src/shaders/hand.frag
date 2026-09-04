#version 460 core
in vec3 v_uv;
in float v_brightness;
in float v_shade;

uniform sampler2DArray u_atlas;

out vec4 frag;

void main() {
    vec4 tex = texture(u_atlas, v_uv);
    if (tex.a < 0.1) discard;
    vec3 col = tex.rgb * v_brightness * v_shade;
    
    frag = vec4(col, tex.a);
}
