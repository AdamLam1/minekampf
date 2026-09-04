#include <gtest/gtest.h>
#include "audio/audio_engine.hpp"
#include "audio/sound_manager.hpp"

namespace mc {

TEST(AudioTest, EngineInitAndShutdown) {
    AudioEngine engine;
    
    // miniaudio init might fail in a CI environment without audio devices,
    // so we handle it gracefully. It returns false if it fails.
    bool success = engine.init();
    
    if (success) {
        // Can set listener attributes
        engine.set_listener_position(glm::vec3(0.0f, 0.0f, 0.0f));
        engine.set_listener_orientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        
        engine.shutdown();
    } else {
        GTEST_SKIP() << "Audio hardware not available, skipping AudioEngine test.";
    }
}

TEST(AudioTest, SoundManagerRegistration) {
    AudioEngine engine;
    // We don't strictly need to init the engine to test registration.
    SoundManager manager(engine);
    
    SoundEvent event;
    event.id = "test.sound";
    event.category = SoundCategory::BLOCKS;
    
    SoundEntry entry;
    entry.file_path = "non_existent.ogg";
    entry.weight = 1;
    event.variants.push_back(entry);
    
    manager.register_sound_event(event);
    
    // Test that it doesn't crash when playing a sound
    // If engine is not initialized or file is missing, it will gracefully warn/abort playing.
    manager.play_sound("test.sound", glm::vec3(0.0f));
    manager.update();
}

TEST(AudioTest, CategoryVolumes) {
    AudioEngine engine;
    engine.set_category_volume(SoundCategory::MUSIC, 0.5f);
    EXPECT_FLOAT_EQ(engine.get_category_volume(SoundCategory::MUSIC), 0.5f);
    
    engine.set_category_volume(SoundCategory::BLOCKS, 0.8f);
    EXPECT_FLOAT_EQ(engine.get_category_volume(SoundCategory::BLOCKS), 0.8f);
    
    // Unset category should return 1.0f (or whatever default we set)
    // Actually, in AudioEngine constructor we set all default categories to 1.0f
    EXPECT_FLOAT_EQ(engine.get_category_volume(SoundCategory::PLAYERS), 1.0f);
}

} // namespace mc
