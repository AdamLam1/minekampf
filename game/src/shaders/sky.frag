#version 460 core
// Atmospheric Skybox with Volumetric 3D Noise Clouds, Rayleigh/Mie Scattering, and Starfield.

in float v_y;
in vec3 v_dir;

uniform vec3 u_sky_color;
uniform vec3 u_fog_color;
uniform vec3 u_sun_dir;
uniform float u_time;
uniform float u_clouds; // quality preset: cumulus layer on/off

out vec4 frag;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 x) {
    vec2 p = floor(x);
    vec2 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(p + vec2(0.0, 0.0)), hash(p + vec2(1.0, 0.0)), f.x),
               mix(hash(p + vec2(0.0, 1.0)), hash(p + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 x) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(0.87758, 0.47943, -0.47943, 0.87758);
    for (int i = 0; i < 5; ++i) {
        v += a * noise(x);
        x = rot * x * 2.0 + vec2(100.0);
        a *= 0.5;
    }
    return v;
}

void main() {
    vec3 dir = normalize(v_dir);
    vec3 sun = normalize(u_sun_dir);

    // Rayleigh scattering
    float cos_theta = dot(dir, sun);
    float rayleigh_phase = 0.75 * (1.0 + cos_theta * cos_theta);

    // Mie scattering (horizon haze). The raw Henyey-Greenstein-style phase
    // explodes as cos_theta -> 1 and painted a huge white blob over the sun;
    // that vicinity is handled by the dedicated sun disc/glow below instead.
    float g = 0.985;
    float mie_raw = 1.5 * ((1.0 - g*g) / (2.0 + g*g)) * (1.0 + cos_theta*cos_theta) / pow(max(1.0 + g*g - 2.0*g*cos_theta, 1e-4), 1.5);
    float sun_core_mask = smoothstep(0.9990, 0.9999, cos_theta);
    // Cap the phase: raw values reach 1e9 near cos=1 and whited-out a ~20 deg
    // blob around the sun. 1.5 keeps a visible atmospheric haze only.
    float mie_phase = min(mie_raw, 0.5) * (1.0 - sun_core_mask);

    vec3 fog_linear = u_fog_color;
    vec3 sky_linear = u_sky_color;

    float blend = pow(max(dir.y, 0.0), 0.6);
    vec3 col = mix(fog_linear, sky_linear, blend);

    // Rayleigh & Mie scattering glow
    vec3 scatter_color = mix(vec3(1.0, 0.5, 0.2), vec3(1.0, 0.95, 0.85), clamp(sun.y * 3.0, 0.0, 1.0));
    col += scatter_color * rayleigh_phase * 0.08 * max(sun.y, 0.0);
    col += scatter_color * mie_phase * 0.4 * smoothstep(-0.25, 0.25, sun.y);
    float sun_halo2 = smoothstep(0.9985, 0.99985, cos_theta);
    col += vec3(1.1, 1.0, 0.85) * sun_halo2 * 0.8 * smoothstep(-0.1, 0.1, sun.y);

    // Sun Disc (round, with a soft halo)
    float sun_disc = smoothstep(0.9995, 0.9998, cos_theta);
    float sun_halo = smoothstep(0.999, 0.9995, cos_theta);
    col += vec3(4.0, 3.8, 3.0) * sun_disc * smoothstep(-0.1, 0.1, sun.y);
    col += vec3(1.3, 1.2, 1.0) * sun_halo * smoothstep(-0.1, 0.1, sun.y);

    // Night Starfield
    if (sun.y < 0.1 && dir.y > 0.05) {
        vec2 star_uv = dir.xz / (dir.y + 0.1) * 80.0;
        float star_hash = hash(floor(star_uv));
        if (star_hash > 0.982) {
            float star_brightness = pow((star_hash - 0.982) / 0.018, 2.0);
            float shimmer = sin(u_time * 3.0 + star_hash * 100.0) * 0.3 + 0.7;
            // Stars only once the sun is truly below the horizon — on a bright
            // dusk sky they read as rendering noise.
            float night_fade = smoothstep(-0.02, -0.2, sun.y) * smoothstep(0.05, 0.2, dir.y);
            col += vec3(0.9, 0.95, 1.0) * star_brightness * shimmer * night_fade * 1.35;
        }
    }

    // Moon — rides opposite the sun so it arcs across the night sky.
    vec3 moon_dir = -sun;
    float cos_moon = dot(dir, moon_dir);
    if (cos_moon > 0.9985 && moon_dir.y > -0.05) {
        // Surface mottling: stable per-direction, fake maria.
        vec3 m = floor(dir * 900.0);
        float maria = hash(m.xy + m.z * 0.37) * 0.5 + hash(m.yz * 1.13) * 0.5;
        float moon_disc = smoothstep(0.99935, 0.99975, cos_moon);
        float moon_edge = smoothstep(0.9989, 0.99935, cos_moon);
        float moon_up = smoothstep(-0.02, 0.06, moon_dir.y);
        vec3 moon_col = vec3(0.92, 0.94, 1.0) * mix(1.0, 0.72, maria * 0.8);
        col += moon_col * (moon_disc * 1.15 + moon_edge * 0.18) * moon_up;
        // faint halo
        col += vec3(0.55, 0.62, 0.8) * smoothstep(0.995, 0.9989, cos_moon) * 0.16 * moon_up;
    }

    // Cumulus clouds: domain-warped fbm with sun-side self-shading, shaded
    // undersides and time-of-day tinting (white noon, amber dusk, dark night).
    if (dir.y > 0.015 && u_clouds > 0.5) {
        vec2 plane_uv = dir.xz / (dir.y + 0.06);
        plane_uv *= 1.35;
        plane_uv += vec2(u_time * 0.030, u_time * 0.011);

        vec2 warp = vec2(fbm(plane_uv * 0.5), fbm(plane_uv * 0.5 + 7.3));
        float n_big = fbm(plane_uv * 1.4 + warp * 1.6);
        float n_det = fbm(plane_uv * 4.2 - warp * 0.8);
        float density = n_big * 0.72 + n_det * 0.28;

        // Puffy coverage with defined edges.
        float cov = smoothstep(0.46, 0.60, density);
        // Fake thickness: clouds thin out near the horizon.
        float thick = smoothstep(0.02, 0.22, dir.y);

        // Self-shading: density sampled toward the sun; where the cloud is
        // denser sun-side, we face a shaded wall; where sparser, sunlit top.
        vec2 sun_uv = normalize(sun.xz + vec2(1e-4)) * 0.85;
        float shade_n = fbm(plane_uv * 1.4 + sun_uv + warp * 1.2);
        float light_side = clamp(0.5 + (density - shade_n) * 2.4, 0.0, 1.0);
        // Deeper inside the field = shaded base.
        float depth = clamp((density - 0.52) * 3.2, 0.0, 1.0);

        vec3 lit = vec3(1.05, 1.03, 1.00);
        vec3 shaded = vec3(0.58, 0.64, 0.75);
        vec3 cloud_col = mix(shaded, lit, light_side) * (1.0 - depth * 0.55);

        // Time-of-day energy: bright white noon, amber dusk, dark blue night.
        float sun_up = clamp(sun.y * 4.0, 0.0, 1.0);
        vec3 day_tint = mix(vec3(1.10, 0.60, 0.36), vec3(1.0), sun_up);
        vec3 night_tint = vec3(0.085, 0.10, 0.17);
        vec3 tint = mix(night_tint, day_tint, smoothstep(-0.10, 0.14, sun.y));
        cloud_col *= tint;

        float horizon_fade = smoothstep(0.015, 0.25, dir.y);
        col = mix(col, cloud_col, cov * thick * horizon_fade * 0.92);
    }

    frag = vec4(col, 1.0);
}
