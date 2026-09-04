#include "network/buffer.hpp"

#include <bit>
#include <cstring>
#include <stdexcept>

namespace mc::net {

void PacketBuffer::write_byte(uint8_t b) {
    data_.push_back(b);
}

void PacketBuffer::write_short(uint16_t s) {
    data_.push_back(static_cast<uint8_t>(s >> 8));
    data_.push_back(static_cast<uint8_t>(s & 0xFF));
}

void PacketBuffer::write_int(int32_t i) {
    uint32_t u = static_cast<uint32_t>(i);
    data_.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
    data_.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
    data_.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    data_.push_back(static_cast<uint8_t>(u & 0xFF));
}

void PacketBuffer::write_long(int64_t l) {
    uint64_t u = static_cast<uint64_t>(l);
    for (int shift = 56; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<uint8_t>((u >> shift) & 0xFF));
    }
}

void PacketBuffer::write_float(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(float));
    write_int(static_cast<int32_t>(u));
}

void PacketBuffer::write_double(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof(double));
    write_long(static_cast<int64_t>(u));
}

void PacketBuffer::write_bool(bool b) {
    write_byte(b ? 1 : 0);
}

void PacketBuffer::write_varint(int32_t value) {
    uint32_t u = static_cast<uint32_t>(value);
    while (true) {
        if ((u & ~0x7F) == 0) {
            write_byte(static_cast<uint8_t>(u));
            return;
        }
        write_byte(static_cast<uint8_t>((u & 0x7F) | 0x80));
        u >>= 7;
    }
}

void PacketBuffer::write_varlong(int64_t value) {
    uint64_t u = static_cast<uint64_t>(value);
    while (true) {
        if ((u & ~0x7F) == 0) {
            write_byte(static_cast<uint8_t>(u));
            return;
        }
        write_byte(static_cast<uint8_t>((u & 0x7F) | 0x80));
        u >>= 7;
    }
}

void PacketBuffer::write_string(std::string_view str) {
    write_varint(static_cast<int32_t>(str.length()));
    write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(str.data()), str.length()));
}

void PacketBuffer::write_bytes(std::span<const uint8_t> bytes) {
    data_.insert(data_.end(), bytes.begin(), bytes.end());
}

uint8_t PacketBuffer::read_byte() {
    if (read_pos_ >= data_.size()) throw std::out_of_range("PacketBuffer: end of stream");
    return data_[read_pos_++];
}

uint16_t PacketBuffer::read_short() {
    uint16_t s = 0;
    s |= static_cast<uint16_t>(read_byte()) << 8;
    s |= static_cast<uint16_t>(read_byte());
    return s; // byteswap applied implicitly by reading big-endian bytes
}

int32_t PacketBuffer::read_int() {
    uint32_t i = 0;
    i |= static_cast<uint32_t>(read_byte()) << 24;
    i |= static_cast<uint32_t>(read_byte()) << 16;
    i |= static_cast<uint32_t>(read_byte()) << 8;
    i |= static_cast<uint32_t>(read_byte());
    return static_cast<int32_t>(i);
}

int64_t PacketBuffer::read_long() {
    uint64_t l = 0;
    for (int i = 0; i < 8; ++i) {
        l = (l << 8) | read_byte();
    }
    return static_cast<int64_t>(l);
}

float PacketBuffer::read_float() {
    uint32_t u = static_cast<uint32_t>(read_int());
    float f;
    std::memcpy(&f, &u, sizeof(float));
    return f;
}

double PacketBuffer::read_double() {
    uint64_t u = static_cast<uint64_t>(read_long());
    double d;
    std::memcpy(&d, &u, sizeof(double));
    return d;
}

bool PacketBuffer::read_bool() {
    return read_byte() != 0;
}

int32_t PacketBuffer::read_varint() {
    uint32_t result = 0;
    int shift = 0;
    while (true) {
        uint8_t b = read_byte();
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift >= 32) throw std::runtime_error("VarInt is too big");
    }
    return static_cast<int32_t>(result);
}

int64_t PacketBuffer::read_varlong() {
    uint64_t result = 0;
    int shift = 0;
    while (true) {
        uint8_t b = read_byte();
        result |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift >= 64) throw std::runtime_error("VarLong is too big");
    }
    return static_cast<int64_t>(result);
}

std::string PacketBuffer::read_string(size_t max_length) {
    size_t length = static_cast<size_t>(read_varint());
    if (length > max_length) throw std::runtime_error("String length exceeds max length");
    std::string str;
    str.resize(length);
    for (size_t i = 0; i < length; ++i) {
        str[i] = static_cast<char>(read_byte());
    }
    return str;
}

std::vector<uint8_t> PacketBuffer::read_bytes(size_t length) {
    std::vector<uint8_t> bytes;
    bytes.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        bytes.push_back(read_byte());
    }
    return bytes;
}

size_t PacketBuffer::varint_size(int32_t value) {
    uint32_t u = static_cast<uint32_t>(value);
    size_t count = 0;
    do {
        u >>= 7;
        count++;
    } while (u != 0);
    return count;
}

} // namespace mc::net
