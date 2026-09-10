#version 460 core
// Upgraded Chunk vertex shader with multi-frequency wind harmonics for stylized foliage.

// Compact 24-byte packed vertex (see chunk_mesh.hpp PackedVertex).
layout(location = 0) in ivec4 a_pos_i;      // world x, y, z (int16, whole blocks)
layout(location = 1) in uint a_packed_uv;   // u(16) v(16), uv/16*65535
layout(location = 2) in uvec4 a_meta;       // tile, block_light, sky_light, ao
layout(location = 3) in uvec4 a_meta2;      // face, r, g, b
layout(location = 4) in uint a_packed_alpha;

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
    // Unpack the compact vertex (names kept identical to the old attribs).
    const vec3 a_pos = vec3(a_pos_i.x, a_pos_i.y, a_pos_i.z);
    const vec3 a_uv = vec3(float(a_packed_uv & 0xFFFFu) * (16.0 / 65535.0),
                           float((a_packed_uv >> 16u) & 0xFFFFu) * (16.0 / 65535.0),
                           float(a_meta.x));
    const uvec4 a_light = uvec4(a_meta.y, a_meta.z, a_meta.w, a_meta2.x);
    const vec4 a_color = vec4(float(a_meta2.y), float(a_meta2.z), float(a_meta2.w),
                              float(a_packed_alpha)) / 255.0;

    v_uv = a_uv;
    v_color = a_color.rgb;
    v_alpha = a_color.a;
    v_light = vec3(float(a_light.x) / 15.0, float(a_light.y) / 15.0, float(a_light.z) / 3.0);
    
    uint face = a_light.w;
    uint face_dir = face;
    if (face >= 40u) { v_mat_type = 4.0; face_dir -= 40u; } // static cross (torch...)
    else if (face >= 30u) { v_mat_type = 3.0; face_dir -= 30u; }
    else if (face >= 20u) { v_mat_type = 2.0; face_dir -= 20u; }
    else if (face >= 10u) { v_mat_type = 1.0; face_dir -= 10u; }
    else { v_mat_type = 0.0; }
    
    vec3 pos = a_pos;
    
    if (v_mat_type == 3.0) { // Foliage / Grass only — torches (mat 4) stay still
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
