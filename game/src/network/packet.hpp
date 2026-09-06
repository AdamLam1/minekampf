#pragma once

#include "network/buffer.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mc::net {

// Minekampf multiplayer protocol v1. Flat packet id space (C→S low ids,
// S→C high ids); the handshake negotiates MP_PROTOCOL_VERSION.
inline constexpr int32_t MP_PROTOCOL_VERSION = 1;
inline constexpr uint16_t MP_DEFAULT_PORT = 25590;
inline constexpr int MP_MAX_PLAYERS = 8;

enum class PacketId : int32_t {
    // Client → Server
    Handshake = 1,
    LoginStart = 2,
    ClientReady = 3,
    PlayerMove = 4,
    PlayerAction = 5,
    ChatMessage = 6,
    RequestChunks = 7,
    // Server → Client
    LoginAccepted = 32,
    ChunkData = 33,
    BlockUpdates = 34,
    SpawnPlayer = 35,
    DespawnPlayer = 36,
    PlayerStates = 37,
    TimeSync = 38,
    ChatBroadcast = 39,
    KeepAlive = 40,
    Disconnect = 41,
};

// Shared player movement flag bits (PlayerMove / PlayerStates).
inline constexpr uint8_t PLAYER_FLAG_ON_GROUND = 1 << 0;
inline constexpr uint8_t PLAYER_FLAG_SNEAKING = 1 << 1;
inline constexpr uint8_t PLAYER_FLAG_SPRINTING = 1 << 2;
inline constexpr uint8_t PLAYER_FLAG_SWINGING = 1 << 3;

// Block position packing (Minecraft-style): x: bits 38..63, z: bits 12..37,
// y: bits 0..11 (each coordinate truncated to its field).
[[nodiscard]] inline int64_t pack_block_pos(int32_t x, int32_t y, int32_t z) {
    return (static_cast<int64_t>(x & 0x3FFFFFF) << 38) |
           (static_cast<int64_t>(z & 0x3FFFFFF) << 12) |
           static_cast<int64_t>(y & 0xFFF);
}
[[nodiscard]] inline int32_t unpack_block_x(int64_t packed) {
    // x occupies the top bits: arithmetic shift sign-extends naturally.
    return static_cast<int32_t>(packed >> 38);
}
[[nodiscard]] inline int32_t unpack_block_z(int64_t packed) {
    // Shift the 26-bit z field up to the sign bit, then shift back.
    return static_cast<int32_t>((packed << 26) >> 38);
}
[[nodiscard]] inline int32_t unpack_block_y(int64_t packed) {
    // y lives in the low 12 bits and is non-negative in this engine
    // (MIN_Y = 0), so mask rather than sign-extend.
    return static_cast<int32_t>(packed & 0xFFF);
}

struct Packet {
    virtual ~Packet() = default;
    virtual void serialize(PacketBuffer& buf) const = 0;
    virtual void deserialize(PacketBuffer& buf) = 0;
    [[nodiscard]] virtual int32_t packet_id() const = 0;
};

// ---------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------

// C→S, always the first packet on the wire.
struct HandshakePacket : public Packet {
    int32_t protocol_version = MP_PROTOCOL_VERSION;
    std::string username; // informational only; LoginStart is authoritative

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(protocol_version);
        buf.write_string(username);
    }
    void deserialize(PacketBuffer& buf) override {
        protocol_version = buf.read_varint();
        username = buf.read_string(64);
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::Handshake); }
};

// S→C: rejected login / kicked / server closing.
struct DisconnectPacket : public Packet {
    std::string reason;

    void serialize(PacketBuffer& buf) const override { buf.write_string(reason); }
    void deserialize(PacketBuffer& buf) override { reason = buf.read_string(256); }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::Disconnect); }
};

struct KeepAlivePacket : public Packet {
    int64_t keep_alive_id = 0;

    void serialize(PacketBuffer& buf) const override { buf.write_long(keep_alive_id); }
    void deserialize(PacketBuffer& buf) override { keep_alive_id = buf.read_long(); }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::KeepAlive); }
};

// ---------------------------------------------------------
// Login
// ---------------------------------------------------------

struct LoginStartPacket : public Packet {
    std::string username;

    void serialize(PacketBuffer& buf) const override { buf.write_string(username); }
    void deserialize(PacketBuffer& buf) override { username = buf.read_string(32); }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::LoginStart); }
};

