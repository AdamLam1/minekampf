#pragma once

#include "audio/audio_engine.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <glm/vec3.hpp>

// Forward declarations
struct ma_sound;

namespace mc {

struct SoundEntry {
    std::string file_path;
    uint32_t weight = 1;
    bool stream = false;
    float attenuation_distance = 16.0f;
    float base_pitch = 1.0f;
    float base_volume = 1.0f;
};

struct SoundEvent {
    std::string id;
    std::vector<SoundEntry> variants;
    SoundCategory category = SoundCategory::BLOCKS;
};

class SoundManager {
public:
    SoundManager(AudioEngine& engine);
    ~SoundManager();

    // Disable copy/move
    SoundManager(const SoundManager&) = delete;
    SoundManager& operator=(const SoundManager&) = delete;

    void register_sound_event(const SoundEvent& event);
    
    // Play a 3D spatialized sound
    void play_sound(const std::string& event_id, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f);
    
    // Play a 2D UI/Global sound (e.g. level up)
    void play_sound_global(const std::string& event_id, float volume = 1.0f, float pitch = 1.0f);

    // Update periodic tasks (culling, cleaning up finished sounds)
    void update();

private:
    const SoundEntry& select_random_variant(const SoundEvent& event);
    void play_internal(const std::string& event_id, const glm::vec3* position, float volume, float pitch);

    AudioEngine& m_engine;
    std::unordered_map<std::string, SoundEvent> m_registry;
    
    // Active sounds. In a real engine, we'd use a pool of max 28 sounds and cull them.
    // miniaudio automatically manages many concurrent sounds via ma_sound, but we need to keep the object alive.
    // For simplicity, we just store dynamically allocated sounds and clean them up when they stop.
    std::vector<std::unique_ptr<ma_sound>> m_active_sounds;
};

} // namespace mc
