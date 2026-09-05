// Tests for the custom Blockbench content pipeline: GeoModel parsing of both
// supported formats (Bedrock .geo.json and native .bbmodel projects, incl.
// per-face UVs, cube rotations and embedded textures), the MobRegistry
// species discovery (directory scan + sidecar .mob.json), and the spawner's
// custom-species wiring.

#include <gtest/gtest.h>
#include "gameplay/entity.hpp"
#include "gameplay/quest.hpp"
#include "gameplay/spawning.hpp"
#include "renderer/geo_model.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace mc {

namespace {

// Minimal valid Bedrock geometry: body + head with box UVs.
const char* kGeoBoxUv = R"JSON({
  "minecraft:geometry": [{
    "description": {"texture_width": 32, "texture_height": 32},
    "bones": [
      {"name": "body", "pivot": [0, 0, 0],
       "cubes": [{"origin": [-4, 0, -2], "size": [8, 12, 4], "uv": [0, 0]}]},
      {"name": "head", "parent": "body", "pivot": [0, 12, 0],
       "cubes": [{"origin": [-4, 12, -4], "size": [8, 8, 8], "uv": [0, 16]}]}
    ]
  }]
})JSON";

// Per-face UV cube + a rotated cube + a bone with Y rest rotation.
const char* kGeoPerFace = R"JSON({
  "minecraft:geometry": [{
    "description": {"texture_width": 64, "texture_height": 64},
    "bones": [
      {"name": "body", "pivot": [0, 0, 0],
       "cubes": [
         {"origin": [0, 0, 0], "size": [4, 4, 4],
          "uv": {"north": [0, 0], "south": [8, 8],
                 "up": {"uv": [16, 16], "uv_size": [4, 4]}}},
         {"origin": [-2, 0, -2], "size": [4, 8, 4], "uv": [0, 0],
          "rotation": {"axis": "y", "angle": 45}, "pivot": [0, 0, 0]}
       ]},
      {"name": "neck", "pivot": [0, 4, 0], "rotation": {"y": 30}}
    ]
  }]
})JSON";

// Native Blockbench project: one group with a rotated per-face element, a
// nested empty group, and a 4x4 embedded PNG texture.
const char* kBbmodel = R"JSON({
  "meta": {"format_version": "4.5", "model_format": "free"},
  "name": "wolf",
  "resolution": {"width": 64, "height": 64},
  "elements": [
    {"name": "cube", "uuid": "e1", "from": [4, 0, 4], "to": [12, 8, 12],
     "rotation": {"axis": "z", "angle": 22.5, "origin": [8, 0, 8]},
     "inflate": 0.25,
     "faces": {
       "north": {"uv": [0, 0, 8, 8], "texture": 0},
       "south": {"uv": [8, 0, 16, 8], "texture": 0},
       "up": {"uv": [16, 0, 24, 8], "texture": 0},
       "down": {"uv": [24, 0, 32, 8], "texture": 0},
       "west": {"uv": [32, 0, 40, 8], "texture": 0},
       "east": {"uv": [40, 0, 48, 8], "texture": 0}
     }}
  ],
  "outliner": [
    {"name": "head", "origin": [8, 8, 8], "children": ["e1"]},
    {"name": "body", "origin": [0, 0, 0],
     "children": [{"name": "legL", "origin": [8, 0, 8], "children": []}]}
  ],
  "textures": [{"name": "tex", "id": "0",
    "source": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFUlEQVR4nGP8z8DwnwEJMCFziBMAAIPRAgYEvCRHAAAAAElFTkSuQmCC",
    "width": 4, "height": 4}]
})JSON";

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good()) << path.string();
    out << content;
}

} // namespace

// ---------------------------------------------------------------- GeoModel

TEST(GeoModelTest, ParsesBedrockBoxUvGeometry) {
    std::string err;
    auto model = GeoModel::load_from_memory(kGeoBoxUv, &err);
    ASSERT_NE(model, nullptr) << err;
    ASSERT_EQ(model->bones.size(), 2u);
    EXPECT_EQ(model->bones[0].name, "body");
    EXPECT_EQ(model->bones[1].name, "head");
    EXPECT_EQ(model->bones[1].parent, 0); // head.parent -> body
    EXPECT_FLOAT_EQ(model->tex_w, 32.0f);
    EXPECT_FLOAT_EQ(model->tex_h, 32.0f);
    EXPECT_FALSE(model->bones[0].cubes[0].per_face);
    EXPECT_FLOAT_EQ(model->bones[0].cubes[0].origin.x, -4.0f);
    EXPECT_FLOAT_EQ(model->bones[0].cubes[0].size.y, 12.0f);
}

