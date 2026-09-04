#include "save/nbt.hpp"
#include <cstring>
#include <bit>
#include <array>

namespace mc {

// Endianness utilities (Minecraft NBT uses Big-Endian)
template <typename T>
T swap_endian(T u) {
    if constexpr (std::endian::native == std::endian::big) {
        return u;
    } else {
        auto u8 = std::bit_cast<std::array<uint8_t, sizeof(T)>>(u);
        std::array<uint8_t, sizeof(T)> res;
        for (size_t i = 0; i < sizeof(T); ++i) {
            res[i] = u8[sizeof(T) - 1 - i];
        }
        return std::bit_cast<T>(res);
    }
}

NbtTag::NbtTag(const NbtTag& other) : type(other.type), list_type(other.list_type) {
    if (type == NbtTagType::List) {
        value = std::make_unique<NbtList>(*std::get<std::unique_ptr<NbtList>>(other.value));
    } else if (type == NbtTagType::Compound) {
        value = std::make_unique<NbtCompound>(*std::get<std::unique_ptr<NbtCompound>>(other.value));
    } else {
        // Safe because other types are trivially copyable (or string/vector which have their own copy ctors)
        // We can just copy the variant but we need a switch to avoid moving the unique_ptrs.
        // Actually std::variant handles copy correctly if the contained types do. 
        // But unique_ptr doesn't. We have to do it manually.
        switch(type) {
            case NbtTagType::End: value = std::monostate{}; break;
            case NbtTagType::Byte: value = std::get<int8_t>(other.value); break;
            case NbtTagType::Short: value = std::get<int16_t>(other.value); break;
            case NbtTagType::Int: value = std::get<int32_t>(other.value); break;
            case NbtTagType::Long: value = std::get<int64_t>(other.value); break;
            case NbtTagType::Float: value = std::get<float>(other.value); break;
            case NbtTagType::Double: value = std::get<double>(other.value); break;
            case NbtTagType::ByteArray: value = std::get<NbtByteArray>(other.value); break;
            case NbtTagType::String: value = std::get<std::string>(other.value); break;
            case NbtTagType::IntArray: value = std::get<NbtIntArray>(other.value); break;
            case NbtTagType::LongArray: value = std::get<NbtLongArray>(other.value); break;
            default: break;
        }
    }
}

NbtTag& NbtTag::operator=(const NbtTag& other) {
    if (this == &other) return *this;
    type = other.type;
    list_type = other.list_type;
    if (type == NbtTagType::List) {
        value = std::make_unique<NbtList>(*std::get<std::unique_ptr<NbtList>>(other.value));
    } else if (type == NbtTagType::Compound) {
        value = std::make_unique<NbtCompound>(*std::get<std::unique_ptr<NbtCompound>>(other.value));
    } else {
        switch(type) {
            case NbtTagType::End: value = std::monostate{}; break;
            case NbtTagType::Byte: value = std::get<int8_t>(other.value); break;
            case NbtTagType::Short: value = std::get<int16_t>(other.value); break;
            case NbtTagType::Int: value = std::get<int32_t>(other.value); break;
            case NbtTagType::Long: value = std::get<int64_t>(other.value); break;
            case NbtTagType::Float: value = std::get<float>(other.value); break;
            case NbtTagType::Double: value = std::get<double>(other.value); break;
            case NbtTagType::ByteArray: value = std::get<NbtByteArray>(other.value); break;
            case NbtTagType::String: value = std::get<std::string>(other.value); break;
            case NbtTagType::IntArray: value = std::get<NbtIntArray>(other.value); break;
            case NbtTagType::LongArray: value = std::get<NbtLongArray>(other.value); break;
            default: break;
        }
    }
    return *this;
}

// -------------------------------------------------------------
// Serialization
// -------------------------------------------------------------

class BufferWriter {
public:
    std::vector<uint8_t> buffer;
    void write_byte(int8_t v) { buffer.push_back((uint8_t)v); }
    void write_short(int16_t v) { v = swap_endian(v); uint8_t b[2]; std::memcpy(b, &v, 2); buffer.insert(buffer.end(), b, b+2); }
    void write_int(int32_t v) { v = swap_endian(v); uint8_t b[4]; std::memcpy(b, &v, 4); buffer.insert(buffer.end(), b, b+4); }
    void write_long(int64_t v) { v = swap_endian(v); uint8_t b[8]; std::memcpy(b, &v, 8); buffer.insert(buffer.end(), b, b+8); }
    void write_float(float v) {
        uint32_t i; std::memcpy(&i, &v, 4);
        write_int(i);
    }
    void write_double(double v) {
        uint64_t i; std::memcpy(&i, &v, 8);
        write_long(i);
    }
    void write_string(const std::string& v) {
        write_short((int16_t)v.length());
        buffer.insert(buffer.end(), v.begin(), v.end());
    }
};

static void write_tag_payload(BufferWriter& w, const NbtTag& tag) {
    switch(tag.type) {
        case NbtTagType::End: break;
        case NbtTagType::Byte: w.write_byte(std::get<int8_t>(tag.value)); break;
        case NbtTagType::Short: w.write_short(std::get<int16_t>(tag.value)); break;
        case NbtTagType::Int: w.write_int(std::get<int32_t>(tag.value)); break;
        case NbtTagType::Long: w.write_long(std::get<int64_t>(tag.value)); break;
        case NbtTagType::Float: w.write_float(std::get<float>(tag.value)); break;
        case NbtTagType::Double: w.write_double(std::get<double>(tag.value)); break;
        case NbtTagType::ByteArray: {
            const auto& arr = std::get<NbtByteArray>(tag.value);
            w.write_int((int32_t)arr.size());
            for(auto b : arr) w.write_byte(b);
            break;
        }
        case NbtTagType::String: w.write_string(std::get<std::string>(tag.value)); break;
        case NbtTagType::List: {
            w.write_byte((int8_t)tag.list_type);
            const auto& list = *std::get<std::unique_ptr<NbtList>>(tag.value);
            w.write_int((int32_t)list.size());
            for(const auto& item : list) {
                write_tag_payload(w, item);
            }
            break;
        }
        case NbtTagType::Compound: {
            const auto& comp = *std::get<std::unique_ptr<NbtCompound>>(tag.value);
            for(const auto& [name, item] : comp) {
                w.write_byte((int8_t)item.type);
                w.write_string(name);
                write_tag_payload(w, item);
            }
            w.write_byte((int8_t)NbtTagType::End);
            break;
        }
        case NbtTagType::IntArray: {
            const auto& arr = std::get<NbtIntArray>(tag.value);
            w.write_int((int32_t)arr.size());
            for(auto i : arr) w.write_int(i);
            break;
        }
        case NbtTagType::LongArray: {
            const auto& arr = std::get<NbtLongArray>(tag.value);
            w.write_int((int32_t)arr.size());
            for(auto l : arr) w.write_long(l);
            break;
        }
    }
}

std::vector<uint8_t> NbtSerializer::serialize(const std::string& root_name, const NbtTag& tag) {
    BufferWriter w;
    w.write_byte((int8_t)tag.type);
    if (tag.type != NbtTagType::End) {
        w.write_string(root_name);
        write_tag_payload(w, tag);
    }
    return w.buffer;
}

// -------------------------------------------------------------
// Deserialization
// -------------------------------------------------------------

class BufferReader {
public:
    std::span<const uint8_t> buffer;
    size_t offset = 0;

