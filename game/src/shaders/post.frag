#version 460 core
in vec2 v_uv;

uniform sampler2D u_color_tex;
uniform sampler2D u_depth_tex;
uniform sampler2DShadow u_shadow_map;

uniform vec3 u_sun_dir;
uniform mat4 u_view_proj;
uniform mat4 u_inv_view_proj;
uniform vec3 u_camera_pos;
uniform mat4 u_light_space_matrix;
uniform float u_time;
uniform float u_is_underwater;
uniform float u_shadows_on;
uniform float u_sky_brightness;

out vec4 frag;

// ACES tone mapping curve
vec3 aces_tonemap(vec3 color) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

float ShadowCalculation(vec3 world_pos) {
    vec4 fragPosLightSpace = u_light_space_matrix * vec4(world_pos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0; // Outside shadow map is lit

    float currentDepth = projCoords.z;
    float bias = 0.001; 
    
    float shadow = texture(u_shadow_map, vec3(projCoords.xy, currentDepth - bias));
    return shadow;
}

vec3 get_world_pos(vec2 uv, float depth) {
    // OpenGL depth range is [0, 1] unless changed, but clip space Z is usually [-1, 1]
    vec4 clip_space_pos = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view_space_pos = u_inv_view_proj * clip_space_pos;
    return view_space_pos.xyz / view_space_pos.w;
}

void main() {
    float depth = texture(u_depth_tex, v_uv).r;
    
    vec2 uv = v_uv;
    if (u_is_underwater > 0.5) {
        // Underwater screen distortion
        uv.x += sin(uv.y * 10.0 + u_time * 3.0) * 0.005;
        uv.y += cos(uv.x * 10.0 + u_time * 3.0) * 0.005;
    }

    vec3 col = texture(u_color_tex, uv).rgb;
    if (u_is_underwater > 0.5) {
        vec2 center_dist = uv - 0.5;
        float ca_strength = dot(center_dist, center_dist) * 0.02;
        col.r = texture(u_color_tex, clamp(uv - center_dist * ca_strength, 0.0, 1.0)).r;
        col.b = texture(u_color_tex, clamp(uv + center_dist * ca_strength, 0.0, 1.0)).b;
    }
    
    // Calculate world position
    vec3 world_pos = get_world_pos(uv, depth);
    vec3 view_dir = world_pos - u_camera_pos;
    float dist = length(view_dir);
    float max_dist = dist;
    
    // If it's the sky, cap the distance
    if (depth >= 0.9999) {
        max_dist = 64.0;
    } else {
        max_dist = min(max_dist, 64.0); // Don't raymarch too far for performance
    }
    
    view_dir = normalize(view_dir);
    
    // Screen Space Ambient Occlusion (SSAO)
    float ssao = 1.0;
    if (depth < 0.9999) {
        vec3 normal = normalize(cross(dFdx(world_pos), dFdy(world_pos)));
        // Generate TBN
        vec3 noise_vec = vec3(fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453),
                              fract(sin(dot(uv, vec2(39.346, 11.135))) * 43758.5453), 0.0) * 2.0 - 1.0;
        vec3 tangent = normalize(noise_vec - normal * dot(noise_vec, normal));
        vec3 bitangent = cross(normal, tangent);
        mat3 tbn = mat3(tangent, bitangent, normal);

        float occlusion = 0.0;
        const int SSAO_SAMPLES = 8;
        vec3 samples[8] = vec3[](
            normalize(vec3( 0.5,  0.5,  0.5)), normalize(vec3(-0.5,  0.5,  0.5)),
            normalize(vec3( 0.5, -0.5,  0.5)), normalize(vec3(-0.5, -0.5,  0.5)),
            normalize(vec3( 0.2,  0.8,  0.2)), normalize(vec3(-0.8,  0.2,  0.2)),
            normalize(vec3( 0.0,  0.0,  0.8)), normalize(vec3( 0.8,  0.0,  0.2))
        );
        
        float radius = 0.6;
        float bias = 0.05;

        for (int i = 0; i < SSAO_SAMPLES; ++i) {
            vec3 sample_pos = world_pos + tbn * samples[i] * radius * (float(i+1)/float(SSAO_SAMPLES));
            
            vec4 offset = u_view_proj * vec4(sample_pos, 1.0);
            offset.xyz /= offset.w;
            offset.xyz = offset.xyz * 0.5 + 0.5;
            
            if (offset.x > 0.0 && offset.x < 1.0 && offset.y > 0.0 && offset.y < 1.0) {
                float sample_depth = texture(u_depth_tex, offset.xy).r;
                if (sample_depth < 0.9999) {
                    vec3 sample_world = get_world_pos(offset.xy, sample_depth);
                    float range_check = smoothstep(0.0, 1.0, radius / (length(world_pos - sample_world) + 0.001));
                    if (length(sample_world - u_camera_pos) < length(sample_pos - u_camera_pos) - bias) {
                        occlusion += 1.0 * range_check;
                    }
                }
            }
        }
        ssao = 1.0 - (occlusion / float(SSAO_SAMPLES));
        // Soften SSAO — full-strength screen-space AO bands badly on flat voxel faces.
        col *= mix(1.0, ssao, 0.55);
    }

    // Volumetric Raymarching (God Rays) — only when looking toward the sun,
    // otherwise the raymarch noise shows up as diagonal banding everywhere.
    // Sun-height gate: at night the below-horizon sun turns this into a warm
    // blob with no visible source (the "mysterious beach glow").
    vec3 god_ray_col = vec3(0.0);
    float scattering = max(0.0, dot(view_dir, u_sun_dir));
    if (scattering > 0.2 && u_sun_dir.y > 0.05 && depth < 0.9999 && u_shadows_on > 0.5) {
        const int NUM_STEPS = 16;
        float step_size = max_dist / float(NUM_STEPS);
        float dither = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
        vec3 start_ray = u_camera_pos + view_dir * (step_size * dither);
        
        vec4 start_light_space = u_light_space_matrix * vec4(start_ray, 1.0);
        vec4 step_light_space = u_light_space_matrix * vec4(view_dir * step_size, 0.0);
        
        vec4 current_light_space = start_light_space;
        float accum_fog = 0.0;
        for(int i = 0; i < NUM_STEPS; i++) {
            vec3 projCoords = current_light_space.xyz / current_light_space.w;
            projCoords = projCoords * 0.5 + 0.5;
            
            float shadow = 1.0;
            if(projCoords.z <= 1.0 && projCoords.x >= 0.0 && projCoords.x <= 1.0 && projCoords.y >= 0.0 && projCoords.y <= 1.0) {
                float bias = 0.001;
                shadow = texture(u_shadow_map, vec3(projCoords.xy, projCoords.z - bias));
            }
            accum_fog += shadow;
            current_light_space += step_light_space;
        }
        accum_fog /= float(NUM_STEPS);
        
        // Wider phase cone than a pure Mie spike so shafts read over half the
        // sky; intensity peaks at LOW sun (shaderpack look) and fades toward
        // noon, scales with overall daylight so it never pops before sunrise.
        float phase = pow(scattering, 6.0);
        float low_sun = smoothstep(0.02, 0.12, u_sun_dir.y) * (1.0 - smoothstep(0.30, 0.65, u_sun_dir.y));
        god_ray_col = vec3(1.0, 0.88, 0.72) * accum_fog * phase * low_sun * (0.25 + 0.45 * u_sky_brightness);
    }
    col += god_ray_col;
    
    // Bloom (Glowing blocks like lava)
    vec3 bloom = vec3(0.0);
    float weight_sum = 0.0;
    vec2 texel_size = 1.0 / textureSize(u_color_tex, 0);
    // 9-tap blur on bright spots
    for (int x = -2; x <= 2; x += 2) {
        for (int y = -2; y <= 2; y += 2) {
            vec2 offset = vec2(x, y) * texel_size * 2.5;
            vec3 sample_col = texture(u_color_tex, uv + offset).rgb;
            float brightness = dot(sample_col, vec3(0.2126, 0.7152, 0.0722));
            if (brightness > 1.2) { 
                float weight = 1.0 / (1.0 + float(x*x + y*y));
                bloom += sample_col * weight;
                weight_sum += weight;
            }
        }
    }
    if (weight_sum > 0.0) {
        col += (bloom / weight_sum) * 0.5;
    }

    // Apply standard distance fog or underwater fog.
    if (depth < 0.9999) {
        if (u_is_underwater > 0.5) {
            // thick cyan/blue fog
            float fog_factor = smoothstep(2.0, 15.0, dist);
            vec3 fog_color = vec3(0.05, 0.4, 0.6); // deep blue/cyan
            col = mix(col, fog_color, fog_factor);
            // underwater tint
            col *= vec3(0.3, 0.8, 0.9);
        } else {
            float fog_factor = smoothstep(75.0, 140.0, dist);
            // The warm sun-side tint and the bright fog color are DAYLIGHT
            // features — at night fog must fall to near-black, otherwise the
            // distant sun-facing slopes glow warm with no source.
            float day = smoothstep(-0.12, 0.10, u_sun_dir.y);
            vec3 fog_color = mix(vec3(0.65, 0.80, 0.95), vec3(1.0, 0.7, 0.4),
                                 max(0.0, dot(view_dir, u_sun_dir)));
            fog_color = mix(vec3(0.02, 0.03, 0.05), fog_color, day);
            col = mix(col, fog_color, fog_factor);
        }
    } else if (u_is_underwater > 0.5) {
        // sky should also be fogged if underwater
        col = vec3(0.05, 0.4, 0.6);
    }
    
    // Tone mapping with a gentle saturation lift for a stylized look.
    // NOTE: albedo textures are authored in sRGB and sampled without linear
    // conversion, so NO extra gamma pass here — it would wash everything out.
    col = aces_tonemap(col * 1.12);
    float luminance = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(luminance), col, 1.12);
    frag = vec4(clamp(col, 0.0, 1.0), 1.0);
}
