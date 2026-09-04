#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "audio/audio_engine.hpp"
#include <spdlog/spdlog.h>

namespace mc {

AudioEngine::AudioEngine() {
    m_engine = std::make_unique<ma_engine>();
    
    // Initialize default volumes
    m_category_volumes[SoundCategory::MASTER] = 1.0f;
    m_category_volumes[SoundCategory::MUSIC] = 1.0f;
    m_category_volumes[SoundCategory::RECORDS] = 1.0f;
    m_category_volumes[SoundCategory::WEATHER] = 1.0f;
    m_category_volumes[SoundCategory::BLOCKS] = 1.0f;
    m_category_volumes[SoundCategory::HOSTILE] = 1.0f;
    m_category_volumes[SoundCategory::NEUTRAL] = 1.0f;
    m_category_volumes[SoundCategory::PLAYERS] = 1.0f;
    m_category_volumes[SoundCategory::AMBIENT] = 1.0f;
    m_category_volumes[SoundCategory::VOICE] = 1.0f;
}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init() {
    if (m_initialized) return true;

    ma_engine_config engineConfig = ma_engine_config_init();
    // Configure default spatialization
    engineConfig.listenerCount = 1;

    ma_result result = ma_engine_init(&engineConfig, m_engine.get());
    if (result != MA_SUCCESS) {
        spdlog::error("Failed to initialize Audio Engine (miniaudio code: {})", (int)result);
        return false;
    }

    m_initialized = true;
    spdlog::info("Audio Engine initialized successfully.");
    return true;
}

void AudioEngine::shutdown() {
    if (m_initialized) {
        ma_engine_uninit(m_engine.get());
        m_initialized = false;
        spdlog::info("Audio Engine shut down.");
    }
}

void AudioEngine::set_listener_position(const glm::vec3& position) {
    if (!m_initialized) return;
    ma_engine_listener_set_position(m_engine.get(), 0, position.x, position.y, position.z);
}

void AudioEngine::set_listener_orientation(const glm::vec3& forward, const glm::vec3& up) {
    if (!m_initialized) return;
    ma_engine_listener_set_direction(m_engine.get(), 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(m_engine.get(), 0, up.x, up.y, up.z);
}

void AudioEngine::set_category_volume(SoundCategory category, float volume) {
    m_category_volumes[category] = volume;
    if (category == SoundCategory::MASTER && m_initialized) {
        ma_engine_set_volume(m_engine.get(), volume);
    }
}

float AudioEngine::get_category_volume(SoundCategory category) const {
    auto it = m_category_volumes.find(category);
    if (it != m_category_volumes.end()) {
        return it->second;
    }
    return 1.0f;
}

} // namespace mc
