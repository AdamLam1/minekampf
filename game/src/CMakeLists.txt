# Engine static library — all subsystems compose into one lib for simplicity.
file(GLOB_RECURSE MINEKAMPF_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/world/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/renderer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gameplay/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gameplay/ai/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/generation/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/network/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/save/*.cpp"
)

add_library(minekampf_engine STATIC ${MINEKAMPF_SOURCES})
target_include_directories(minekampf_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(minekampf_engine
    PUBLIC
        glfw
        glm::glm
        spdlog::spdlog
        glad
        zlib
        nlohmann_json::nlohmann_json
)

if(MINEKAMPF_ENABLE_TRACY)
    target_link_libraries(minekampf_engine PUBLIC TracyClient)
    target_compile_definitions(minekampf_engine PUBLIC TRACY_ENABLE)
endif()

# If asio doesn't provide a target, add its include directory manually
FetchContent_GetProperties(asio)
target_include_directories(minekampf_engine PUBLIC ${asio_SOURCE_DIR}/asio/include)

FetchContent_GetProperties(miniaudio)
target_include_directories(minekampf_engine PUBLIC ${miniaudio_SOURCE_DIR})

# MSVC-friendly compile options (target-scoped — Golden Rule: target_* only)
if(MSVC)
    target_compile_options(minekampf_engine PRIVATE
        /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus /utf-8 /EHsc
        /MP
        $<$<BOOL:${MINEKAMPF_WARNINGS_AS_ERRORS}>:/WX>
    )
    target_compile_definitions(minekampf_engine PRIVATE
        _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(minekampf_engine PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<BOOL:${MINEKAMPF_WARNINGS_AS_ERRORS}>:-Werror>
    )
    target_compile_options(minekampf_engine PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer>)
    target_link_options(minekampf_engine PRIVATE $<$<CONFIG:Debug>:-fsanitize=address,undefined>)
endif()

target_compile_definitions(minekampf_engine PRIVATE
    $<$<CONFIG:Debug>:MINEKAMPF_DEBUG=1>
    $<$<CONFIG:Release>:MINEKAMPF_RELEASE=1>
    GLM_FORCE_SILENT_WARNINGS
    GLM_ENABLE_EXPERIMENTAL
    ASIO_STANDALONE
)
target_compile_definitions(minekampf_engine PUBLIC MINEKAMPF_VERSION="${PROJECT_VERSION}")

# Keep GLFW from pulling a system GL header — glad is the GL loader.
target_compile_definitions(minekampf_engine PUBLIC GLFW_INCLUDE_NONE)

# ---------------------------------------------------------------------------
# Game executable
# ---------------------------------------------------------------------------
add_executable(minekampf main.cpp)
target_link_libraries(minekampf PRIVATE minekampf_engine)

# ---------------------------------------------------------------------------
# Offline asset dumper (QA): writes contact-sheet PNGs of procedural art.
# ---------------------------------------------------------------------------
add_executable(minekampf_assets dev/asset_dump.cpp)
target_link_libraries(minekampf_assets PRIVATE minekampf_engine)
target_compile_definitions(minekampf_assets PRIVATE
    _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN MC_ATLAS_DEBUG_TILE=1
)
if(MSVC)
    target_compile_options(minekampf_assets PRIVATE /W4 /utf-8 /EHsc)
endif()

# Copy shaders next to the binary so they can be loaded at runtime.
# A dedicated ALWAYS-runs target (not POST_BUILD) so that editing a .vert/.frag
# re-copies even when no C++ file changed — otherwise stale shaders ship.
add_custom_target(minekampf_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:minekampf>/shaders
)
add_dependencies(minekampf minekampf_shaders)

# Copy swapable art assets (Blockbench models, texture overrides) next to the
# binary; ALWAYS-runs so edited assets refresh on every build.
add_custom_target(minekampf_assets_copy ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/../assets
            $<TARGET_FILE_DIR:minekampf>/assets
)
add_dependencies(minekampf minekampf_assets_copy)
