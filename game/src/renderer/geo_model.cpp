#include "renderer/geo_model.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace mc {

namespace {

// Substring helpers for bone-name -> role classification.
bool contains_lower(const std::string& s, const char* sub) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find(sub) != std::string::npos;
}

} // namespace

float GeoModel::height_blocks() const {
    float min_y = 0.0f, max_y = 0.0f;
    for (const auto& bone : bones) {
        for (const auto& cube : bone.cubes) {
            min_y = std::min(min_y, (bone.pivot.y + cube.origin.y) * (1.0f / 16.0f));
            max_y = std::max(max_y,
                             (bone.pivot.y + cube.origin.y + cube.size.y) * (1.0f / 16.0f));
        }
    }
    return max_y - min_y;
}

GeoAnim GeoModel::anim_from_name(const std::string& name, float pivot_x, float pivot_z,
                                 int /*leg_count_hint*/) {
    const bool is_left = contains_lower(name, "left") || contains_lower(name, "_l") ||
                         contains_lower(name, "l_");
    const bool is_right = contains_lower(name, "right") || contains_lower(name, "_r") ||
                          contains_lower(name, "r_");
    const bool front = pivot_z > 0.0f;

    if (contains_lower(name, "head")) return GeoAnim::Head;
    if (contains_lower(name, "arm")) {
        // Default convention: right limb at negative X (Bedrock mirrors).
        bool left = is_left || (!is_right && pivot_x > 0.0f);
        return left ? GeoAnim::ArmLeft : GeoAnim::ArmRight;
    }
    if (contains_lower(name, "leg") || contains_lower(name, "foot")) {
        bool left = is_left || (!is_right && pivot_x > 0.0f);
        // Quadruped legs: disambiguate front/back by Z, then mirror gait.
        if (contains_lower(name, "front") || contains_lower(name, "back") ||
            contains_lower(name, "hind") || std::fabs(pivot_z) > 0.5f) {
            if (left) return front ? GeoAnim::LegFL : GeoAnim::LegBL;
            return front ? GeoAnim::LegFR : GeoAnim::LegBR;
        }
        return left ? GeoAnim::LegLeft : GeoAnim::LegRight;
    }
    return GeoAnim::None;
}

std::unique_ptr<GeoModel> GeoModel::load_from_file(const std::string& path,
                                                   std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return nullptr;
    };

    std::ifstream in(path);
    if (!in) return fail("cannot open " + path);

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        return fail(std::string("JSON parse: ") + e.what());
    }

    auto model = std::make_unique<GeoModel>();

    const nlohmann::json* bones = nullptr;
    if (j.contains("minecraft:geometry")) {
        // Bedrock geometry (Blockbench "Export Bedrock Geometry").
        const auto& geoms = j["minecraft:geometry"];
        if (!geoms.is_array() || geoms.empty()) return fail("empty geometry array");
        const auto& geo = geoms[0];
        if (geo.contains("description")) {
            const auto& d = geo["description"];
            model->tex_w = d.value("texture_width", 64.0f);
            model->tex_h = d.value("texture_height", 64.0f);
        }
        if (!geo.contains("bones") || !geo["bones"].is_array())
            return fail("geometry has no bones");
        bones = &geo["bones"];
    } else {
        return fail("unsupported format (expected minecraft:geometry — export "
                    "Bedrock Geometry from Blockbench)");
    }

    // Bones may reference parents defined later; do two passes.
    std::vector<std::string> parent_names;
    for (const auto& b : *bones) {
        parent_names.push_back(b.value("name", ""));
    }

    int leg_hint = 0;
    for (const auto& b : *bones) {
        if (b.contains("name") && contains_lower(b.value("name", ""), "leg")) ++leg_hint;
    }

    for (const auto& b : *bones) {
        GeoBone bone;
        bone.name = b.value("name", "");

        if (b.contains("parent")) {
            std::string pname = b.value("parent", "");
            for (size_t i = 0; i < parent_names.size(); ++i) {
                if (parent_names[i] == pname) {
                    bone.parent = static_cast<int>(i);
                    break;
                }
            }
        }

        auto read_vec3 = [&](const char* key, glm::vec3& out) {
            if (!b.contains(key) || !b[key].is_array() || b[key].size() < 3) return;
            out = {b[key][0].get<float>(), b[key][1].get<float>(), b[key][2].get<float>()};
        };
        read_vec3("pivot", bone.pivot);
        glm::vec3 rot_deg{0.0f};
        read_vec3("rotation", rot_deg);
        bone.base_rot_x = rot_deg.x * 3.14159265f / 180.0f;

        if (b.contains("cubes") && b["cubes"].is_array()) {
            for (const auto& c : b["cubes"]) {
                GeoCube cube;
                if (c.contains("origin") && c["origin"].is_array() && c["origin"].size() >= 3) {
                    cube.origin = {c["origin"][0].get<float>(), c["origin"][1].get<float>(),
                                   c["origin"][2].get<float>()};
                }
                if (c.contains("size") && c["size"].is_array() && c["size"].size() >= 3) {
                    cube.size = {c["size"][0].get<float>(), c["size"][1].get<float>(),
                                 c["size"][2].get<float>()};
                }
                if (c.contains("uv")) {
                    if (c["uv"].is_array() && c["uv"].size() >= 2) {
                        cube.uv = {c["uv"][0].get<float>(), c["uv"][1].get<float>()};
                    } else if (c["uv"].is_object() && c["uv"].contains("north")) {
                        // Per-face UV: derive the box origin from the north face.
                        auto& nf = c["uv"]["north"];
                        if (nf.is_array() && nf.size() >= 2) {
                            cube.uv = {nf[0].get<float>(), nf[1].get<float>()};
                        }
                    }
                }
                cube.mirror = c.value("mirror", false);
                cube.inflate = c.value("inflate", 0.0f);
                bone.cubes.push_back(cube);
            }
        }

        bone.anim = anim_from_name(bone.name, bone.pivot.x, bone.pivot.z, leg_hint);
        model->bones.push_back(std::move(bone));
    }

    if (model->bones.empty()) return fail("no bones parsed");
    return model;
}

} // namespace mc
