#pragma once

// Blockbench model pipeline: runtime loader for Bedrock entity geometry
// (.geo.json — what Blockbench's "Minecraft Entity" projects export).
//
// Workflow for artists (human or AI):
//   1. Model the entity in Blockbench (blockbench.net).
//   2. File -> Export -> "Bedrock Geometry" -> assets/models/mobs/<name>.geo.json
//   3. Export texture as <name>.png next to it (box UV unwrap).
// The game hot-swaps the model at startup; deleting the file falls back to
// the procedural rig. Units: Bedrock model space is 1/16 of a block, Y up,
// feet at y = 0.

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace mc {

enum class GeoAnim {
    None,     // no automatic animation (torso, accessories)
    Head,     // follows mob pitch
    ArmLeft,  // walk swing + attack chop
    ArmRight,
    LegLeft,  // walk swing
    LegRight,
    LegFL,    // quadruped diagonal gait
    LegFR,
    LegBL,
    LegBR,
};

struct GeoCube {
    glm::vec3 origin{0.0f};  // cube corner in model units (1/16 block)
    glm::vec3 size{0.0f};    // extents in model units
    glm::vec2 uv{0.0f};      // box-unwrap origin in texture pixels
    bool mirror = false;
    float inflate = 0.0f;    // outward padding in model units
};

struct GeoBone {
    std::string name;
    int parent = -1;              // index into GeoModel::bones
    glm::vec3 pivot{0.0f};        // joint position, model units
    float base_rot_x = 0.0f;      // rest pose X rotation, radians
    GeoAnim anim = GeoAnim::None; // derived from the bone name
    std::vector<GeoCube> cubes;
};

struct GeoModel {
    std::vector<GeoBone> bones;
    float tex_w = 64.0f;      // declared texture size (for UV normalization)
    float tex_h = 64.0f;

    // Total rendered blocks-high of the model (bounds), for sanity checks.
    float height_blocks() const;

    // Parses a .geo.json file. Returns nullptr + reason on failure.
    static std::unique_ptr<GeoModel> load_from_file(const std::string& path,
                                                    std::string* error = nullptr);

    // Classifies a bone name ("head", "leftArm", "leg3", ...) to an anim role.
    static GeoAnim anim_from_name(const std::string& name, float pivot_x,
                                  float pivot_z, int leg_count_hint = 0);
};

} // namespace mc
