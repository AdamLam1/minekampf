#pragma once

#include <array>
#include <chrono>
#include <string_view>
#include <cstdio>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScoped
#define FrameMark
#endif

namespace mc {

enum class ProfileSection : uint8_t {
    NetworkInput,
    ScheduledTicks,
    RandomTicks,
    EntityTicking,
    MobSpawning,
    ChunkManagement,
    NetworkOutput,
    FluidProcessing,
    WeatherTick,
    Autosave,
    TotalTick,
    // Client
    InputProcessing,
    CameraUpdate,
    MeshBuilding,
    VboUpload,
    FrustumCulling,
    RenderOpaque,
    RenderTransparent,
    RenderSky,
    RenderParticles,
    RenderGUI,
    PresentFrame,
    TotalFrame,

    Count
};

class Profiler {
public:
    static Profiler& get() {
        static Profiler instance;
        return instance;
    }

    void begin_section(ProfileSection sec) {
        m_section_starts[static_cast<size_t>(sec)] = clock::now();
    }

    void end_section(ProfileSection sec) {
        auto end = clock::now();
        auto start = m_section_starts[static_cast<size_t>(sec)];
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        m_timings_us[static_cast<size_t>(sec)] = static_cast<float>(us);
    }

    [[nodiscard]] float timing_us(ProfileSection sec) const {
        return m_timings_us[static_cast<size_t>(sec)];
    }

    [[nodiscard]] float timing_ms(ProfileSection sec) const {
        return m_timings_us[static_cast<size_t>(sec)] * 0.001f;
    }

    [[nodiscard]] static constexpr std::string_view name(ProfileSection sec) {
        constexpr std::string_view names[] = {
            "NetIn", "SchedTick", "RandTick", "EntTick", "MobSpawn",
            "ChunkMgmt", "NetOut", "Fluids", "Weather", "Autosave", "TotalTick",
            "Input", "Camera", "MeshBuild", "VboUpload", "Cull",
            "Opaque", "Transparent", "Sky", "Particles", "GUI", "Present", "TotalFrame"
        };
        return names[static_cast<size_t>(sec)];
    }

    void reset() { m_timings_us.fill(0); }

    // Format a single-line stats string for the debug overlay / title bar.
    [[nodiscard]] std::string tick_summary() const {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "Tick:%.1fms net:%.0f sched:%.0f rand:%.0f ent:%.0f mobs:%.0f chunk:%.0f fluid:%.0f",
            timing_ms(ProfileSection::TotalTick),
            timing_us(ProfileSection::NetworkInput),
            timing_us(ProfileSection::ScheduledTicks),
            timing_us(ProfileSection::RandomTicks),
            timing_us(ProfileSection::EntityTicking),
            timing_us(ProfileSection::MobSpawning),
            timing_us(ProfileSection::ChunkManagement),
            timing_us(ProfileSection::FluidProcessing));
        return buf;
    }

    [[nodiscard]] std::string frame_summary() const {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "Frame:%.1fms cull:%.0f opaque:%.0f trans:%.0f sky:%.0f part:%.0f gui:%.0f mesh:%.0f",
            timing_ms(ProfileSection::TotalFrame),
            timing_us(ProfileSection::FrustumCulling),
            timing_us(ProfileSection::RenderOpaque),
            timing_us(ProfileSection::RenderTransparent),
            timing_us(ProfileSection::RenderSky),
            timing_us(ProfileSection::RenderParticles),
            timing_us(ProfileSection::RenderGUI),
            timing_us(ProfileSection::MeshBuilding));
        return buf;
    }

private:
    using clock = std::chrono::steady_clock;
    Profiler() = default;

    std::array<std::chrono::steady_clock::time_point, static_cast<size_t>(ProfileSection::Count)> m_section_starts{};
    std::array<float, static_cast<size_t>(ProfileSection::Count)> m_timings_us{};
};

// RAII scope timer for convenient profiling
struct ProfileScope {
    explicit ProfileScope(ProfileSection sec) : section(sec) {
        Profiler::get().begin_section(section);
    }
    ~ProfileScope() {
        Profiler::get().end_section(section);
    }
    ProfileSection section;
};

} // namespace mc
