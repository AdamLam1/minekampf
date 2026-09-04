#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <stdexcept>
#include <string_view>

namespace mc::net {

// A buffer for reading and writing Minecraft protocol data types.
class PacketBuffer {
public:
    PacketBuffer() = default;
    explicit PacketBuffer(std::vector<uint8_t> data) : data_(std::move(data)), read_pos_(0) {}

    // Writing
    void write_byte(uint8_t b);
    void write_short(uint16_t s);
    void write_int(int32_t i);
    void write_long(int64_t l);
    void write_float(float f);
    void write_double(double d);
    void write_bool(bool b);

    // VarInt/VarLong
    void write_varint(int32_t value);
    void write_varlong(int64_t value);

    // Strings
    void write_string(std::string_view str);

    // Raw data
    void write_bytes(std::span<const uint8_t> bytes);

    // Reading
    uint8_t read_byte();
    uint16_t read_short();
    int32_t read_int();
    int64_t read_long();
    float read_float();
    double read_double();
    bool read_bool();

    int32_t read_varint();
    int64_t read_varlong();

    std::string read_string(size_t max_length = 32767);
    
    std::vector<uint8_t> read_bytes(size_t length);

    // Buffer access
    [[nodiscard]] const std::vector<uint8_t>& data() const { return data_; }
    [[nodiscard]] size_t readable_bytes() const { return data_.size() - read_pos_; }
    [[nodiscard]] size_t read_position() const { return read_pos_; }
    void clear() { data_.clear(); read_pos_ = 0; }
    void set_read_position(size_t pos) { read_pos_ = pos; }

    static size_t varint_size(int32_t value);

private:
    std::vector<uint8_t> data_;
    size_t read_pos_{0};
};

} // namespace mc::net