TEST(GeoModelTest, ParsesPerFaceUvsAndRotations) {
    std::string err;
    auto model = GeoModel::load_from_memory(kGeoPerFace, &err);
    ASSERT_NE(model, nullptr) << err;
    ASSERT_EQ(model->bones.size(), 2u);

    const GeoCube& uv_cube = model->bones[0].cubes[0];
    ASSERT_TRUE(uv_cube.per_face);
    // north is face 1: array form -> origin (0,0), default size (w,h) = (4,4).
    EXPECT_FLOAT_EQ(uv_cube.faces[1].u, 0.0f);
    EXPECT_FLOAT_EQ(uv_cube.faces[1].v, 0.0f);
    EXPECT_FLOAT_EQ(uv_cube.faces[1].w, 4.0f);
    EXPECT_FLOAT_EQ(uv_cube.faces[1].h, 4.0f);
    // south is face 0.
    EXPECT_FLOAT_EQ(uv_cube.faces[0].u, 8.0f);
    EXPECT_FLOAT_EQ(uv_cube.faces[0].v, 8.0f);
    // up is face 3: explicit uv_size.
    EXPECT_FLOAT_EQ(uv_cube.faces[3].u, 16.0f);
    EXPECT_FLOAT_EQ(uv_cube.faces[3].w, 4.0f);
    EXPECT_FLOAT_EQ(uv_cube.faces[3].h, 4.0f);

    const GeoCube& rotated = model->bones[0].cubes[1];
    EXPECT_TRUE(rotated.rotated);
    EXPECT_FLOAT_EQ(rotated.rot_deg.y, 45.0f);
    EXPECT_FLOAT_EQ(rotated.rot_pivot.y, 0.0f);

    EXPECT_FLOAT_EQ(model->bones[1].base_rot_deg.y, 30.0f);
}

TEST(GeoModelTest, ParsesBbmodelProjectWithEmbeddedTexture) {
    std::string err;
    auto model = GeoModel::load_from_memory(kBbmodel, &err);
    ASSERT_NE(model, nullptr) << err;

    // Root + head + body + legL.
    ASSERT_EQ(model->bones.size(), 4u);
    EXPECT_EQ(model->bones[0].name, "root");
    EXPECT_EQ(model->bones[1].name, "head");
    EXPECT_EQ(model->bones[1].parent, 0);
    EXPECT_EQ(model->bones[2].name, "body");
    EXPECT_EQ(model->bones[3].name, "legL");
    EXPECT_EQ(model->bones[3].parent, 2); // legL nested under body

    ASSERT_EQ(model->bones[1].cubes.size(), 1u);
    const GeoCube& cube = model->bones[1].cubes[0];
    EXPECT_FLOAT_EQ(cube.origin.x, 4.0f);
    EXPECT_FLOAT_EQ(cube.size.x, 8.0f);
    EXPECT_FLOAT_EQ(cube.size.y, 8.0f);
    EXPECT_FLOAT_EQ(cube.inflate, 0.25f);
    EXPECT_TRUE(cube.per_face);
    EXPECT_FLOAT_EQ(cube.faces[0].u, 8.0f);  // south
    EXPECT_FLOAT_EQ(cube.faces[0].v, 0.0f);
    EXPECT_FLOAT_EQ(cube.faces[0].w, 8.0f);
    EXPECT_FLOAT_EQ(cube.faces[0].h, 8.0f);
    EXPECT_TRUE(cube.rotated);
    EXPECT_FLOAT_EQ(cube.rot_deg.z, 22.5f);
    EXPECT_FLOAT_EQ(cube.rot_pivot.x, 8.0f);
    EXPECT_FLOAT_EQ(cube.rot_pivot.y, 0.0f);

    EXPECT_FLOAT_EQ(model->tex_w, 64.0f);
    EXPECT_FLOAT_EQ(model->tex_h, 64.0f);
    // Embedded texture: PNG signature + IHDR dims (4x4).
    ASSERT_GE(model->embedded_png.size(), 24u);
    EXPECT_EQ(model->embedded_png[0], 0x89);
    EXPECT_EQ(model->embedded_png[1], 'P');
    auto be32 = [&](size_t o) {
        return (model->embedded_png[o] << 24) | (model->embedded_png[o + 1] << 16) |
               (model->embedded_png[o + 2] << 8) | model->embedded_png[o + 3];
    };
    EXPECT_EQ(be32(16), 4u);
    EXPECT_EQ(be32(20), 4u);
}

