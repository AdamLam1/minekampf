#version 460 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_size;

uniform mat4 u_view_proj;

out vec3 v_uv;
out vec4 v_color;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
