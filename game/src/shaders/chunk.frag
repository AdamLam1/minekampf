#version 460 core
// Stylized Fantasy Chunk Fragment Shader with Foliage SSS, Rim Lighting & Caustic Water.

in vec3 v_uv;
in vec3 v_color;
in float v_alpha;
in vec3 v_light;
in float v_dist;
in float v_mat_type;
in vec3 v_normal;
in vec3 v_tangent;
in vec3 v_bitangent;
in vec3 v_world_pos;

// Drifting cloud-shadow noise (2D fbm, matches the sky cumulus drift).
float cloud_hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float cloud_noise(vec2 x) {
    vec2 p = floor(x);
    vec2 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(cloud_hash(p), cloud_hash(p + vec2(1.0, 0.0)), f.x),
               mix(cloud_hash(p + vec2(0.0, 1.0)), cloud_hash(p + vec2(1.0, 1.0)), f.x), f.y);
}
float cloud_fbm(vec2 x) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; ++i) {
        v += a * cloud_noise(x);
        x = x * 2.13 + vec2(37.0);
        a *= 0.5;
    }
    return v;
}
in vec4 v_frag_pos_light_space;
in vec4 v_clip;

uniform sampler2DArray u_albedo_map;
uniform sampler2DArray u_normal_map;
uniform sampler2DArray u_specular_map;
uniform sampler2DArray u_height_map;

uniform sampler2DShadow u_shadow_map;
uniform sampler2D u_opaque_color;
uniform sampler2D u_opaque_depth;

uniform vec2 u_resolution;
uniform float u_sky_brightness;
uniform float u_time;
uniform float u_shadows_on;
uniform mat4 u_view_proj;
uniform vec2 u_near_far;
uniform float u_pom_dist;    // quality preset: POM active distance (0 = off)
uniform float u_ssr;         // quality preset: water SSR on/off
uniform float u_shadow_soft;
uniform float u_shadow_texel; // quality preset: soft-PCF taps on/off

uniform vec3 u_camera_pos;
uniform vec3 u_sun_dir;
uniform vec3 u_sky_color;
uniform vec3 u_fog_color;
uniform float u_fog_start;
uniform float u_fog_end;
uniform float u_fog_density;
uniform int u_fog_mode;

out vec4 frag;

// Steep Parallax Occlusion Mapping (subtle — stylized look, artifact-free)
vec2 parallax_mapping(vec3 tex_uv, vec3 view_dir_ts) {
    const float min_layers = 8.0;
    const float max_layers = 32.0;
    float num_layers = mix(max_layers, min_layers, max(dot(vec3(0.0, 0.0, 1.0), view_dir_ts), 0.0));
    
    float layer_depth = 1.0 / num_layers;
    float current_layer_depth = 0.0;
    
    vec2 p = view_dir_ts.xy * 0.02; 
    vec2 delta_uv = p / num_layers;
    
    vec2 current_uv = tex_uv.xy;
    float current_depth_value = 1.0 - texture(u_height_map, vec3(current_uv, tex_uv.z)).r;
    
    for (int i = 0; i < 32; ++i) {
        if (current_layer_depth >= current_depth_value) break;
        current_uv -= delta_uv;
        current_depth_value = 1.0 - texture(u_height_map, vec3(current_uv, tex_uv.z)).r;
        current_layer_depth += layer_depth;
    }

    // NO clamp to 0..1: greedy-meshed quads carry tiled UVs (0..w / 0..h) and
    // clamping here crushed a 12x12 merged beach quad into a fraction of one
    // tile — stretched smears across every flat surface. The array layers use
    // GL_REPEAT, so UVs beyond 1.0 keep tiling correctly.
    return current_uv;
}