TEST(GeoModelTest, RejectsGarbage) {
    std::string err;
    EXPECT_EQ(GeoModel::load_from_memory("{not json", &err), nullptr);
    EXPECT_EQ(GeoModel::load_from_memory("{}", &err), nullptr);
    EXPECT_EQ(GeoModel::load_from_memory(R"({"elements": []})", &err), nullptr);
}

TEST(GeoModelTest, ResampleNearestScalesPixels) {
    // 2x1 source: red, blue -> 4x2: each source pixel covers a 2x2 block.
    std::vector<uint8_t> src = {255, 0, 0, 255, 0, 0, 255, 255};
    std::vector<uint8_t> dst;
    resample_rgba_nearest(src.data(), 2, 1, dst, 4, 2);
    ASSERT_EQ(dst.size(), 32u);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint8_t* p = &dst[(y * 4 + x) * 4];
            if (x < 2) {
                EXPECT_EQ(p[0], 255) << x << "," << y;
                EXPECT_EQ(p[2], 0);
            } else {
                EXPECT_EQ(p[0], 0);
                EXPECT_EQ(p[2], 255) << x << "," << y;
            }
        }
    }
    resample_rgba_nearest(nullptr, 0, 0, dst, 4, 4);
    EXPECT_EQ(dst.size(), 64u); // zero-filled, no crash
}

// -------------------------------------------------------------- MobRegistry

class MobRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("minekampf_mobs_test_" + std::to_string(::testing::UnitTest::GetInstance()
                                                            ->random_seed()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override {
        MobRegistry::instance().reset_to_builtin();
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path dir_;
};

TEST_F(MobRegistryTest, DiscoversCustomSpeciesSortedWithSidecars) {
    write_file(dir_ / "b_wolf.geo.json", kGeoBoxUv);
    // Sidecar: hostile wolf that drops iron.
    write_file(dir_ / "b_wolf.mob.json", R"JSON({
      "display_name": "Alpha Wolf", "hostile": true, "health": 30,
      "speed": 0.09, "attack_damage": 5, "scale": 1.4,
      "body_width": 0.9, "body_height": 1.0, "xp": 8,
      "drop_item": "iron_ingot", "drop_min": 1, "drop_max": 2
    })JSON");
    write_file(dir_ / "a_dragon.bbmodel", kBbmodel);
    write_file(dir_ / "zombie.geo.json", kGeoBoxUv);     // builtin name: skipped
    write_file(dir_ / "broken.geo.json", "{ nope");      // invalid: skipped

    std::string err;
    ASSERT_TRUE(MobRegistry::instance().scan_directory(dir_.string(), &err)) << err;

    auto& reg = MobRegistry::instance();
    ASSERT_EQ(reg.size(), 6u); // 4 builtin + dragon + wolf

    const MobSpec* dragon = reg.find("a_dragon");
    ASSERT_NE(dragon, nullptr);
    EXPECT_EQ(dragon->id, 4); // alphabetical: a_dragon first
    EXPECT_FALSE(dragon->hostile);
    EXPECT_FLOAT_EQ(dragon->health, 10.0f); // default
    EXPECT_TRUE(dragon->model_path.size() > 4 &&
                dragon->model_path.compare(dragon->model_path.size() - 8, 8,
                                           ".bbmodel") == 0);
    EXPECT_TRUE(dragon->texture_path.empty()); // embedded texture

    const MobSpec* wolf = reg.find("b_wolf");
    ASSERT_NE(wolf, nullptr);
    EXPECT_EQ(wolf->id, 5);
    EXPECT_TRUE(wolf->hostile);
    EXPECT_EQ(wolf->display_name, "Alpha Wolf");
    EXPECT_FLOAT_EQ(wolf->health, 30.0f);
    EXPECT_FLOAT_EQ(wolf->scale, 1.4f);
    EXPECT_FLOAT_EQ(wolf->body_height, 1.0f);
    EXPECT_EQ(wolf->drop_item, "iron_ingot");
    EXPECT_EQ(wolf->drop_max, 2);
    EXPECT_EQ(wolf->xp_reward, 8);

    // Name lookup is case-insensitive and covers built-ins.
    EXPECT_NE(reg.find("ZOMBIE"), nullptr);
    EXPECT_EQ(reg.find("no_such_species"), nullptr);

    // Custom hostility round-trips through is_hostile().
    EXPECT_FALSE(is_hostile(static_cast<MobType>(dragon->id)));
    EXPECT_TRUE(is_hostile(static_cast<MobType>(wolf->id)));

    // Custom pools are hostility-filtered.
    EXPECT_NE(reg.random_custom(true, 0), nullptr);
    EXPECT_NE(reg.random_custom(false, 0), nullptr);
    EXPECT_EQ(reg.random_custom(true, 0)->id, wolf->id);
    EXPECT_EQ(reg.random_custom(false, 0)->id, dragon->id);
}

TEST_F(MobRegistryTest, ScanMissingDirectoryFailsCleanly) {
    std::string err;
    EXPECT_FALSE(
        MobRegistry::instance().scan_directory((dir_ / "nope").string(), &err));
    EXPECT_NE(err.find("nope"), std::string::npos);
    // Registry falls back to built-ins only.
    EXPECT_EQ(MobRegistry::instance().size(), 4u);
}

// ------------------------------------------------------------------ Spawner

TEST_F(MobRegistryTest, SpawnerAppliesCustomSpecStats) {
    write_file(dir_ / "b_wolf.geo.json", kGeoBoxUv);
    write_file(dir_ / "b_wolf.mob.json", R"JSON({
      "hostile": true, "health": 30, "speed": 0.09, "attack_damage": 5,
      "body_width": 0.9, "body_height": 1.0
    })JSON");
    std::string err;
    ASSERT_TRUE(MobRegistry::instance().scan_directory(dir_.string(), &err)) << err;

    MobRegistry& reg = MobRegistry::instance();
    const MobSpec* wolf = reg.find("b_wolf");
    ASSERT_NE(wolf, nullptr);

    Rng rng(12345);
    MobSpawner spawner;
    Mob m = spawner.spawn_forced(MobCategory::Creature,
                                 BlockPos(0, 70, 0), rng,
                                 static_cast<MobType>(wolf->id));
    EXPECT_FALSE(m.is_natural_spawn); // spawn_forced contract
    EXPECT_EQ(m.type, static_cast<MobType>(wolf->id));
    EXPECT_FLOAT_EQ(m.max_health, 30.0f);
    EXPECT_FLOAT_EQ(m.health, 30.0f);
    EXPECT_FLOAT_EQ(m.speed, 0.09f);
    EXPECT_FLOAT_EQ(m.attack_damage, 5.0f);
    EXPECT_FLOAT_EQ(m.body_width, 0.9f);
    EXPECT_FLOAT_EQ(m.body_height, 1.0f);
    // Hostile customs are categorized as monsters regardless of the call's
    // nominal category.
    EXPECT_EQ(static_cast<MobCategory>(m.spawn_category), MobCategory::Monster);

    // Hitbox honors the custom body size.
    AABB box = m.aabb();
    EXPECT_FLOAT_EQ(box.max.x - box.min.x, 0.9f);
    EXPECT_FLOAT_EQ(box.max.y - box.min.y, 1.0f);
}

TEST(QuestNameLookupTest, RegistryCoversBuiltinsAndRejectsUnknown) {
    MobType out = MobType::Zombie;
    EXPECT_TRUE(quest::mob_from_name("zombie", out));
    EXPECT_EQ(out, MobType::Zombie);
    EXPECT_TRUE(quest::mob_from_name("PiG", out));
    EXPECT_EQ(out, MobType::Pig);
    EXPECT_FALSE(quest::mob_from_name("definitely_not_a_mob", out));
}

} // namespace mc
