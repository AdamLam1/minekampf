#include "audio/sound_manager.hpp"
#include <miniaudio.h>
#include <spdlog/spdlog.h>
#include <random>

namespace mc {

SoundManager::SoundManager(AudioEngine& engine) : m_engine(engine) {
}

SoundManager::~SoundManager() {
    for (auto& snd : m_active_sounds) {
        ma_sound_uninit(snd.get());
    }
    m_active_sounds.clear();
}

void SoundManager::register_sound_event(const SoundEvent& event) {
    m_registry[event.id] = event;
}

const SoundEntry& SoundManager::select_random_variant(const SoundEvent& event) {
    if (event.variants.empty()) {
        static SoundEntry empty_entry;
        return empty_entry;
    }
    
    // Simple random selection based on weight
    uint32_t total_weight = 0;
    for (const auto& var : event.variants) {
        total_weight += var.weight;
    }
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, total_weight - 1);
    uint32_t roll = dist(gen);
    
    uint32_t cumulative = 0;
    for (const auto& var : event.variants) {
        cumulative += var.weight;
        if (roll < cumulative) {
            return var;
        }
    }
    
    return event.variants.back();
}

void SoundManager::play_sound(const std::string& event_id, const glm::vec3& position, float volume, float pitch) {
    play_internal(event_id, &position, volume, pitch);
}

void SoundManager::play_sound_global(const std::string& event_id, float volume, float pitch) {
    play_internal(event_id, nullptr, volume, pitch);
}

void SoundManager::play_internal(const std::string& event_id, const glm::vec3* position, float volume, float pitch) {
    auto it = m_registry.find(event_id);
    if (it == m_registry.end()) {
        spdlog::warn("Attempted to play unknown sound event: {}", event_id);
        return;
    }
    
    const SoundEvent& event = it->second;
    const SoundEntry& entry = select_random_variant(event);
    if (entry.file_path.empty()) return;
    
    auto* ma_engine_ptr = m_engine.get_backend_engine();
    if (!ma_engine_ptr) return;
    
    float category_vol = m_engine.get_category_volume(event.category);
    float effective_volume = volume * entry.base_volume * category_vol;
    
    if (effective_volume <= 0.001f) return;
    
    auto snd = std::make_unique<ma_sound>();
    
    // If stream is true, it streams from disk. If false, miniaudio decodes it wholly into memory by default 
    // when using ma_sound_init_from_file with MA_SOUND_FLAG_DECODE.
    ma_uint32 flags = entry.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (position == nullptr) {
        // Global sound (no spatialization)
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    }
    
    ma_result result = ma_sound_init_from_file(ma_engine_ptr, entry.file_path.c_str(), flags, nullptr, nullptr, snd.get());
    if (result != MA_SUCCESS) {
        spdlog::warn("Failed to load sound: {} (code {})", entry.file_path, (int)result);
        return;
    }
    
    ma_sound_set_volume(snd.get(), effective_volume);
    ma_sound_set_pitch(snd.get(), pitch * entry.base_pitch);
    
    if (position != nullptr) {
        ma_sound_set_position(snd.get(), position->x, position->y, position->z);
        // Miniaudio defaults to a sensible distance model, but we can set max distance
        ma_sound_set_max_distance(snd.get(), entry.attenuation_distance * 4.0f);
        ma_sound_set_rolloff(snd.get(), 1.0f);
    }
    
    ma_sound_start(snd.get());
    m_active_sounds.push_back(std::move(snd));
    
    // Cull sounds if we exceed max concurrent sounds (e.g., 28)
    if (m_active_sounds.size() > 28) {
        // Just uninit and remove the oldest one
        ma_sound_uninit(m_active_sounds.front().get());
        m_active_sounds.erase(m_active_sounds.begin());
    }
}

void SoundManager::update() {
    // Remove stopped sounds
    for (auto it = m_active_sounds.begin(); it != m_active_sounds.end(); ) {
        if (!ma_sound_is_playing(it->get())) {
            ma_sound_uninit(it->get());
            it = m_active_sounds.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace mc
