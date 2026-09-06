#pragma once

#include <cstdint>
#include <string>

#include "core/math.hpp"
#include "gameplay/entity.hpp"
#include "network/packet.hpp"

namespace mc {

// Registry id of the synthetic "player" species (0 fallback when absent —
// e.g. unit tests that reset the registry to built-ins only).
[[nodiscard]] uint8_t player_species_id();

// A player other than the local one, as seen through the multiplayer
// session. Positions are double-buffered (prev/next snapshot) and
// interpolated at render time with the same alpha as the local player.
struct RemotePlayer {
    int32_t id = 0;
    std::string name;
    Vec3 pos{0.f, 100.f, 0.f};     // latest snapshot (render interpolates from prev_pos)
    Vec3 prev_pos{0.f, 100.f, 0.f};
    Vec3 velocity{0.f, 0.f, 0.f};
    float yaw = 0.f, pitch = 0.f;
    float prev_yaw = 0.f, prev_pitch = 0.f;
    uint8_t flags = 0;
    float health = 20.0f;
    bool visible = false; // has received at least one snapshot

    [[nodiscard]] bool sneaking() const { return (flags & net::PLAYER_FLAG_SNEAKING) != 0; }
    [[nodiscard]] bool sprinting() const { return (flags & net::PLAYER_FLAG_SPRINTING) != 0; }
    [[nodiscard]] bool swinging() const { return (flags & net::PLAYER_FLAG_SWINGING) != 0; }

    // Interpolated state for rendering (alpha in [0,1), same value the
    // renderer passes for local view bobbing).
    [[nodiscard]] Vec3 interp_pos(float alpha) const {
        return prev_pos + (pos - prev_pos) * alpha;
    }

    // Render-only Mob view so remote players share the mob rig pipeline
    // (synthetic "player" species, humanoid fallback rig). Never used for AI.
    [[nodiscard]] Mob to_render_mob(float alpha) const {
        Mob m;
        m.type = static_cast<MobType>(player_species_id());
        m.pos = interp_pos(alpha);
        m.velocity = velocity;
        m.yaw = yaw;
        m.pitch = pitch;
        m.body_width = 0.6f;
        m.body_height = 1.8f;
        m.health = health;
        m.max_health = 20.0f;
        m.attack_anim = swinging() ? 1.0f : 0.0f;
        return m;
    }
};

} // namespace mc
