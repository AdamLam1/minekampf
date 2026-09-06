add_executable(minekampf_tests
    test_coordinates.cpp
    test_paletted_container.cpp
    test_noise.cpp
    test_collision.cpp
    test_raycast.cpp
    test_network.cpp
    test_network_frames.cpp
    test_multiplayer_protocol.cpp
    test_chunk_codec.cpp
    test_structures.cpp
    test_inventory.cpp
    test_crafting.cpp
    test_crafting_ext.cpp
    test_enchanting.cpp
    test_audio.cpp
    test_nbt.cpp
    test_region.cpp
    test_mining.cpp
    test_survival.cpp
    test_durability.cpp
    test_mob_rig.cpp
    test_smelting.cpp
    test_bow.cpp
    test_quest.cpp
    test_redstone.cpp
    test_modmanager.cpp
    test_custom_mobs.cpp
    test_biomes.cpp
    test_safesave.cpp
    test_ai.cpp
)
target_link_libraries(minekampf_tests PRIVATE minekampf_engine GTest::gtest_main)

if(MINEKAMPF_ENABLE_TRACY)
    target_link_libraries(minekampf_tests PRIVATE TracyClient)
endif()

if(MSVC)
    target_compile_options(minekampf_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_definitions(minekampf_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GoogleTest)
gtest_discover_tests(minekampf_tests)
