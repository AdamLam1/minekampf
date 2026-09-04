#pragma once

#include "network/buffer.hpp"
#include <memory>
#include <string>

namespace mc::net {

enum class ConnectionState {
    HANDSHAKE = 0,
    STATUS = 1,
    LOGIN = 2,
    PLAY = 3,
    DISCONNECT = 4
};

// Base packet class
struct Packet {
    virtual ~Packet() = default;
    virtual void serialize(PacketBuffer& buf) const = 0;
    virtual void deserialize(PacketBuffer& buf) = 0;
    [[nodiscard]] virtual int32_t packet_id() const = 0;
};

// ---------------------------------------------------------
// HANDSHAKE STATE
// ---------------------------------------------------------

struct HandshakePacket : public Packet {
    int32_t protocol_version;
    std::string server_address;
    uint16_t server_port;
    int32_t next_state;

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(protocol_version);
        buf.write_string(server_address);
        buf.write_short(server_port);
        buf.write_varint(next_state);
    }
    void deserialize(PacketBuffer& buf) override {
        protocol_version = buf.read_varint();
        server_address = buf.read_string();
        server_port = buf.read_short();
        next_state = buf.read_varint();
    }
    [[nodiscard]] int32_t packet_id() const override { return 0x00; }
};

// ---------------------------------------------------------
// LOGIN STATE
// ---------------------------------------------------------

struct LoginStartPacket : public Packet {
    std::string username;

    void serialize(PacketBuffer& buf) const override {
        buf.write_string(username);
    }
    void deserialize(PacketBuffer& buf) override {
        username = buf.read_string();
    }
    [[nodiscard]] int32_t packet_id() const override { return 0x00; }
};

struct LoginSuccessPacket : public Packet {
    std::string uuid_str;
    std::string username;

    void serialize(PacketBuffer& buf) const override {
        buf.write_string(uuid_str);
        buf.write_string(username);
        buf.write_varint(0); // properties count = 0
    }
    void deserialize(PacketBuffer& buf) override {
        uuid_str = buf.read_string();
        username = buf.read_string();
        int32_t properties = buf.read_varint();
        for (int i = 0; i < properties; ++i) {
            buf.read_string();
            buf.read_string();
            // Assuming no signature for simplicity in custom protocol
        }
    }
    [[nodiscard]] int32_t packet_id() const override { return 0x02; }
};

// ---------------------------------------------------------
// PLAY STATE
// ---------------------------------------------------------

struct KeepAlivePacket : public Packet {
    int64_t keep_alive_id;

    void serialize(PacketBuffer& buf) const override {
        buf.write_long(keep_alive_id);
    }
    void deserialize(PacketBuffer& buf) override {
        keep_alive_id = buf.read_long();
    }
    [[nodiscard]] int32_t packet_id() const override { return 0x21; } // Typically 0x21 or similar
};

struct PlayerPositionPacket : public Packet {
    double x, y, z;
    bool on_ground;

    void serialize(PacketBuffer& buf) const override {
        buf.write_double(x);
        buf.write_double(y);
        buf.write_double(z);
        buf.write_bool(on_ground);
    }
    void deserialize(PacketBuffer& buf) override {
        x = buf.read_double();
        y = buf.read_double();
        z = buf.read_double();
        on_ground = buf.read_bool();
    }
    [[nodiscard]] int32_t packet_id() const override { return 0x1E; }
};

struct BlockUpdatePacket : public Packet {
    int64_t position; // Packed
    int32_t block_state_id;

    void serialize(PacketBuffer& buf) const override {
        buf.write_long(position);
        buf.write_varint(block_state_id);
    }
    void deserialize(PacketBuffer& buf) override {
        position = buf.read_long();
        block_state_id = buf.read_varint();
    }
    [[nodiscard]] int32_t packet_id() const override { return 0x44; }
};

// Chunk Data is complex; omitted full NBT for prototype
struct ChunkDataPacket : public Packet {
    int32_t chunk_x, chunk_z;
    std::vector<uint8_t> data;

    void serialize(PacketBuffer& buf) const override {
        buf.write_int(chunk_x);
        buf.write_int(chunk_z);
        buf.write_varint(static_cast<int32_t>(data.size()));
        buf.write_bytes(data);
    }
    void deserialize(PacketBuffer& buf) override {
        chunk_x = buf.read_int();
        chunk_z = buf.read_int();
        int32_t len = buf.read_varint();
        data = buf.read_bytes(len);
    }
    [[nodiscard]] int32_t packet_id() const override { return 0x25; }
};

} // namespace mc::net
