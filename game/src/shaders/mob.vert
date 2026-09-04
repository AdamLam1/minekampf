#version 460 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_layer;
layout(location = 3) in float a_light;
layout(location = 4) in vec4 a_color;

uniform mat4 u_vp;
uniform vec3 u_cam_pos;

out vec2 v_uv;
out float v_layer;
out vec4 v_color;
out float v_dist;

void main() {
    gl_Position = u_vp * vec4(a_pos, 1.0);
    v_uv = a_uv;
    v_layer = a_layer;
    v_color = vec4(a_color.rgb * a_light, a_color.a);
    v_dist = length(a_pos - u_cam_pos);
}
