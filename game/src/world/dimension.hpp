#pragma once

#include <cstdint>

namespace mc {

enum class DimensionId : uint8_t {
    Overworld = 0,
    Nether = 1,
    End = 2
};

struct DimensionType {
    DimensionId id;
    bool has_skylight;
    bool has_ceiling;
    float ambient_light;
    int logical_height;
    int min_y;
    int height;
    bool bed_works;
    double coordinate_scale;
    bool ultrawarm;
};

inline DimensionType get_dimension_type(DimensionId id) {
    switch (id) {
        case DimensionId::Nether:
            return {
                DimensionId::Nether,
                false, // has_skylight
                true,  // has_ceiling
                0.1f,  // ambient_light
                128,   // logical_height
                0,     // min_y
                256,   // height
                false, // bed_works
                8.0,   // coordinate_scale
                true   // ultrawarm
            };
        case DimensionId::End:
            return {
                DimensionId::End,
                false, // has_skylight
                false, // has_ceiling
                0.0f,  // ambient_light
                256,   // logical_height
                0,     // min_y
                256,   // height
                false, // bed_works
                1.0,   // coordinate_scale
                false  // ultrawarm
            };
        case DimensionId::Overworld:
        default:
            return {
                DimensionId::Overworld,
                true,  // has_skylight
                false, // has_ceiling
                0.0f,  // ambient_light
                384,   // logical_height
                -64,   // min_y
                384,   // height
                true,  // bed_works
                1.0,   // coordinate_scale
                false  // ultrawarm
            };
    }
}

} // namespace mc
