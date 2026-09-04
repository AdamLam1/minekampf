#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <memory>
#include <stdexcept>
#include <span>

namespace mc {

enum class NbtTagType : uint8_t {
    End = 0,
    Byte = 1,
    Short = 2,
    Int = 3,
    Long = 4,
    Float = 5,
    Double = 6,
    ByteArray = 7,
    String = 8,
    List = 9,
    Compound = 10,
    IntArray = 11,
    LongArray = 12
};

struct NbtTag;

using NbtCompound = std::unordered_map<std::string, NbtTag>;
using NbtList = std::vector<NbtTag>;
using NbtByteArray = std::vector<int8_t>;
using NbtIntArray = std::vector<int32_t>;
using NbtLongArray = std::vector<int64_t>;

struct NbtTag {
    NbtTagType type;
    std::variant<
        std::monostate,               // 0: End
        int8_t,                       // 1: Byte
        int16_t,                      // 2: Short
        int32_t,                      // 3: Int
        int64_t,                      // 4: Long
        float,                        // 5: Float
        double,                       // 6: Double
        NbtByteArray,                 // 7: ByteArray
        std::string,                  // 8: String
        std::unique_ptr<NbtList>,     // 9: List (heap allocated for recursion)
        std::unique_ptr<NbtCompound>, // 10: Compound (heap allocated for recursion)
        NbtIntArray,                  // 11: IntArray
        NbtLongArray                  // 12: LongArray
    > value;

    // Type of elements in the list if this is a List tag
    NbtTagType list_type = NbtTagType::End;

    // Constructors for ease of use
    NbtTag() : type(NbtTagType::End), value(std::monostate{}) {}
    NbtTag(int8_t v) : type(NbtTagType::Byte), value(v) {}
    NbtTag(int16_t v) : type(NbtTagType::Short), value(v) {}
    NbtTag(int32_t v) : type(NbtTagType::Int), value(v) {}
    NbtTag(int64_t v) : type(NbtTagType::Long), value(v) {}
    NbtTag(float v) : type(NbtTagType::Float), value(v) {}
    NbtTag(double v) : type(NbtTagType::Double), value(v) {}
    NbtTag(const std::string& v) : type(NbtTagType::String), value(v) {}
    NbtTag(const char* v) : type(NbtTagType::String), value(std::string(v)) {}
    NbtTag(NbtByteArray v) : type(NbtTagType::ByteArray), value(std::move(v)) {}
    NbtTag(NbtIntArray v) : type(NbtTagType::IntArray), value(std::move(v)) {}
    NbtTag(NbtLongArray v) : type(NbtTagType::LongArray), value(std::move(v)) {}

    // Special constructors for List and Compound
    static NbtTag List(NbtTagType l_type) {
        NbtTag tag;
        tag.type = NbtTagType::List;
        tag.value = std::make_unique<NbtList>();
        tag.list_type = l_type;
        return tag;
    }
    
    static NbtTag Compound() {
        NbtTag tag;
        tag.type = NbtTagType::Compound;
        tag.value = std::make_unique<NbtCompound>();
        return tag;
    }

    // Copy semantics for unique_ptr
    NbtTag(const NbtTag& other);
    NbtTag& operator=(const NbtTag& other);

    // Move semantics
    NbtTag(NbtTag&&) noexcept = default;
    NbtTag& operator=(NbtTag&&) noexcept = default;
};

class NbtSerializer {
public:
    static std::vector<uint8_t> serialize(const std::string& root_name, const NbtTag& tag);
    static std::pair<std::string, NbtTag> deserialize(std::span<const uint8_t> data);
};

} // namespace mc
