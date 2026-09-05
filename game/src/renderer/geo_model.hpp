#pragma once

// Blockbench model pipeline: runtime loader for the two formats Blockbench
// produces:
//   * Bedrock entity geometry (.geo.json — "Export Bedrock Geometry"), and
//   * native Blockbench project files (.bbmodel — File -> Save As), including
//     the texture embedded as base64.
//
// Workflow for artists (human or AI): model the entity in Blockbench
// (blockbench.net), then EITHER
//   1. File -> Export -> "Bedrock Geometry" -> assets/models/mobs/<name>.geo.json
//      plus a box-UV texture <name>.png next to it, OR
//   2. drop the saved project file itself: assets/models/mobs/<name>.bbmodel
//      (texture embedded in the file, no extra PNG needed).
// Files are discovered by MobRegistry at startup; a missing/corrupt file
// falls back to the procedural rig. Units: 1/16 of a block, Y up, feet at
// y = 0.

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

// Nearest-neighbour RGBA resampler (model-pipeline utility, no GL). Used to
// normalize mob textures of arbitrary sizes into one GL_TEXTURE_2D_ARRAY
// (all layers share dims). Exposed for unit tests. `dst` receives
// dw*dh*4 bytes.
void resample_rgba_nearest(const uint8_t* src, int src_w, int src_h,
                           std::vector<uint8_t>& dst, int dst_w, int dst_h);

// Automatic animation role derived from the bone name. Custom bones that
// match none of the heuristics stay static (GeoAnim::None).
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

// Rect in texture pixels. Face order everywhere below:
// 0 = south (+Z), 1 = north (-Z), 2 = down (-Y), 3 = up (+Y),
// 4 = west (-X), 5 = east (+X).
struct GeoUvRect {
    float u = 0.0f, v = 0.0f, w = 0.0f, h = 0.0f;
};

struct GeoCube {
    glm::vec3 origin{0.0f};  // cube corner in model units (1/16 block)
    glm::vec3 size{0.0f};    // extents in model units
    glm::vec2 uv{0.0f};      // box-unwrap origin in texture pixels
    bool mirror = false;
    float inflate = 0.0f;    // outward padding in model units

    // Per-face UV mode (bbmodel always; .geo.json when "uv" is an object).
    // When set, `faces` holds explicit rects and `uv` is unused.
    bool per_face = false;
    GeoUvRect faces[6];

    // Optional cube-local rotation, degrees around `rot_pivot` (model units).
    glm::vec3 rot_deg{0.0f};
    glm::vec3 rot_pivot{0.0f};
    bool rotated = false;
};

struct GeoBone {
    std::string name;
    int parent = -1;              // index into GeoModel::bones
    glm::vec3 pivot{0.0f};        // joint position, model units
    glm::vec3 base_rot_deg{0.0f}; // rest pose rotation, degrees (X applied first)
    GeoAnim anim = GeoAnim::None; // derived from the bone name
    std::vector<GeoCube> cubes;
};

// One animation track for a bone: sorted keyframes (time -> value).
// Blocks keyframes carry Euler rotations in degrees or position offsets in
// model units; Molang-expression keyframes are skipped by the parser.
struct GeoBoneTrack {
    struct Key {
        float t = 0.0f;
        glm::vec3 v{0.0f};
    };
    std::vector<Key> rotation;   // degrees (empty = not animated)
    std::vector<Key> position;   // model units (empty = not animated)

    [[nodiscard]] bool has_rotation() const { return !rotation.empty(); }
    [[nodiscard]] bool has_position() const { return !position.empty(); }

    // Linear interpolation between the surrounding keyframes, wrapping t
    // into [0, length] for looping animations. Falls back to `fallback`
    // when the track has no usable keys.
    static glm::vec3 sample(const std::vector<Key>& keys, float t, float length,
                            const glm::vec3& fallback);
};

// One named animation ("idle", "walk", ...) from a Blockbench
// .animation.json export: per-bone rotation/position tracks.
struct GeoAnimation {
    std::string name;
    float length = 1.0f;   // seconds
    bool loop = true;
    std::unordered_map<std::string, GeoBoneTrack> bones;

    // Parses every animation in a .animation.json document. Molang string
    // keyframes are skipped; purely procedural channels stay empty.
    static bool load_from_file(const std::string& path,
                               std::vector<GeoAnimation>& out,
                               std::string* error = nullptr);
    static bool load_from_memory(const std::string& source,
                                 std::vector<GeoAnimation>& out,
                                 std::string* error = nullptr);
};

struct GeoModel {
    std::vector<GeoBone> bones;
    float tex_w = 64.0f;      // declared texture size (for UV normalization)
    float tex_h = 64.0f;
    // PNG bytes for a texture embedded in a .bbmodel project (empty when the
    // texture comes from a sidecar file). The renderer decodes this when no
    // <name>.png sits next to the model.
    std::vector<uint8_t> embedded_png;

    // Total rendered blocks-high of the model (bounds), for sanity checks.
    float height_blocks() const;

    // Parses a .geo.json or .bbmodel file. Returns nullptr + reason on failure.
    static std::unique_ptr<GeoModel> load_from_file(const std::string& path,
                                                    std::string* error = nullptr);

    // Parses raw file contents (same formats; used by tests). For bbmodel the
    // embedded texture is decoded; `path` is only used in error messages.
    static std::unique_ptr<GeoModel> load_from_memory(const std::string& source,
                                                      std::string* error = nullptr);

    // Classifies a bone name ("head", "leftArm", "leg3", ...) to an anim role.
    static GeoAnim anim_from_name(const std::string& name, float pivot_x,
                                  float pivot_z, int leg_count_hint = 0);
};

} // namespace mc
