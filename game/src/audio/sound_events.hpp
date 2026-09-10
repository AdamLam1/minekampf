#pragma once

#include <glm/vec3.hpp>
#include <memory>
#include <string>

#include "audio/audio_engine.hpp"
#include "world/block.hpp"

namespace mc {

class SoundManager;

// Gameplay-facing sound facade. Owns the SoundManager, registers the starter
// event set from assets/sounds/ and exposes intent-level calls ("dig", "step",
// "hurt") so game code never touches event ids directly. Safe to construct
// with nullptr engine (headless builds / tests) — every call becomes a no-op.
class SoundEvents {
public:
    SoundEvents(); // defined in the .cpp: Impl is incomplete in the header
    ~SoundEvents();

    SoundEvents(const SoundEvents&) = delete;
    SoundEvents& operator=(const SoundEvents&) = delete;

    // Loads every registered event; returns false when the engine is missing
    // or no sound files could be found (game continues silently).
    bool init(AudioEngine* engine);
    void shutdown();
    void update() { /* reserved: per-frame culling */ }

    // Block feedback. Digs map to material families (stone/dirt/grass/wood/sand).
    void dig(BlockId block, const glm::vec3& pos);
    void place(BlockId block, const glm::vec3& pos);
    void step(BlockId block, const glm::vec3& pos);

    void hurt(const glm::vec3& pos);
    void eat(const glm::vec3& pos);
    void mob_hurt(const std::string& species, const glm::vec3& pos);
    void level_up();
    void ui_click();

    // Rain bed loop: intensity 0..1 fades the looped weather sound around the
    // listener. Call every frame (internally throttled).
    void rain(float intensity, const glm::vec3& listener_pos);

    [[nodiscard]] bool ok() const { return impl_ != nullptr; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mc
