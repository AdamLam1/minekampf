#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace mc {

// User settings (graphics / gameplay / audio). Persisted as a simple
// key=value file in %APPDATA%/.minekampf/settings.cfg.
struct Settings {
    // Graphics
    int render_distance = 6;        // chunks (2..10)
    float fov = 70.0f;              // degrees (60..110)
    bool shadows = true;
    bool particles = true;
    bool view_bobbing = true;
    bool vsync = true;
    bool fullscreen = false;
    int quality = 2;                // 0=low, 1=medium, 2=high (renderer preset)
    float render_scale = 1.0f;      // world render scale (0.5..1.0, OptiFine-style)

    // Gameplay
    float mouse_sensitivity = 1.0f; // multiplier (0.2..3.0)

    // Audio (0..1)
    float volume_master = 1.0f;
    float volume_blocks = 1.0f;
    float volume_ambient = 1.0f;
    float volume_weather = 1.0f;

    static std::string config_path() {
        const char* appdata = std::getenv("APPDATA");
        std::string base = appdata ? std::string(appdata) + "/.minekampf" : ".minekampf";
#ifdef _WIN32
        _mkdir(base.c_str());
#else
        mkdir(base.c_str(), 0755);
#endif
        return base + "/settings.cfg";
    }

    void save() const {
        FILE* f = std::fopen(config_path().c_str(), "w");
        if (!f) return;
        std::fprintf(f, "render_distance=%d\n", render_distance);
        std::fprintf(f, "render_scale=%.2f\n", render_scale);
        std::fprintf(f, "fov=%.1f\n", fov);
        std::fprintf(f, "shadows=%d\n", shadows ? 1 : 0);
        std::fprintf(f, "particles=%d\n", particles ? 1 : 0);
        std::fprintf(f, "view_bobbing=%d\n", view_bobbing ? 1 : 0);
        std::fprintf(f, "vsync=%d\n", vsync ? 1 : 0);
        std::fprintf(f, "fullscreen=%d\n", fullscreen ? 1 : 0);
        std::fprintf(f, "quality=%d\n", quality);
        std::fprintf(f, "mouse_sensitivity=%.2f\n", mouse_sensitivity);
        std::fprintf(f, "volume_master=%.2f\n", volume_master);
        std::fprintf(f, "volume_blocks=%.2f\n", volume_blocks);
        std::fprintf(f, "volume_ambient=%.2f\n", volume_ambient);
        std::fprintf(f, "volume_weather=%.2f\n", volume_weather);
        std::fclose(f);
    }

    static float parse_float(const std::string& v, float def) {
        try { return std::stof(v); } catch (...) { return def; }
    }
    static int parse_int(const std::string& v, int def) {
        try { return std::stoi(v); } catch (...) { return def; }
    }

    static Settings load() {
        Settings s;
        FILE* f = std::fopen(config_path().c_str(), "r");
        if (!f) return s;
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            std::string ln(line);
            auto eq = ln.find('=');
            if (eq == std::string::npos) continue;
            std::string key = ln.substr(0, eq);
            std::string val = ln.substr(eq + 1);
            while (!val.empty() && (val.back() == '\n' || val.back() == '\r')) val.pop_back();
            if (key == "render_distance") s.render_distance = parse_int(val, s.render_distance);
            else if (key == "fov") s.fov = parse_float(val, s.fov);
            else if (key == "render_scale") s.render_scale = parse_float(val, s.render_scale);
            else if (key == "shadows") s.shadows = parse_int(val, s.shadows ? 1 : 0) != 0;
            else if (key == "particles") s.particles = parse_int(val, s.particles ? 1 : 0) != 0;
            else if (key == "view_bobbing") s.view_bobbing = parse_int(val, s.view_bobbing ? 1 : 0) != 0;
            else if (key == "vsync") s.vsync = parse_int(val, s.vsync ? 1 : 0) != 0;
            else if (key == "fullscreen") s.fullscreen = parse_int(val, s.fullscreen ? 1 : 0) != 0;
            else if (key == "quality") s.quality = parse_int(val, s.quality);
            else if (key == "mouse_sensitivity") s.mouse_sensitivity = parse_float(val, s.mouse_sensitivity);
            else if (key == "volume_master") s.volume_master = parse_float(val, s.volume_master);
            else if (key == "volume_blocks") s.volume_blocks = parse_float(val, s.volume_blocks);
            else if (key == "volume_ambient") s.volume_ambient = parse_float(val, s.volume_ambient);
            else if (key == "volume_weather") s.volume_weather = parse_float(val, s.volume_weather);
        }
        std::fclose(f);
        // Clamp to safe ranges.
        s.render_distance = std::clamp(s.render_distance, 2, 10);
        s.quality = std::clamp(s.quality, 0, 2);
        s.fov = std::clamp(s.fov, 60.0f, 110.0f);
        s.render_scale = std::clamp(s.render_scale, 0.5f, 1.0f);
        s.mouse_sensitivity = std::clamp(s.mouse_sensitivity, 0.2f, 3.0f);
        s.volume_master = std::clamp(s.volume_master, 0.0f, 1.0f);
        s.volume_blocks = std::clamp(s.volume_blocks, 0.0f, 1.0f);
        s.volume_ambient = std::clamp(s.volume_ambient, 0.0f, 1.0f);
        s.volume_weather = std::clamp(s.volume_weather, 0.0f, 1.0f);
        return s;
    }
};

} // namespace mc