// S→C: login OK. Tells the client its identity and the world it joins.
struct LoginAcceptedPacket : public Packet {
    int32_t player_id = 0;
    uint64_t seed = 0;
    int32_t game_mode = 0; // GameMode
    float time_of_day = 0.25f;
    float spawn_x = 0.f, spawn_y = 100.f, spawn_z = 0.f;

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(player_id);
        buf.write_long(static_cast<int64_t>(seed));
        buf.write_varint(game_mode);
        buf.write_float(time_of_day);
        buf.write_float(spawn_x);
        buf.write_float(spawn_y);
        buf.write_float(spawn_z);
    }
    void deserialize(PacketBuffer& buf) override {
        player_id = buf.read_varint();
        seed = static_cast<uint64_t>(buf.read_long());
        game_mode = buf.read_varint();
        time_of_day = buf.read_float();
        spawn_x = buf.read_float();
        spawn_y = buf.read_float();
        spawn_z = buf.read_float();
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::LoginAccepted); }
};

// C→S: login finished, client built its local world and wants chunk data.
struct ClientReadyPacket : public Packet {
    void serialize(PacketBuffer&) const override {}
    void deserialize(PacketBuffer&) override {}
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::ClientReady); }
};

// C→S: stream chunks around this column (sent on join and on chunk crossings).
struct RequestChunksPacket : public Packet {
    int32_t center_x = 0, center_z = 0;
    int32_t radius = 6;

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(center_x);
        buf.write_varint(center_z);
        buf.write_varint(radius);
    }
    void deserialize(PacketBuffer& buf) override {
        center_x = buf.read_varint();
        center_z = buf.read_varint();
        radius = buf.read_varint();
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::RequestChunks); }
};

// ---------------------------------------------------------
// World state
// ---------------------------------------------------------

// S→C: zlib-compressed chunk payload (see network/chunk_codec.hpp).
struct ChunkDataPacket : public Packet {
    int32_t chunk_x = 0, chunk_z = 0;
    std::vector<uint8_t> data; // compressed

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(chunk_x);
        buf.write_varint(chunk_z);
        buf.write_varint(static_cast<int32_t>(data.size()));
        buf.write_bytes(data);
    }
    void deserialize(PacketBuffer& buf) override {
        chunk_x = buf.read_varint();
        chunk_z = buf.read_varint();
        int32_t len = buf.read_varint();
        if (len < 0 || len > (16 << 20)) throw std::runtime_error("ChunkData payload too large");
        data = buf.read_bytes(static_cast<size_t>(len));
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::ChunkData); }
};

// S→C: batch of block changes applied this tick.
struct BlockUpdatesPacket : public Packet {
    // (packed pos, block state id)
    std::vector<std::pair<int64_t, int32_t>> updates;

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(static_cast<int32_t>(updates.size()));
        for (const auto& [pos, block] : updates) {
            buf.write_long(pos);
            buf.write_varint(block);
        }
    }
    void deserialize(PacketBuffer& buf) override {
        int32_t count = buf.read_varint();
        if (count < 0 || count > 65536) throw std::runtime_error("BlockUpdates batch too large");
        updates.reserve(static_cast<size_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            int64_t pos = buf.read_long();
            updates.emplace_back(pos, buf.read_varint());
        }
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::BlockUpdates); }
};

// S→C: host's authoritative clock.
struct TimeSyncPacket : public Packet {
    float time_of_day = 0.25f;

    void serialize(PacketBuffer& buf) const override { buf.write_float(time_of_day); }
    void deserialize(PacketBuffer& buf) override { time_of_day = buf.read_float(); }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::TimeSync); }
};

// ---------------------------------------------------------
// Players
// ---------------------------------------------------------

// C→S: local player state at 20 Hz (client-authoritative movement).
struct PlayerMovePacket : public Packet {
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    uint8_t flags = 0;

    void serialize(PacketBuffer& buf) const override {
        buf.write_float(x);
        buf.write_float(y);
        buf.write_float(z);
        buf.write_float(yaw);
        buf.write_float(pitch);
        buf.write_byte(flags);
    }
    void deserialize(PacketBuffer& buf) override {
        x = buf.read_float();
        y = buf.read_float();
        z = buf.read_float();
        yaw = buf.read_float();
        pitch = buf.read_float();
        flags = buf.read_byte();
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::PlayerMove); }
};

