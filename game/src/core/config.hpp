#pragma once

// Global engine constants. Centralized so tuning is one-edit.
//
// Deviation note (vs plan/PHASE2.md): the plan models the 1.18+ world
// (Y: -64..320, 24 sections, 384 blocks tall). For a performant playable
// foundation we use the classic 0..255 column (16 sections). The density/
// generation pipeline is unchanged in structure; only the vertical extent
// is reduced. Raising WORLD_HEIGHT later only changes these constants and
// the per-chunk section count — no algorithmic change required.

namespace mc {

inline constexpr int CHUNK_SIZE = 16;            // blocks per chunk side (XZ)
inline constexpr int SECTION_SIZE = 16;          // blocks per section side
inline constexpr int SECTION_VOLUME = SECTION_SIZE * SECTION_SIZE * SECTION_SIZE; // 4096

inline constexpr int MIN_Y = 0;                  // lowest block Y
inline constexpr int WORLD_HEIGHT = 256;         // blocks tall (MAX_Y = MIN_Y + WORLD_HEIGHT)
inline constexpr int MAX_Y = MIN_Y + WORLD_HEIGHT;
inline constexpr int SECTIONS_PER_CHUNK = WORLD_HEIGHT / SECTION_SIZE; // 16

inline constexpr int SEA_LEVEL = 64;             // ocean surface
inline constexpr int BEDROCK_FLOOR = 0;          // lowest solid layer

// Server / client timing (PHASE1 §1.4)
inline constexpr int SERVER_TPS = 20;            // 20 Hz
inline constexpr double TICK_INTERVAL_SEC = 1.0 / static_cast<double>(SERVER_TPS); // 50ms
inline constexpr double TICK_INTERVAL_MS = 1000.0 / static_cast<double>(SERVER_TPS);

inline constexpr int AUTOSAVE_TICKS = 6000;      // 5 minutes at 20 TPS

// Interaction reach (PHASE4 §4.2)
inline constexpr float REACH_SURVIVAL = 4.5f;
inline constexpr float REACH_CREATIVE = 5.0f;

// Player dimensions (PHASE4 §1.1)
inline constexpr float PLAYER_WIDTH = 0.6f;
inline constexpr float PLAYER_HEIGHT = 1.8f;
inline constexpr float PLAYER_EYE_HEIGHT = 1.62f;

// Physics constants (PHASE4 §3)
inline constexpr float GRAVITY_PER_TICK = 0.08f;
inline constexpr float VERTICAL_DRAG = 0.98f;
inline constexpr float JUMP_VELOCITY = 0.42f;
inline constexpr float GROUND_DRAG = 0.546f;     // 0.6 slipperiness * 0.91
inline constexpr float AIR_DRAG = 0.91f;
inline constexpr float WALK_SPEED = 0.1f;

// Random tick gamerule default (PHASE5 §1.3)
inline constexpr int RANDOM_TICK_SPEED = 1;

// World boundary — fixed 16x16 chunk world centered at (0,0).
inline constexpr int WORLD_CHUNK_HALF = 8;
inline constexpr int WORLD_CHUNK_MIN = -WORLD_CHUNK_HALF;
inline constexpr int WORLD_CHUNK_MAX = WORLD_CHUNK_HALF - 1;

// Render defaults
inline constexpr int DEFAULT_RENDER_DISTANCE = 6;   // chunks (radius)
inline constexpr int MAX_FRAMES_IN_FLIGHT = 2;

} // namespace mc
