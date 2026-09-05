#include "renderer/geo_model.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <map>

namespace mc {

namespace {

using nlohmann::json;

// Substring helpers for bone-name -> role classification.
bool contains_lower(const std::string& s, const char* sub) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find(sub) != std::string::npos;
}

// Bedrock/Blockbench face keys, indexed like GeoUvRect/GeoCube::faces.
const char* kFaceNames[6] = {"south", "north", "down", "up", "west", "east"};

glm::vec3 vec3_from(const json& j, glm::vec3 fallback = {0.0f, 0.0f, 0.0f}) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// Reads a Blockbench/Bedrock rotation spec, which appears as either
// {"axis": "x", "angle": 45} (single-axis) or {"x":..,"y":..,"z":..} / [x,y,z].
glm::vec3 rotation_deg_from(const json& j, bool* present) {
    *present = j.is_object() || j.is_array();
    if (!*present) return {0.0f, 0.0f, 0.0f};
    glm::vec3 out{0.0f};
    if (j.is_object() && j.contains("axis")) {
        std::string axis = j.value("axis", "x");
        float angle = j.value("angle", 0.0f);
        if (axis == "x") out.x = angle;
        else if (axis == "y") out.y = angle;
        else if (axis == "z") out.z = angle;
        return out;
    }
    if (j.is_object()) {
        out.x = j.value("x", 0.0f);
        out.y = j.value("y", 0.0f);
        out.z = j.value("z", 0.0f);
        return out;
    }
    return vec3_from(j);
}

// Default per-face rect sizes when a Bedrock per-face UV entry gives only an
// origin: north/south are width x height, east/west depth x height, up/down
// width x depth (matching the box-unwrap layout).
GeoUvRect face_rect(const json& face, const glm::vec3& size, int face_index) {
    GeoUvRect r;
    float face_w, face_h;
    switch (face_index) {
        case 0: case 1: face_w = std::abs(size.x); face_h = std::abs(size.y); break;
        case 2: case 3: face_w = std::abs(size.x); face_h = std::abs(size.z); break;
        default:        face_w = std::abs(size.z); face_h = std::abs(size.y); break;
    }
    if (face.is_array() && face.size() >= 2) {
        r.u = face[0].get<float>();
        r.v = face[1].get<float>();
        r.w = face_w;
        r.h = face_h;
        return r;
    }
    if (face.is_object()) {
        const json* uv = nullptr;
        if (face.contains("uv")) uv = &face["uv"];
        if (uv && uv->is_array() && uv->size() >= 2) {
            r.u = (*uv)[0].get<float>();
            r.v = (*uv)[1].get<float>();
        }
        const json* uv_size = nullptr;
        if (face.contains("uv_size")) uv_size = &face["uv_size"];
        if (uv_size && uv_size->is_array() && uv_size->size() >= 2) {
            r.w = (*uv_size)[0].get<float>();
            r.h = (*uv_size)[1].get<float>();
        } else {
            r.w = face_w;
            r.h = face_h;
        }
        return r;
    }
    r.w = face_w;
    r.h = face_h;
    return r;
}

// ------------------------------------------------------------------ base64

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool decode_base64(const std::string& in, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(in.size() / 4 * 3);
    int val = 0, bits = 0;
    for (char c : in) {
        if (c == '=') break;
        int v = base64_value(c);
        if (v < 0) continue; // skip whitespace/newlines
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
        }
    }
    return !out.empty();
}

// Extracts the PNG payload from a "data:image/png;base64,..." URI.
bool decode_data_uri(const std::string& uri, std::vector<uint8_t>& png) {
    const std::string marker = "base64,";
    size_t pos = uri.find(marker);
    if (pos == std::string::npos) return false;
    return decode_base64(uri.substr(pos + marker.size()), png);
}

// ------------------------------------------------------------------ bbmodel

// Parent-agnostic JSON value type for outliner children: either a nested
// group object or an element uuid string.
void collect_bb_elements(const json& outliner, const std::function<void(const json&)>& visit) {
    if (!outliner.is_array()) return;
    for (const auto& node : outliner) {
        if (node.is_string()) {
            visit(node);
        } else if (node.is_object() && node.contains("children")) {
            collect_bb_elements(node["children"], visit);
        }
    }
}