struct PlayerStateEntry {
    int32_t player_id = 0;
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    uint8_t flags = 0;
};

// S→C: snapshot of every connected player at 20 Hz.
struct PlayerStatesPacket : public Packet {
    std::vector<PlayerStateEntry> states;

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(static_cast<int32_t>(states.size()));
        for (const auto& s : states) {
            buf.write_varint(s.player_id);
            buf.write_float(s.x);
            buf.write_float(s.y);
            buf.write_float(s.z);
            buf.write_float(s.yaw);
            buf.write_float(s.pitch);
            buf.write_byte(s.flags);
        }
    }
    void deserialize(PacketBuffer& buf) override {
        int32_t count = buf.read_varint();
        if (count < 0 || count > MP_MAX_PLAYERS * 4) throw std::runtime_error("PlayerStates too large");
        states.resize(static_cast<size_t>(count));
        for (auto& s : states) {
            s.player_id = buf.read_varint();
            s.x = buf.read_float();
            s.y = buf.read_float();
            s.z = buf.read_float();
            s.yaw = buf.read_float();
            s.pitch = buf.read_float();
            s.flags = buf.read_byte();
        }
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::PlayerStates); }
};

// S→C: a player appeared (join or entered our view).
struct SpawnPlayerPacket : public Packet {
    int32_t player_id = 0;
    std::string name;
    float x = 0, y = 100, z = 0;
    float yaw = 0, pitch = 0;

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(player_id);
        buf.write_string(name);
        buf.write_float(x);
        buf.write_float(y);
        buf.write_float(z);
        buf.write_float(yaw);
        buf.write_float(pitch);
    }
    void deserialize(PacketBuffer& buf) override {
        player_id = buf.read_varint();
        name = buf.read_string(32);
        x = buf.read_float();
        y = buf.read_float();
        z = buf.read_float();
        yaw = buf.read_float();
        pitch = buf.read_float();
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::SpawnPlayer); }
};

struct DespawnPlayerPacket : public Packet {
    int32_t player_id = 0;

    void serialize(PacketBuffer& buf) const override { buf.write_varint(player_id); }
    void deserialize(PacketBuffer& buf) override { player_id = buf.read_varint(); }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::DespawnPlayer); }
};

// ---------------------------------------------------------
// Interaction & chat
// ---------------------------------------------------------

enum class PlayerActionType : int32_t {
    BreakBlock = 0,
    PlaceBlock = 1,
};

// C→S: intent to modify the world; the host validates and applies.
struct PlayerActionPacket : public Packet {
    int32_t action = 0; // PlayerActionType
    int32_t x = 0, y = 0, z = 0;
    int32_t block_id = 0; // for place

    void serialize(PacketBuffer& buf) const override {
        buf.write_varint(action);
        buf.write_varint(x);
        buf.write_varint(y);
        buf.write_varint(z);
        buf.write_varint(block_id);
    }
    void deserialize(PacketBuffer& buf) override {
        action = buf.read_varint();
        x = buf.read_varint();
        y = buf.read_varint();
        z = buf.read_varint();
        block_id = buf.read_varint();
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::PlayerAction); }
};

// C→S: chat line (leading '/' means a command — host executes locally).
struct ChatMessagePacket : public Packet {
    std::string text;

    void serialize(PacketBuffer& buf) const override { buf.write_string(text); }
    void deserialize(PacketBuffer& buf) override { text = buf.read_string(256); }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::ChatMessage); }
};

// S→C: chat line from a player.
struct ChatBroadcastPacket : public Packet {
    std::string from_name;
    std::string text;

    void serialize(PacketBuffer& buf) const override {
        buf.write_string(from_name);
        buf.write_string(text);
    }
    void deserialize(PacketBuffer& buf) override {
        from_name = buf.read_string(32);
        text = buf.read_string(256);
    }
    [[nodiscard]] int32_t packet_id() const override { return static_cast<int32_t>(PacketId::ChatBroadcast); }
};

// Flat packet factory (no transport-level state machine; sessions enforce
// sequencing on top).
[[nodiscard]] std::shared_ptr<Packet> make_packet(int32_t id);

} // namespace mc::net