    int8_t read_byte() {
        if(offset >= buffer.size()) throw std::runtime_error("EOF");
        return (int8_t)buffer[offset++];
    }
    int16_t read_short() {
        if(offset + 2 > buffer.size()) throw std::runtime_error("EOF");
        int16_t v; std::memcpy(&v, &buffer[offset], 2); offset += 2;
        return swap_endian(v);
    }
    int32_t read_int() {
        if(offset + 4 > buffer.size()) throw std::runtime_error("EOF");
        int32_t v; std::memcpy(&v, &buffer[offset], 4); offset += 4;
        return swap_endian(v);
    }
    int64_t read_long() {
        if(offset + 8 > buffer.size()) throw std::runtime_error("EOF");
        int64_t v; std::memcpy(&v, &buffer[offset], 8); offset += 8;
        return swap_endian(v);
    }
    float read_float() {
        uint32_t i = read_int();
        float v; std::memcpy(&v, &i, 4);
        return v;
    }
    double read_double() {
        uint64_t i = read_long();
        double v; std::memcpy(&v, &i, 8);
        return v;
    }
    std::string read_string() {
        int16_t len = read_short();
        if(len < 0 || offset + len > buffer.size()) throw std::runtime_error("EOF");
        std::string s((const char*)&buffer[offset], len);
        offset += len;
        return s;
    }
};

static NbtTag read_tag_payload(BufferReader& r, NbtTagType type) {
    switch(type) {
        case NbtTagType::End: return NbtTag();
        case NbtTagType::Byte: return NbtTag(r.read_byte());
        case NbtTagType::Short: return NbtTag(r.read_short());
        case NbtTagType::Int: return NbtTag(r.read_int());
        case NbtTagType::Long: return NbtTag(r.read_long());
        case NbtTagType::Float: return NbtTag(r.read_float());
        case NbtTagType::Double: return NbtTag(r.read_double());
        case NbtTagType::ByteArray: {
            int32_t len = r.read_int();
            if (len < 0) throw std::runtime_error("Invalid array length");
            NbtByteArray arr(len);
            for(int i=0; i<len; ++i) arr[i] = r.read_byte();
            return NbtTag(std::move(arr));
        }
        case NbtTagType::String: return NbtTag(r.read_string());
        case NbtTagType::List: {
            NbtTagType l_type = (NbtTagType)r.read_byte();
            int32_t len = r.read_int();
            if (len < 0) throw std::runtime_error("Invalid list length");
            NbtTag tag = NbtTag::List(l_type);
            auto& list = *std::get<std::unique_ptr<NbtList>>(tag.value);
            list.reserve(len);
            for(int i=0; i<len; ++i) {
                list.push_back(read_tag_payload(r, l_type));
            }
            return tag;
        }
        case NbtTagType::Compound: {
            NbtTag tag = NbtTag::Compound();
            auto& comp = *std::get<std::unique_ptr<NbtCompound>>(tag.value);
            while(true) {
                NbtTagType child_type = (NbtTagType)r.read_byte();
                if (child_type == NbtTagType::End) break;
                std::string name = r.read_string();
                comp[name] = read_tag_payload(r, child_type);
            }
            return tag;
        }
        case NbtTagType::IntArray: {
            int32_t len = r.read_int();
            if (len < 0) throw std::runtime_error("Invalid array length");
            NbtIntArray arr(len);
            for(int i=0; i<len; ++i) arr[i] = r.read_int();
            return NbtTag(std::move(arr));
        }
        case NbtTagType::LongArray: {
            int32_t len = r.read_int();
            if (len < 0) throw std::runtime_error("Invalid array length");
            NbtLongArray arr(len);
            for(int i=0; i<len; ++i) arr[i] = r.read_long();
            return NbtTag(std::move(arr));
        }
    }
    throw std::runtime_error("Unknown NBT tag type");
}

std::pair<std::string, NbtTag> NbtSerializer::deserialize(std::span<const uint8_t> data) {
    BufferReader r;
    r.buffer = data;
    NbtTagType type = (NbtTagType)r.read_byte();
    if (type == NbtTagType::End) {
        return {"", NbtTag()};
    }
    std::string name = r.read_string();
    NbtTag tag = read_tag_payload(r, type);
    return {name, std::move(tag)};
}

} // namespace mc