std::unique_ptr<GeoModel> load_bbmodel(const json& j, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return nullptr;
    };
    if (!j.contains("elements") || !j["elements"].is_array() || j["elements"].empty())
        return fail("bbmodel has no elements");

    auto model = std::make_unique<GeoModel>();

    if (j.contains("resolution") && j["resolution"].is_object()) {
        model->tex_w = j["resolution"].value("width", 0.0f);
        model->tex_h = j["resolution"].value("height", 0.0f);
    }

    // Embedded textures: decode the first base64 data-URI texture so the
    // renderer can upload it when no sidecar .png exists.
    if (j.contains("textures") && j["textures"].is_array()) {
        for (const auto& t : j["textures"]) {
            if (!t.is_object()) continue;
            std::string source = t.value("source", "");
            std::vector<uint8_t> png;
            if (source.rfind("data:image/", 0) == 0 && decode_data_uri(source, png)) {
                model->embedded_png = std::move(png);
                break;
            }
        }
    }
    if ((model->tex_w <= 0.0f || model->tex_h <= 0.0f) && !model->embedded_png.empty()) {
        // PNG IHDR: width/height are big-endian uint32 at byte offsets 16/20.
        const auto& p = model->embedded_png;
        if (p.size() >= 24) {
            auto be32 = [&](size_t o) {
                return static_cast<float>((p[o] << 24) | (p[o + 1] << 16) |
                                          (p[o + 2] << 8) | p[o + 3]);
            };
            if (model->tex_w <= 0.0f) model->tex_w = be32(16);
            if (model->tex_h <= 0.0f) model->tex_h = be32(20);
        }
    }
    if (model->tex_w <= 0.0f || model->tex_h <= 0.0f) {
        model->tex_w = 64.0f;
        model->tex_h = 64.0f;
    }

    // Index elements by uuid.
    std::map<std::string, const json*> elements;
    for (const auto& e : j["elements"]) {
        if (e.is_object() && e.contains("uuid"))
            elements[e.value("uuid", "")] = &e;
    }

    // Walk the outliner: groups become bones, element uuids attach cubes.
    // Elements outside any group join a synthetic root bone.
    GeoBone root;
    root.name = "root";
    const int root_index = 0;
    model->bones.push_back(std::move(root));

    std::function<void(const json&, int)> walk = [&](const json& node, int parent) {
        if (node.is_string()) {
            auto it = elements.find(node.get<std::string>());
            if (it == elements.end()) return;
            const json& e = *it->second;
            GeoCube cube;
            cube.origin = vec3_from(e.value("from", json::array({0, 0, 0})));
            glm::vec3 to = vec3_from(e.value("to", json::array({0, 0, 0})));
            cube.size = to - cube.origin;
            // Normalize inverted from/to (legal in Blockbench) to min corner.
            for (int a = 0; a < 3; ++a) {
                if (cube.size[a] < 0.0f) {
                    cube.origin[a] = to[a];
                    cube.size[a] = -cube.size[a];
                }
            }
            cube.inflate = e.value("inflate", 0.0f);
            if (e.contains("rotation")) {
                bool present = false;
                cube.rot_deg = rotation_deg_from(e["rotation"], &present);
                cube.rotated = present && glm::length(cube.rot_deg) > 0.0001f;
                // Rotation origin: inside the rotation object (Blockbench),
                // or the element's own origin/pivot field as a fallback.
                if (cube.rotated) {
                    if (e["rotation"].contains("origin"))
                        cube.rot_pivot = vec3_from(e["rotation"]["origin"]);
                    else if (e.contains("origin"))
                        cube.rot_pivot = vec3_from(e["origin"]);
                    else if (e.contains("pivot"))
                        cube.rot_pivot = vec3_from(e["pivot"]);
                }
            }
            if (e.contains("faces") && e["faces"].is_object()) {
                cube.per_face = true;
                for (int f = 0; f < 6; ++f) {
                    if (e["faces"].contains(kFaceNames[f]))
                        cube.faces[f] = face_rect(e["faces"][kFaceNames[f]], cube.size, f);
                }
            }
            model->bones[static_cast<size_t>(parent)].cubes.push_back(std::move(cube));
            return;
        }
        if (!node.is_object() || !node.contains("children")) return;

        GeoBone bone;
        bone.name = node.value("name", "group");
        bone.parent = parent;
        if (node.contains("origin")) bone.pivot = vec3_from(node["origin"]);
        if (node.contains("rotation")) {
            bool present = false;
            bone.base_rot_deg = rotation_deg_from(node["rotation"], &present);
        }
        bone.anim = GeoModel::anim_from_name(bone.name, bone.pivot.x, bone.pivot.z, 0);
        int idx = static_cast<int>(model->bones.size());
        model->bones.push_back(std::move(bone));
        for (const auto& child : node["children"]) walk(child, idx);
    };

    if (j.contains("outliner") && j["outliner"].is_array()) {
        for (const auto& node : j["outliner"]) walk(node, root_index);
    }

    // Elements not referenced by the outliner (rare, but Blockbench allows a
    // flat elements list with an empty outliner): attach them to the root.
    std::function<void(const json&)> visit_flat = [&](const json& node) {
        if (!node.is_string()) return;
        auto it = elements.find(node.get<std::string>());
        if (it != elements.end()) elements.erase(it);
    };
    if (j.contains("outliner")) collect_bb_elements(j["outliner"], visit_flat);
    for (const auto& [uuid, e] : elements) {
        GeoCube cube;
        cube.origin = vec3_from(e->value("from", json::array({0, 0, 0})));
        glm::vec3 to = vec3_from(e->value("to", json::array({0, 0, 0})));
        cube.size = to - cube.origin;
        cube.inflate = e->value("inflate", 0.0f);
        if (e->contains("faces") && (*e)["faces"].is_object()) {
            cube.per_face = true;
            for (int f = 0; f < 6; ++f) {
                if ((*e)["faces"].contains(kFaceNames[f]))
                    cube.faces[f] = face_rect((*e)["faces"][kFaceNames[f]], cube.size, f);
            }
        }
        model->bones[root_index].cubes.push_back(std::move(cube));
    }

    if (model->bones.size() <= 1) return fail("bbmodel has no bones");
    return model;
}

