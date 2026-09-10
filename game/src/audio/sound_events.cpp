#include "audio/sound_events.hpp"

#include "audio/sound_manager.hpp"
#include "core/logger.hpp"

#include <unordered_set>

namespace mc {

namespace {

// Material family per block id (top-level: what the block "is made of" for
// audio purposes). Defaults to stone for anything unlisted.
enum class Mat { Stone, Dirt, Grass, Wood, Sand };

Mat material_of(BlockId id) {
    switch (id) {
        case BLOCK_DIRT:
        case BLOCK_CLAY:
        case BLOCK_GRAVEL:
            return Mat::Dirt;
        case BLOCK_GRASS:
        case BLOCK_OAK_LEAVES:
        case BLOCK_SPRUCE_LEAVES:
        case BLOCK_BIRCH_LEAVES:
        case BLOCK_TALL_GRASS:
            return Mat::Grass;
        case BLOCK_OAK_LOG:
        case BLOCK_SPRUCE_LOG:
        case BLOCK_BIRCH_LOG:
        case BLOCK_OAK_PLANKS:
        case BLOCK_CRAFTING_TABLE:
        case BLOCK_BOOKSHELF:
            return Mat::Wood;
        case BLOCK_SAND:
        case BLOCK_SANDSTONE:
            return Mat::Sand;
        default:
            return Mat::Stone;
    }
}

const char* dig_event_for(Mat mat) {
    switch (mat) {
        case Mat::Dirt:  return "dig_dirt";
        case Mat::Grass: return "dig_grass";
        case Mat::Wood:  return "dig_wood";
        case Mat::Sand:  return "dig_sand";
        default:         return "dig_stone";
    }
}

const char* step_event_for(Mat mat) {
    switch (mat) {
        case Mat::Dirt:
        case Mat::Grass: return "step_grass";
        case Mat::Wood:  return "step_wood";
        default:         return "step_stone";
    }
}

struct EventDef {
    const char* id;
    const char* file;
    SoundCategory category;
    float volume;
};

// The starter set — one variant per event for now; more variants can be
// dropped into assets/sounds and appended here.
constexpr EventDef kEvents[] = {
    {"dig_stone", "assets/sounds/dig_stone.wav", SoundCategory::BLOCKS, 0.8f},
    {"dig_dirt", "assets/sounds/dig_dirt.wav", SoundCategory::BLOCKS, 0.8f},
    {"dig_grass", "assets/sounds/dig_grass.wav", SoundCategory::BLOCKS, 0.8f},
    {"dig_wood", "assets/sounds/dig_wood.wav", SoundCategory::BLOCKS, 0.8f},
    {"dig_sand", "assets/sounds/dig_sand.wav", SoundCategory::BLOCKS, 0.8f},
    {"place", "assets/sounds/place.wav", SoundCategory::BLOCKS, 0.7f},
    {"step_grass", "assets/sounds/step_grass.wav", SoundCategory::PLAYERS, 0.35f},
    {"step_stone", "assets/sounds/step_stone.wav", SoundCategory::PLAYERS, 0.35f},
    {"step_wood", "assets/sounds/step_wood.wav", SoundCategory::PLAYERS, 0.35f},
    {"hurt", "assets/sounds/hurt.wav", SoundCategory::PLAYERS, 0.9f},
    {"eat", "assets/sounds/eat.wav", SoundCategory::PLAYERS, 0.8f},
    {"click", "assets/sounds/click.wav", SoundCategory::MASTER, 0.6f},
    {"level_up", "assets/sounds/level_up.wav", SoundCategory::PLAYERS, 0.9f},
    {"splash", "assets/sounds/splash.wav", SoundCategory::BLOCKS, 0.8f},
    {"zombie_growl", "assets/sounds/zombie_growl.wav", SoundCategory::HOSTILE, 0.8f},
    {"skeleton_rattle", "assets/sounds/skeleton_rattle.wav", SoundCategory::HOSTILE, 0.7f},
    {"rain_loop", "assets/sounds/rain_loop.wav", SoundCategory::WEATHER, 0.0f},
    {"music_ambient", "assets/sounds/music_ambient.wav", SoundCategory::MUSIC, 0.5f},
};

} // namespace

struct SoundEvents::Impl {
    AudioEngine* engine = nullptr;
    std::unique_ptr<SoundManager> manager;
    bool rain_playing = false;
    float last_rain_intensity = -1.0f;
};

SoundEvents::SoundEvents() = default;

SoundEvents::~SoundEvents() {
    shutdown();
}

bool SoundEvents::init(AudioEngine* engine) {
    shutdown();
    if (!engine) return false;

    impl_ = std::make_unique<Impl>();
    impl_->engine = engine;
    impl_->manager = std::make_unique<SoundManager>(*engine);

    int registered = 0;
    for (const auto& def : kEvents) {
        SoundEvent ev;
        ev.id = def.id;
        ev.category = def.category;
        SoundEntry entry;
        entry.file_path = def.file;
        entry.base_volume = def.volume;
        ev.variants.push_back(entry);
        impl_->manager->register_sound_event(ev);
        ++registered;
    }

    MC_LOG_INFO("SoundEvents: {} events registered", registered);
    return registered > 0;
}

void SoundEvents::shutdown() {
    impl_.reset();
}

void SoundEvents::dig(BlockId block, const glm::vec3& pos) {
    if (!impl_) return;
    impl_->manager->play_sound(dig_event_for(material_of(block)), pos);
}

void SoundEvents::place(BlockId block, const glm::vec3& pos) {
    if (!impl_) return;
    (void)block;
    impl_->manager->play_sound("place", pos);
}

void SoundEvents::step(BlockId block, const glm::vec3& pos) {
    if (!impl_) return;
    impl_->manager->play_sound(step_event_for(material_of(block)), pos);
}

void SoundEvents::hurt(const glm::vec3& pos) {
    if (!impl_) return;
    impl_->manager->play_sound("hurt", pos);
}

void SoundEvents::eat(const glm::vec3& pos) {
    if (!impl_) return;
    impl_->manager->play_sound("eat", pos);
}

void SoundEvents::mob_hurt(const std::string& species, const glm::vec3& pos) {
    if (!impl_) return;
    if (species == "zombie" || species == "husk") {
        impl_->manager->play_sound("zombie_growl", pos);
    } else if (species == "skeleton" || species == "stray") {
        impl_->manager->play_sound("skeleton_rattle", pos);
    }
}

void SoundEvents::level_up() {
    if (!impl_) return;
    impl_->manager->play_sound_global("level_up");
}

void SoundEvents::ui_click() {
    if (!impl_) return;
    impl_->manager->play_sound_global("click", 0.7f);
}

void SoundEvents::rain(float intensity, const glm::vec3& listener_pos) {
    if (!impl_) return;
    // Fire-and-forget loop restarted while it rains; miniaudio keeps it alive
    // until it finishes, so re-trigger every ~2 s of intensity.
    if (intensity <= 0.05f) {
        impl_->rain_playing = false;
        impl_->last_rain_intensity = intensity;
        return;
    }
    bool due = impl_->last_rain_intensity < 0.0f ||
               intensity - impl_->last_rain_intensity > 0.15f ||
               !impl_->rain_playing;
    if (due) {
        impl_->manager->play_sound("rain_loop", listener_pos, intensity);
        impl_->rain_playing = true;
        impl_->last_rain_intensity = intensity;
    }
}

} // namespace mc
