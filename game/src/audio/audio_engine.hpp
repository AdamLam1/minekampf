#pragma once

#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declare ma_engine so we don't need to include miniaudio.h in the header
struct ma_engine;

namespace mc {

enum class SoundCategory {
    MASTER,
    MUSIC,
    RECORDS,
    WEATHER,
    BLOCKS,
    HOSTILE,
    NEUTRAL,
    PLAYERS,
    AMBIENT,
    VOICE
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    // Disable copy/move
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init();
    void shutdown();

    // Listener (Player)
    void set_listener_position(const glm::vec3& position);
    void set_listener_orientation(const glm::vec3& forward, const glm::vec3& up);

    // Categories
    void set_category_volume(SoundCategory category, float volume);
    float get_category_volume(SoundCategory category) const;

    // Internal access for SoundManager
    ma_engine* get_backend_engine() const { return m_engine.get(); }

private:
    std::unique_ptr<ma_engine> m_engine;
    bool m_initialized = false;
    
    std::unordered_map<SoundCategory, float> m_category_volumes;
    
    // miniaudio doesn't have native "categories" out of the box in the simplest API,
    // so we'll handle category volumes manually per sound, or use sound groups.
    // For simplicity, we just store the volumes here.
};

} // namespace mc