// Cook-Torrance BRDF Specular
float distribution_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265 * denom * denom;
    return num / max(denom, 0.0001);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometry_schlick_ggx(NdotV, roughness);
    float ggx1 = geometry_schlick_ggx(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnel_schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Linear view-space depth from the hardware depth value.
float linear_depth(float d) {
    float z = d * 2.0 - 1.0;
    float n = u_near_far.x;
    float f = u_near_far.y;
    return 2.0 * n * f / (f + n - z * (f - n));
}

// Screen-space reflection: march the reflected ray through the opaque-only
// color/depth buffers; where it lands just behind seen geometry, that geometry
// is what the water should mirror. Falls back to the analytic sky color.
vec3 ssr_reflection(vec3 start, vec3 refl_dir, vec3 sky_fallback) {
    vec3 p = start + refl_dir * 0.25;
    float step_len = 0.30;
    for (int i = 0; i < 24; ++i) {
        p += refl_dir * step_len;
        // The opaque buffer contains the sea floor — reflecting it paints dirt
        // blotches on the surface. Water only mirrors what is above its plane.
        if (p.y < start.y - 0.05) return sky_fallback;
        vec4 c = u_view_proj * vec4(p, 1.0);
        if (c.w <= 0.0) return sky_fallback;
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (uv.x < 0.005 || uv.x > 0.995 || uv.y < 0.005 || uv.y > 0.995)
            return sky_fallback;
        float scene_d = texture(u_opaque_depth, uv).r;
        if (scene_d >= 0.99995) { step_len *= 1.30; continue; } // sky pixel
        float p_lin = linear_depth(c.z / c.w * 0.5 + 0.5);
        float scene_lin = linear_depth(scene_d);
        if (p_lin < scene_lin) {
            // Behind the surface — accept only if we're close to it (a real
            // reflection contact), not several blocks behind a wall.
            if (scene_lin - p_lin < 1.5) {
                vec3 hit = texture(u_opaque_color, uv).rgb;
                // Fade near screen edges so reflections dissolve, not pop.
                vec2 suv = gl_FragCoord.xy / u_resolution;
                vec2 edge = smoothstep(vec2(0.0), vec2(0.15), suv)
                          * (1.0 - smoothstep(vec2(0.85), vec2(1.0), suv));
                return mix(sky_fallback, hit, edge.x * edge.y);
            }
            return sky_fallback;
        }
        step_len *= 1.30;
    }
    return sky_fallback;
}

void main() {
    mat3 TBN = mat3(normalize(v_tangent), normalize(v_bitangent), normalize(v_normal));
    vec3 view_dir = normalize(u_camera_pos - v_world_pos);
    vec3 view_dir_ts = normalize(transpose(TBN) * view_dir);

    vec3 uv = v_uv;
    // Parallax only up close, facing the surface, and never on foliage/water —
    // at glancing angles it degenerates into streaks.
    bool is_foliage = (v_mat_type == 3.0) || (v_mat_type == 4.0) ||
                      (abs(uv.z - 9.0) < 0.5);
    if (v_dist < u_pom_dist && v_mat_type != 1.0 && !is_foliage &&
        dot(normalize(v_normal), view_dir) > 0.35) {
        uv.xy = parallax_mapping(uv, view_dir_ts);
    }

    vec4 tex = texture(u_albedo_map, uv);
    if (tex.a < 0.1) discard;

    // Stylized albedo grade: a modest saturation/contrast lift on the raw
    // texel (before lighting) keeps blocks from reading as pale chalk under
    // the bright day sky, the way vanilla Minecraft's albedo does.
    {
        float al = dot(tex.rgb, vec3(0.2126, 0.7152, 0.0722));
        tex.rgb = clamp(mix(vec3(al), tex.rgb, 1.22), 0.0, 1.0);
        tex.rgb = clamp((tex.rgb - 0.5) * 1.07 + 0.5, 0.0, 1.0);
    }

    // Terrain normal maps are a subtle micro-detail, not a rerender of the
    // sun: full strength made flat faces dappled/noisy. Water keeps its own
    // animated wave normals below.
    vec3 normal_ts = texture(u_normal_map, uv).rgb * 2.0 - 1.0;
    if (v_mat_type != 1.0) normal_ts *= 0.55;
    vec3 N = normalize(TBN * normal_ts);
    
    vec4 spec_data = texture(u_specular_map, uv);
    float roughness = spec_data.r;
    float metallic = spec_data.g;
    float emissive = spec_data.a;

    // Shadow Mapping — 5-tap rotated disk (each hardware tap is already a
    // 2x2 PCF, so edges read as a soft dithered penumbra instead of stair steps).
    float shadow = 0.0;
    if (u_shadows_on > 0.5) {
        vec3 proj_coords = v_frag_pos_light_space.xyz / v_frag_pos_light_space.w;
        proj_coords = proj_coords * 0.5 + 0.5;
        if (proj_coords.z <= 1.0) {
            float current_depth = proj_coords.z;
            float bias = max(0.003 * (1.0 - dot(v_normal, normalize(u_sun_dir))), 0.001);
            vec3 sm_coord = vec3(proj_coords.xy, current_depth - bias);
            float texel = u_shadow_texel;
            float rot = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
            vec2 o1 = vec2(cos(rot), sin(rot)) * texel * 1.6;
            vec2 o2 = vec2(cos(rot + 2.094), sin(rot + 2.094)) * texel * 1.6;
            float s = texture(u_shadow_map, sm_coord).r;
            if (u_shadow_soft > 0.5) {
                // 4 extra rotated-disk taps — each is already a 2x2 hardware
                // PCF, giving a soft dithered penumbra (quality: medium/high).
                vec2 o1 = vec2(cos(rot), sin(rot)) * texel * 1.6;
                vec2 o2 = vec2(cos(rot + 2.094), sin(rot + 2.094)) * texel * 1.6;
                s += texture(u_shadow_map, vec3(proj_coords.xy + o1, current_depth - bias)).r;
                s += texture(u_shadow_map, vec3(proj_coords.xy - o1, current_depth - bias)).r;
                s += texture(u_shadow_map, vec3(proj_coords.xy + o2, current_depth - bias)).r;
                s += texture(u_shadow_map, vec3(proj_coords.xy - o2, current_depth - bias)).r;
                s /= 5.0;
            }
            shadow = 1.0 - s;
        }
    }

    vec3 L = normalize(u_sun_dir);
    vec3 V = view_dir;
    vec3 H = normalize(V + L);

    // Direct sunlight color: warm gold near the horizon, near-white at noon.
    vec3 sun_tint = mix(vec3(1.0, 0.60, 0.30), vec3(1.0, 0.98, 0.92),
                        clamp(u_sun_dir.y * 2.2, 0.0, 1.0));

    float block_light = (v_mat_type == 2.0) ? 1.0 : v_light.x;
    float sky_light = (v_mat_type == 2.0) ? 1.0 : v_light.y * u_sky_brightness;
    float ao = mix(0.4, 1.0, v_light.z);

    float ndotl = max(dot(N, L), 0.0);
    float sun_exposure = smoothstep(0.7, 1.0, v_light.y) * u_sky_brightness;
    // Sky brightness alone is an AMBIENT proxy — it is ~0.4 at pre-dawn while
    // the sun is still below the horizon. Without the height gate the direct
    // sun (warm specular + boosted diffuse) leaks onto east-facing slopes as a
    // glowing patch long before sunrise.
    float sun_up = smoothstep(0.0, 0.08, u_sun_dir.y);
    float final_sun_exposure = sun_exposure * (1.0 - shadow) * sun_up;

    // Minecraft-style fixed face shading: the voxel read comes from every
    // cube side having its own brightness step (top 1.0, N/S 0.82, E/W 0.65,
    // bottom 0.55), modulated a little by which side faces the sun. The
    // normal-mapped ndotl alone flattened everything into one chalky value.
    vec3 gnorm = normalize(v_normal);
    float face_shade = (gnorm.y > 0.5) ? 1.0
                     : (gnorm.y < -0.5) ? 0.58
                     : (abs(gnorm.x) > 0.5) ? 0.72 : 0.84;
    face_shade *= mix(0.92, 1.08, ndotl);

    // Specular is a METAL/WATER feature only (shaderpack matte terrain):
    // dielectric GGX sheen from F0=0.04 is exactly what made every block read
    // as glass. Non-metals render pure diffuse like vanilla + BSL terrain.
    float dir_light = mix(0.85, 1.18, final_sun_exposure)
                    * clamp(u_sky_brightness + 0.15, 0.0, 1.0)
                    * face_shade;
    // Cloud shadows: drifting fbm darkens the DIRECT sun component only (the
    // ambient stays), so terrain keeps depth while nothing goes pitch-black.
    // Same drift direction/speed family as the sky cumulus layer.
    {
        vec2 cs_uv = v_world_pos.xz * 0.0042 + vec2(u_time * 0.030, u_time * 0.011);
        float cs = cloud_fbm(cs_uv);
        float cloud_shadow = smoothstep(0.52, 0.72, cs) * final_sun_exposure;
        dir_light *= mix(1.0, 0.55, cloud_shadow);
    }
    // Foliage crosses have no meaningful face normal — light them like the
    // terrain around them instead of a downward-facing cube side.
    if (is_foliage) dir_light = 1.0;

    // Colored lighting: torches are warm and FLICKER (three incommensurate
    // sines, ±~4% — subtle flame breathing; a stronger global pulse made
    // whole rooms visibly throb), skylight is cool-bright and picks up the
    // sun's warm grade near the horizon (BSL golden-hour wash).
    float flicker = 1.0
                  + 0.022 * sin(u_time * 7.0)
                  + 0.014 * sin(u_time * 12.3 + 1.7)
                  + 0.008 * sin(u_time * 21.7 + 4.1);
    // Warm AMBER (not cream): deeper green/blue suppression keeps the fire
    // hue visible after tonemap; the old (1.0, 0.80, 0.58) desaturated into
    // a milky wash once combined with bright sandstone albedo.
    vec3 torch_col = vec3(1.00, 0.72, 0.45) * flicker;
    // Torch falloff: vanilla Minecraft multiplies brightness by ~0.8 per
    // light level (exponential), which reads natural — the old pow(bl, 1.45)
    // kept everything near maximum for 10+ blocks and washed the walls into
    // a flat cream blob. Steeper curve = visible gradient + visible texture.
    float bl_curved = pow(0.82, (1.0 - block_light) * 15.0);
    // Warm grade folds in sun PRESENCE: with the sun below the horizon there
    // is no warm direct light — otherwise the whole sky-lit world glows amber
    // at night (the mysterious "beach pool").
    float sun_presence = clamp(u_sun_dir.y * 6.0 + 0.15, 0.0, 1.0);
    vec3 sun_grade = mix(vec3(1.0),
                         mix(vec3(1.00, 0.70, 0.48), vec3(1.0), clamp(u_sun_dir.y * 2.4, 0.0, 1.0)),
                         sun_presence);
    vec3 day_col = vec3(0.86, 0.93, 1.04) * sun_grade;
    // Cast shadows tint the lit component toward cool violet (skylight-only
    // look, Hytale-style) — the violet ambient floor alone reads too flat.
    day_col = mix(day_col, day_col * vec3(0.80, 0.78, 1.12), shadow * sun_up);
    vec3 light_col = max(torch_col * bl_curved, day_col * (sky_light * dir_light));
    // Stylized ambient floor: soft in daylight so shadows stay readable;
    // at night the floor is dim MOONLIGHT BLUE instead of flat grey.
    vec3 night_amb = vec3(0.030, 0.042, 0.075);
    // Stylized shadow tint (Hytale-style): shadows carry a cool violet cast
    // instead of neutral black, so shadowed albedo stays readable.
    float amb_level = mix(0.04, 0.22, clamp(u_sky_brightness, 0.0, 1.0));
    vec3 ambient = mix(night_amb, vec3(0.212, 0.200, 0.252) * (amb_level / 0.22),
                       clamp(u_sky_brightness + 0.04, 0.0, 1.0));
    // Smooth-lighting AO with a floor: raw ao/3 crushed indoor corners to a
    // third of their light even where a torch was flooding the room — but a
    // too-high floor erases corner occlusion entirely.
    float ao_soft = mix(0.38, 1.0, ao);
    light_col = max(light_col * ao_soft, ambient);
    if (is_foliage) light_col = max(light_col, vec3(0.55 * clamp(u_sky_brightness + 0.2, 0.0, 1.0)));

    vec3 col = tex.rgb * v_color * light_col;

    // Sun glints survive only on authored metals (crude ore flecks, tools).
    if (metallic > 0.5 && !is_foliage) {
        float rough_mat = max(roughness, 0.15);
        vec3 F0 = mix(vec3(0.04), tex.rgb, metallic);
        float NDF = distribution_ggx(N, H, rough_mat);
        float G = geometry_smith(N, V, L, rough_mat);
        vec3 F = fresnel_schlick(max(dot(H, V), 0.0), F0);
        float denom = 4.0 * max(dot(N, V), 0.0) * ndotl + 0.0001;
        vec3 specular_brdf = min(NDF * G * F / denom, vec3(0.35));
        col += specular_brdf * sun_tint * (final_sun_exposure * ndotl);
    }

    // 1. Foliage Subsurface Scattering (SSS Translucency Glow when backlit)
    if (is_foliage) {
        float backlighting = max(dot(-V, L), 0.0) * (1.0 - max(dot(N, L), 0.0));
        float sss_factor = pow(backlighting, 2.0) * final_sun_exposure;
        vec3 sss_color = tex.rgb * vec3(0.5, 1.2, 0.3) * sss_factor * 0.25;
        col += sss_color;
    }

    // 2. Emissive glow (Lava, Glowstone, Torches)
    if (v_mat_type == 2.0 || emissive > 0.01) {
        col += tex.rgb * (emissive > 0.01 ? emissive * 3.5 : 4.5);
    }

    // 3. Shaderpack-style water: animated wave normals, Fresnel sky reflection,
    //    narrow sparkling sun glints. The old broad pow-60 lobe + flat tint
    //    read as a sheet of glass.
    float out_alpha = tex.a * v_alpha;
    if (v_mat_type == 1.0) { // Water
        vec2 wave_uv1 = v_world_pos.xz * 0.35 + vec2(u_time * 0.05, u_time * 0.03);
        vec2 wave_uv2 = v_world_pos.xz * 0.90 - vec2(u_time * 0.04, u_time * 0.07);
        vec3 wave_n1 = texture(u_normal_map, vec3(wave_uv1, 9.0)).rgb * 2.0 - 1.0;
        vec3 wave_n2 = texture(u_normal_map, vec3(wave_uv2, 9.0)).rgb * 2.0 - 1.0;
        vec3 water_N = normalize(v_normal + wave_n1 * 0.55 + wave_n2 * 0.35);

        // Schlick Fresnel with water's F0: straight down = nearly no
        // reflection, grazing angles = mirror.
        float cos_up = max(dot(water_N, V), 0.0);
        float fres = mix(0.04, 1.0, pow(1.0 - cos_up, 5.0));

        vec3 refl_dir = reflect(-V, water_N);
        // The dusk wash tints everything the water reflects, not just the sky
        // near the sun — otherwise low-sun water stays cold pewter.
        vec3 refl_col = mix(u_fog_color, u_sky_color, pow(max(refl_dir.y, 0.0), 0.55)) * sun_grade;

        // Screen-space reflections: mirror the actual scene (trees, beaches,
        // cliffs) where the ray finds it; open sky falls back to the analytic
        // gradient above. Quality preset low = analytic sky only.
        if (u_ssr > 0.5) {
            refl_col = ssr_reflection(v_world_pos, refl_dir, refl_col);
        }

        // Gold sun path toward a low sun — the classic shaderpack sunset.
        vec3 flat_L = normalize(vec3(L.x, 0.0, L.z));
        vec3 flat_R = normalize(vec3(refl_dir.x, 0.0, refl_dir.z));
        float low_sun = 1.0 - clamp(u_sun_dir.y * 3.0, 0.0, 1.0);
        refl_col = mix(refl_col, vec3(1.0, 0.55, 0.25),
                       low_sun * pow(max(dot(flat_R, flat_L), 0.0), 3.0) * 0.7);

        vec3 water_deep = vec3(0.035, 0.15, 0.42);
        vec3 water_shallow = vec3(0.07, 0.27, 0.50);
        vec3 body = mix(water_shallow, water_deep, clamp((1.0 - cos_up) * 1.6, 0.0, 1.0));
        body *= 1.0 + (wave_n1.z + wave_n2.z) * 0.06;

        // Capped lower than a mirror: even at grazing angles a hint of the
        // blue body keeps water from reading as liquid glass.
        col = mix(body, refl_col, clamp(fres, 0.0, 0.80)) * max(light_col, vec3(0.35));

        // Glints: a narrow pow-420 sparkle that dances on the wave normals,
        // plus a broad warm lobe for the far shoreline. Gated by sun height
        // only — the sky-exposure term collapses at low sun and would erase
        // exactly the sunset glints this exists for.
        float rdotl = max(dot(refl_dir, L), 0.0);
        col += sun_tint * (pow(rdotl, 420.0) * 2.4 + pow(rdotl, 48.0) * 0.14)
             * sun_up;

        // Looking down: translucent enough to read the tinted bottom; grazing:
        // nearly solid. Far water stays solid regardless of angle.
        out_alpha = mix(0.74, 0.96, clamp(fres * 1.3, 0.0, 1.0));
        out_alpha = max(out_alpha, smoothstep(40.0, 100.0, v_dist));
    }

    // Distance fog is applied once, in the post-processing pass.
    frag = vec4(col, out_alpha);
}
