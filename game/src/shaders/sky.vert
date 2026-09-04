#version 460 core
layout(location = 0) in vec3 a_pos;

uniform mat4 u_view_proj_no_translation;

out float v_y;
out vec3 v_dir;

void main() {
    vec4 pos = u_view_proj_no_translation * vec4(a_pos, 1.0);
    gl_Position = pos.xyww; // push to far plane
    
    // Map y from [0, 1] to [0, 1] for gradient (bottom half is solid fog)
    v_y = clamp(a_pos.y, 0.0, 1.0);
    v_dir = a_pos;
}
