#include <gtest/gtest.h>
#include "save/region_file.hpp"
#include <filesystem>

namespace mc {

TEST(RegionTest, WriteAndReadChunk) {
    std::string test_file = "test_region_0_0.mca";
    
    // Clean up before test
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }
    
    // Create chunk data
    NbtTag chunk_data = NbtTag::Compound();
    auto& comp = *std::get<std::unique_ptr<NbtCompound>>(chunk_data.value);
    
    NbtTag level = NbtTag::Compound();
    auto& level_comp = *std::get<std::unique_ptr<NbtCompound>>(level.value);
    level_comp["xPos"] = NbtTag((int32_t)12);
    level_comp["zPos"] = NbtTag((int32_t)34);
    level_comp["Status"] = NbtTag("full");
    
    comp["Level"] = std::move(level);
    
    // Write chunk
    {
        RegionFile region(test_file);
        region.write_chunk(12, 34, chunk_data);
    } // File closed
    
    // Read chunk
    {
        RegionFile region(test_file);
        auto opt_tag = region.read_chunk(12, 34);
        ASSERT_TRUE(opt_tag.has_value());
        
        const NbtTag& read_tag = opt_tag.value();
        EXPECT_EQ(read_tag.type, NbtTagType::Compound);
        
        const auto& read_comp = *std::get<std::unique_ptr<NbtCompound>>(read_tag.value);
        ASSERT_TRUE(read_comp.contains("Level"));
        
        const auto& read_level = *std::get<std::unique_ptr<NbtCompound>>(read_comp.at("Level").value);
        EXPECT_EQ(std::get<int32_t>(read_level.at("xPos").value), 12);
        EXPECT_EQ(std::get<int32_t>(read_level.at("zPos").value), 34);
        EXPECT_EQ(std::get<std::string>(read_level.at("Status").value), "full");
        
        // Try reading empty chunk
        auto opt_empty = region.read_chunk(0, 0);
        EXPECT_FALSE(opt_empty.has_value());
    }
    
    // Clean up after test
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }
}

} // namespace mc
