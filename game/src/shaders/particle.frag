#version 460 core
in vec3 v_uv;
in vec4 v_color;

uniform sampler2DArray u_atlas;

out vec4 frag;

void main() {
    vec4 tex = texture(u_atlas, v_uv);
    if (tex.a < 0.1) discard;
    vec3 col = tex.rgb * v_color.rgb;
    
    frag = vec4(col, tex.a * v_color.a);
}