std::unique_ptr<GeoModel> load_geo_json(const json& j, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return nullptr;
    };
    auto model = std::make_unique<GeoModel>();

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

        const json& bones = geo["bones"];
        // Bones may reference parents defined later; do two passes.
        std::vector<std::string> parent_names;
        for (const auto& b : bones) parent_names.push_back(b.value("name", ""));

        int leg_hint = 0;
        for (const auto& b : bones) {
            if (b.contains("name") && contains_lower(b.value("name", ""), "leg")) ++leg_hint;
        }

        for (const auto& b : bones) {
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

            if (b.contains("pivot")) bone.pivot = vec3_from(b["pivot"]);
            if (b.contains("rotation")) {
                bool present = false;
                bone.base_rot_deg = rotation_deg_from(b["rotation"], &present);
            }

            if (b.contains("cubes") && b["cubes"].is_array()) {
                for (const auto& c : b["cubes"]) {
                    GeoCube cube;
                    if (c.contains("origin")) cube.origin = vec3_from(c["origin"]);
                    if (c.contains("size")) cube.size = vec3_from(c["size"]);
                    if (c.contains("uv")) {
                        if (c["uv"].is_array() && c["uv"].size() >= 2) {
                            cube.uv = {c["uv"][0].get<float>(), c["uv"][1].get<float>()};
                        } else if (c["uv"].is_object()) {
                            // Per-face UV: every face gets an explicit rect.
                            cube.per_face = true;
                            for (int f = 0; f < 6; ++f) {
                                if (c["uv"].contains(kFaceNames[f]))
                                    cube.faces[f] =
                                        face_rect(c["uv"][kFaceNames[f]], cube.size, f);
                            }
                        }
                    }
                    cube.mirror = c.value("mirror", false);
                    cube.inflate = c.value("inflate", 0.0f);
                    if (c.contains("rotation")) {
                        bool present = false;
                        cube.rot_deg = rotation_deg_from(c["rotation"], &present);
                        cube.rotated = present && glm::length(cube.rot_deg) > 0.0001f;
                        if (cube.rotated) {
                            if (c["rotation"].contains("origin"))
                                cube.rot_pivot = vec3_from(c["rotation"]["origin"]);
                            else if (c.contains("pivot"))
                                cube.rot_pivot = vec3_from(c["pivot"]);
                        }
                    }
                    bone.cubes.push_back(std::move(cube));
                }
            }

            bone.anim = GeoModel::anim_from_name(bone.name, bone.pivot.x, bone.pivot.z,
                                                 leg_hint);
            model->bones.push_back(std::move(bone));
        }
    } else {
        return fail("unsupported format (expected minecraft:geometry or a .bbmodel "
                    "project — export Bedrock Geometry or save the project from "
                    "Blockbench)");
    }

    if (model->bones.empty()) return fail("no bones parsed");
    return model;
}

} // namespace

void resample_rgba_nearest(const uint8_t* src, int src_w, int src_h,
                           std::vector<uint8_t>& dst, int dst_w, int dst_h) {
    dst.assign(static_cast<size_t>(dst_w) * dst_h * 4, 0);
    if (!src || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    for (int y = 0; y < dst_h; ++y) {
        int sy = std::min(src_h - 1, y * src_h / dst_h);
        for (int x = 0; x < dst_w; ++x) {
            int sx = std::min(src_w - 1, x * src_w / dst_w);
            const uint8_t* s = src + (static_cast<size_t>(sy) * src_w + sx) * 4;
            uint8_t* d = &dst[(static_cast<size_t>(y) * dst_w + x) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    }
}

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

std::unique_ptr<GeoModel> GeoModel::load_from_memory(const std::string& source,
                                                     std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return nullptr;
    };
    json j;
    try {
        j = json::parse(source);
    } catch (const std::exception& e) {
        return fail(std::string("JSON parse: ") + e.what());
    }
    if (j.contains("elements") && j.contains("meta")) return load_bbmodel(j, error);
    return load_geo_json(j, error);
}

std::unique_ptr<GeoModel> GeoModel::load_from_file(const std::string& path,
                                                   std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return nullptr;
    }
    std::string source((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    return load_from_memory(source, error);
}

} // namespace mc
