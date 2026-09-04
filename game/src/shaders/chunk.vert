#version 460 core
// Upgraded Chunk vertex shader with multi-frequency wind harmonics for stylized foliage.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_uv;
layout(location = 2) in uvec4 a_light; // (block_light, sky_light, ao, face) as bytes
layout(location = 3) in vec4 a_color; // (r,g,b,a) normalized 0..1

uniform mat4 u_view_proj;
uniform mat4 u_light_space_matrix;
uniform vec3 u_camera_pos;
uniform float u_time;

out vec3 v_uv;
out vec3 v_color;
out float v_alpha;
out vec3 v_light; // (bl, sl, ao) in 0..1
out float v_dist;
out float v_mat_type;
out vec3 v_normal;
out vec3 v_tangent;
out vec3 v_bitangent;
out vec3 v_world_pos;
out vec4 v_frag_pos_light_space;
out vec4 v_clip; // screen-space anchor for SSR ray marching

void main() {
    v_uv = a_uv;
    v_color = a_color.rgb;
    v_alpha = a_color.a;
    v_light = vec3(float(a_light.x) / 15.0, float(a_light.y) / 15.0, float(a_light.z) / 3.0);
    
    uint face = a_light.w;
    uint face_dir = face;
    if (face >= 30u) { v_mat_type = 3.0; face_dir -= 30u; }
    else if (face >= 20u) { v_mat_type = 2.0; face_dir -= 20u; }
    else if (face >= 10u) { v_mat_type = 1.0; face_dir -= 10u; }
    else { v_mat_type = 0.0; }
    
    vec3 pos = a_pos;
    
    if (v_mat_type == 3.0) { // Foliage / Grass
        float wind1 = sin(u_time * 2.5 + pos.x * 0.8 + pos.z * 0.8) * 0.08;
        float wind2 = cos(u_time * 3.7 + pos.x * 1.5 - pos.z * 1.2) * 0.04;
        float sway = (wind1 + wind2) * (1.0 - a_uv.y);
        pos.x += sway;
        pos.z += sway * 0.7;
    }
    // NOTE: water surfaces stay flat here — per-vertex waves pulled adjacent
    // quads apart and exposed gaps to the pond floor. Waves are faked in the
    // fragment shader (normals + glints) instead.

    v_world_pos = pos;
    gl_Position = u_view_proj * vec4(pos, 1.0);
    v_clip = gl_Position;
    
    vec3 normals[6] = vec3[](
        vec3(0.0, -1.0, 0.0), // Down
        vec3(0.0, 1.0, 0.0),  // Up
        vec3(0.0, 0.0, -1.0), // North
        vec3(0.0, 0.0, 1.0),  // South
        vec3(-1.0, 0.0, 0.0), // West
        vec3(1.0, 0.0, 0.0)   // East
    );

    vec3 tangents[6] = vec3[](
        vec3(1.0, 0.0, 0.0),  // Down
        vec3(1.0, 0.0, 0.0),  // Up
        vec3(-1.0, 0.0, 0.0), // North
        vec3(1.0, 0.0, 0.0),  // South
        vec3(0.0, 0.0, 1.0),  // West
        vec3(0.0, 0.0, -1.0)  // East
    );

    v_normal = (face_dir < 6u) ? normals[face_dir] : vec3(0.0, 1.0, 0.0);
    v_tangent = (face_dir < 6u) ? tangents[face_dir] : vec3(1.0, 0.0, 0.0);
    v_bitangent = cross(v_normal, v_tangent);
    
    v_frag_pos_light_space = u_light_space_matrix * vec4(pos, 1.0);
    v_dist = distance(pos, u_camera_pos);
}
