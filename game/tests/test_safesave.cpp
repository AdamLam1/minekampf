#include <gtest/gtest.h>
#include "save/safe_file.hpp"
#include "save/region_file.hpp"
#include "save/level_storage.hpp"
#include "gameplay/item.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace mc {

namespace fs = std::filesystem;

namespace {

void write_raw(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << data;
}

std::string read_raw(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

// ---- savefs primitives ----

TEST(SafeSaveTest, AtomicWriteRoundTripAndBakRotation) {
    fs::path target = "test_safe_file.dat";
    fs::remove(target);
    fs::remove(fs::path(target.string() + ".bak"));

    ASSERT_TRUE(savefs::write_atomic(target, "version-one", 11));
    std::vector<uint8_t> loaded;
    ASSERT_TRUE(savefs::read_with_fallback(target, loaded));
    EXPECT_EQ(std::string(loaded.begin(), loaded.end()), "version-one");

    // Second write rotates the old content into .bak.
    ASSERT_TRUE(savefs::write_atomic(target, "version-two!", 12));
    loaded.clear();
    ASSERT_TRUE(savefs::read_with_fallback(target, loaded));
    EXPECT_EQ(std::string(loaded.begin(), loaded.end()), "version-two!");
    EXPECT_EQ(read_raw(fs::path(target.string() + ".bak")), "version-one");

    fs::remove(target);
    fs::remove(fs::path(target.string() + ".bak"));
}

TEST(SafeSaveTest, CorruptMainFallsBackToBak) {
    fs::path target = "test_safe_crash.dat";
    fs::remove(target);
    fs::remove(fs::path(target.string() + ".bak"));

    ASSERT_TRUE(savefs::write_atomic(target, "good-old-data", 13));
    ASSERT_TRUE(savefs::write_atomic(target, "newer-data", 10));

    // Simulate a crash mid-write: the main file ends up truncated garbage.
    write_raw(target, "\x03\xFF\xAA");

    // Raw fallback accepts anything readable -> garbage. Real loaders pass a
    // validator (parse check) so the corrupt main is skipped in favor of bak.
    std::vector<uint8_t> raw;
    ASSERT_TRUE(savefs::read_with_fallback(target, raw));
    EXPECT_EQ(std::string(raw.begin(), raw.end()), "\x03\xFF\xAA");

    std::vector<uint8_t> loaded;
    ASSERT_TRUE(savefs::read_with_fallback_validated(
        target, loaded, [](const std::vector<uint8_t>& b) { return b.size() > 5; }));
    EXPECT_EQ(std::string(loaded.begin(), loaded.end()), "good-old-data");

    fs::remove(target);
    fs::remove(fs::path(target.string() + ".bak"));
}

TEST(SafeSaveTest, MissingFileReportsFailure) {
    std::vector<uint8_t> loaded;
    EXPECT_FALSE(savefs::read_with_fallback("test_safe_missing.dat", loaded));
}

// ---- Region files ----

namespace {
NbtTag chunk_tag(int version) {
    NbtTag root = NbtTag::Compound();
    auto& comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
    comp["Version"] = NbtTag(static_cast<int32_t>(version));
    return root;
}

int chunk_version(const std::optional<NbtTag>& tag) {
    if (!tag || tag->type != NbtTagType::Compound) return -1;
    const auto& comp = *std::get<std::unique_ptr<NbtCompound>>(tag->value);
    auto it = comp.find("Version");
    if (it == comp.end()) return -1;
    return static_cast<int>(std::get<int32_t>(it->second.value));
}
} // namespace

TEST(SafeSaveTest, RegionRoundTripAndBakRecovery) {
    fs::path region = "test_safe_region.mca";
    fs::remove(region);
    fs::remove(fs::path(region.string() + ".bak"));

    {
        RegionFile r(region.string());
        r.write_chunk(1, 1, chunk_tag(1));
    }
    {
        RegionFile r(region.string()); // reopen: image reload path
        EXPECT_EQ(chunk_version(r.read_chunk(1, 1)), 1);
        r.write_chunk(1, 1, chunk_tag(2));
    }
    {
        RegionFile r(region.string());
        EXPECT_EQ(chunk_version(r.read_chunk(1, 1)), 2);
        EXPECT_EQ(chunk_version(r.read_chunk(2, 2)), -1); // never written
    }

    // Crash simulation: main region truncated -> .bak (older image, v1) wins.
    write_raw(region, "CORRUPT");
    {
        RegionFile r(region.string());
        EXPECT_EQ(chunk_version(r.read_chunk(1, 1)), 1);
    }

    fs::remove(region);
    fs::remove(fs::path(region.string() + ".bak"));
}

TEST(SafeSaveTest, RegionSurvivesGrowAndSecondChunk) {
    fs::path region = "test_safe_region2.mca";
    fs::remove(region);
    fs::remove(fs::path(region.string() + ".bak"));

    {
        RegionFile r(region.string());
        r.write_chunk(0, 0, chunk_tag(10));
        // Second, larger chunk forces sector reallocation + image growth.
        NbtTag big = chunk_tag(11);
        auto& comp = *std::get<std::unique_ptr<NbtCompound>>(big.value);
        NbtTag blob = NbtTag::List(NbtTagType::Int);
        auto& list = *std::get<std::unique_ptr<NbtList>>(blob.value);
        for (int i = 0; i < 5000; ++i) list.push_back(NbtTag(static_cast<int32_t>(i)));
        comp["Blob"] = std::move(blob);
        r.write_chunk(1, 0, big);
    }
    {
        RegionFile r(region.string());
        EXPECT_EQ(chunk_version(r.read_chunk(0, 0)), 10);
        EXPECT_EQ(chunk_version(r.read_chunk(1, 0)), 11);
    }

    fs::remove(region);
    fs::remove(fs::path(region.string() + ".bak"));
}

// ---- LevelStorage .dat recovery ----

TEST(SafeSaveTest, PlayerDatRoundTripsEnchantments) {
    std::string dir = "test_world_enchants";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        LevelStorage storage(dir);
        Player a;
        // Slot 0: enchanted sword; slot 1: enchanted pickaxe; slot 2: plain.
        ItemStack sword(ITEM_IRON_SWORD, 1);
        sword.enchant_levels = with_enchant_level(0, EnchantType::Sharpness, 3);
        ItemStack pick(ITEM_IRON_PICKAXE, 1);
        pick.enchant_levels = with_enchant_level(0, EnchantType::Efficiency, 4) |
                              with_enchant_level(0, EnchantType::Unbreaking, 1);
        a.inventory.set_slot(0, sword);
        a.inventory.set_slot(1, pick);
        storage.save_player_dat(a);
    }

    LevelStorage storage(dir);
    Player loaded;
    ASSERT_TRUE(storage.load_player_dat(loaded));
    ItemStack sword = loaded.inventory.get_slot(0);
    EXPECT_EQ(sword.item, ITEM_IRON_SWORD);
    EXPECT_EQ(sword.enchant_level_of(EnchantType::Sharpness), 3);
    ItemStack pick = loaded.inventory.get_slot(1);
    EXPECT_EQ(pick.enchant_level_of(EnchantType::Efficiency), 4);
    EXPECT_EQ(pick.enchant_level_of(EnchantType::Unbreaking), 1);

    fs::remove_all(dir);
}

TEST(SafeSaveTest, PlayerDatRecoversFromBakAfterCorruption) {
    std::string dir = "test_world_safesave";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        LevelStorage storage(dir);
        Player a;
        a.health = 5.0f;
        storage.save_player_dat(a);
    }
    {
        LevelStorage storage(dir);
        Player b;
        b.health = 9.0f;
        storage.save_player_dat(b); // rotates A into player.dat.bak
    }

    // Crash mid-save of C: main file corrupt, .bak holds the PREVIOUS
    // version (A) because the rotation moved it there during save B.
    write_raw(fs::path(dir) / "player.dat", "garbage");

    LevelStorage storage(dir);
    Player loaded;
    ASSERT_TRUE(storage.load_player_dat(loaded));
    EXPECT_FLOAT_EQ(loaded.health, 5.0f); // recovered from .bak

    fs::remove_all(dir);
}

TEST(SafeSaveTest, TimeDatRecoversFromBak) {
    std::string dir = "test_world_safetime";
    fs::remove_all(dir);
    fs::create_directories(dir);

    LevelStorage storage(dir);
    storage.save_time_of_day(0.75f); // v1
    storage.save_time_of_day(0.25f); // v2, v1 -> .bak
    write_raw(fs::path(dir) / "time.dat", "x"); // crash

    EXPECT_FLOAT_EQ(storage.load_time_of_day(), 0.75f);

    fs::remove_all(dir);
}

} // namespace mc
