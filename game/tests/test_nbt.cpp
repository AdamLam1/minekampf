#include <gtest/gtest.h>
#include "save/nbt.hpp"

namespace mc {

TEST(NbtTest, SerializeDeserializeBasic) {
    NbtTag root = NbtTag::Compound();
    auto& comp = *std::get<std::unique_ptr<NbtCompound>>(root.value);
    
    comp["ByteTest"] = NbtTag((int8_t)127);
    comp["ShortTest"] = NbtTag((int16_t)32767);
    comp["IntTest"] = NbtTag((int32_t)2147483647);
    comp["LongTest"] = NbtTag((int64_t)9223372036854775807LL);
    comp["FloatTest"] = NbtTag(0.49823147058486938f);
    comp["DoubleTest"] = NbtTag(0.49312871321823148);
    comp["StringTest"] = NbtTag("HELLO WORLD THIS IS A TEST STRING ÅÄÖ!");
    
    NbtByteArray ba = {1, 2, 3, 4, 5, -128};
    comp["ByteArrayTest"] = NbtTag(ba);
    
    NbtTag list = NbtTag::List(NbtTagType::Int);
    auto& l = *std::get<std::unique_ptr<NbtList>>(list.value);
    l.push_back(NbtTag((int32_t)11));
    l.push_back(NbtTag((int32_t)22));
    comp["ListTest"] = std::move(list);
    
    std::vector<uint8_t> bytes = NbtSerializer::serialize("hello world", root);
    
    auto [name, deserialized] = NbtSerializer::deserialize(bytes);
    
    EXPECT_EQ(name, "hello world");
    EXPECT_EQ(deserialized.type, NbtTagType::Compound);
    
    auto& d_comp = *std::get<std::unique_ptr<NbtCompound>>(deserialized.value);
    
    EXPECT_EQ(std::get<int8_t>(d_comp["ByteTest"].value), 127);
    EXPECT_EQ(std::get<int16_t>(d_comp["ShortTest"].value), 32767);
    EXPECT_EQ(std::get<int32_t>(d_comp["IntTest"].value), 2147483647);
    EXPECT_EQ(std::get<int64_t>(d_comp["LongTest"].value), 9223372036854775807LL);
    EXPECT_FLOAT_EQ(std::get<float>(d_comp["FloatTest"].value), 0.49823147058486938f);
    EXPECT_DOUBLE_EQ(std::get<double>(d_comp["DoubleTest"].value), 0.49312871321823148);
    EXPECT_EQ(std::get<std::string>(d_comp["StringTest"].value), "HELLO WORLD THIS IS A TEST STRING ÅÄÖ!");
    
    const auto& d_ba = std::get<NbtByteArray>(d_comp["ByteArrayTest"].value);
    EXPECT_EQ(d_ba.size(), 6);
    EXPECT_EQ(d_ba[5], -128);
    
    const auto& d_list = *std::get<std::unique_ptr<NbtList>>(d_comp["ListTest"].value);
    EXPECT_EQ(d_list.size(), 2);
    EXPECT_EQ(std::get<int32_t>(d_list[0].value), 11);
    EXPECT_EQ(std::get<int32_t>(d_list[1].value), 22);
}

} // namespace mc
