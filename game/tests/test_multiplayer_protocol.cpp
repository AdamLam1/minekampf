#include <gtest/gtest.h>
#include "network/packet.hpp"
#include "network/chunk_codec.hpp"
#include "world/block.hpp"
#include "world/chunk.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>
#include <zlib.h>

namespace {

using namespace mc::net;

// Serialize -> deserialize helper that routes through a real frame buffer.
template <typename T>
std::vector<uint8_t> packet_body(const T& pkt) {
    PacketBuffer buf;
    pkt.serialize(buf);
    return buf.data();
}

TEST(MpProtocol, PacketIdsAreStable) {
    // Wire compatibility anchors: reordering these breaks every client.
    EXPECT_EQ(static_cast<int32_t>(PacketId::Handshake), 1);
    EXPECT_EQ(static_cast<int32_t>(PacketId::PlayerMove), 4);
    EXPECT_EQ(static_cast<int32_t>(PacketId::LoginAccepted), 32);
    EXPECT_EQ(static_cast<int32_t>(PacketId::ChunkData), 33);
    EXPECT_EQ(static_cast<int32_t>(PacketId::BlockUpdates), 34);
    EXPECT_EQ(static_cast<int32_t>(PacketId::Disconnect), 41);
    EXPECT_EQ(MP_PROTOCOL_VERSION, 1);
}

TEST(MpProtocol, EveryPacketRoundTripsThroughFactory) {
    // Build one of each packet, serialize, decode via the flat factory, and
    // compare the re-serialized bytes. Catches factory/deserialize drift.
    std::vector<std::shared_ptr<Packet>> packets;
    {
        HandshakePacket p;
        p.protocol_version = 7;
        p.username = "tester";
        packets.push_back(std::make_shared<HandshakePacket>(p));
    }
    {
        DisconnectPacket p;
        p.reason = "bye";
        packets.push_back(std::make_shared<DisconnectPacket>(p));
    }
    {
        KeepAlivePacket p;
        p.keep_alive_id = 1234567LL;
        packets.push_back(std::make_shared<KeepAlivePacket>(p));
    }
    {
        LoginStartPacket p;
        p.username = "Adam";
        packets.push_back(std::make_shared<LoginStartPacket>(p));
    }
    {
        LoginAcceptedPacket p;
        p.player_id = 3;
        p.seed = 0xDEADBEEFCAFEULL;
        p.game_mode = 1;
        p.time_of_day = 0.42f;
        p.spawn_x = 1.5f; p.spawn_y = 90.f; p.spawn_z = -2.5f;
        packets.push_back(std::make_shared<LoginAcceptedPacket>(p));
    }
    packets.push_back(std::make_shared<ClientReadyPacket>());
    {
        RequestChunksPacket p;
        p.center_x = -3; p.center_z = 5; p.radius = 6;
        packets.push_back(std::make_shared<RequestChunksPacket>(p));
    }
    {
        ChunkDataPacket p;
        p.chunk_x = -1; p.chunk_z = 2;
        p.data = {1, 2, 3, 4, 5, 6, 7, 8};
        packets.push_back(std::make_shared<ChunkDataPacket>(p));
    }
    {
        BlockUpdatesPacket p;
        p.updates.emplace_back(pack_block_pos(-100, 64, 999), 7);
        p.updates.emplace_back(pack_block_pos(0, 0, 0), mc::BLOCK_AIR);
        packets.push_back(std::make_shared<BlockUpdatesPacket>(p));
    }
    {
        PlayerMovePacket p;
        p.x = 1.f; p.y = 2.f; p.z = 3.f; p.yaw = 0.5f; p.pitch = -0.25f;
        p.flags = PLAYER_FLAG_SNEAKING | PLAYER_FLAG_ON_GROUND;
        packets.push_back(std::make_shared<PlayerMovePacket>(p));
    }
    {
        PlayerStatesPacket p;
        PlayerStateEntry e;
        e.player_id = 2; e.x = 9.f; e.y = 8.f; e.z = 7.f;
        e.yaw = 1.f; e.pitch = 0.f; e.flags = PLAYER_FLAG_SWINGING;
        p.states.push_back(e);
        packets.push_back(std::make_shared<PlayerStatesPacket>(p));
    }
    {
        SpawnPlayerPacket p;
        p.player_id = 4; p.name = "Gracz2";
        p.x = 1.f; p.y = 2.f; p.z = 3.f; p.yaw = 0.f; p.pitch = 0.f;
        packets.push_back(std::make_shared<SpawnPlayerPacket>(p));
    }
    {
        DespawnPlayerPacket p;
        p.player_id = 4;
        packets.push_back(std::make_shared<DespawnPlayerPacket>(p));
    }
    {
        TimeSyncPacket p;
        p.time_of_day = 0.75f;
        packets.push_back(std::make_shared<TimeSyncPacket>(p));
    }
    {
        ChatMessagePacket p;
        p.text = "/time 0.5";
        packets.push_back(std::make_shared<ChatMessagePacket>(p));
    }
    {
        ChatBroadcastPacket p;
        p.from_name = "Host";
        p.text = "czesc";
        packets.push_back(std::make_shared<ChatBroadcastPacket>(p));
    }
    {
        PlayerActionPacket p;
        p.action = static_cast<int32_t>(PlayerActionType::PlaceBlock);
        p.x = 1; p.y = 2; p.z = 3; p.block_id = 12;
        packets.push_back(std::make_shared<PlayerActionPacket>(p));
    }

    for (const auto& original : packets) {
        auto body = packet_body(*original);
        auto decoded = make_packet(original->packet_id());
        ASSERT_TRUE(decoded != nullptr) << "factory missing id "
                                        << original->packet_id();
        PacketBuffer buf(body);
        decoded->deserialize(buf);
        EXPECT_EQ(buf.readable_bytes(), 0u) << "trailing bytes for id "
                                            << original->packet_id();
        EXPECT_EQ(packet_body(*decoded), body) << "roundtrip mismatch for id "
                                               << original->packet_id();
    }
}

TEST(MpProtocol, BlockPosPackingRoundTripsInWorldBounds) {
    const int32_t min_c = -0x2000000; // 26-bit signed min
    const int32_t max_c = 0x1FFFFFF;  // 26-bit signed max
    const std::pair<int32_t, int32_t> coords[] = {
        {0, 0}, {-1, 1}, {min_c, max_c}, {max_c, min_c}};
    for (const auto& [x, z] : coords) {
        for (int y : {0, 1, 255, 4095}) {
            int64_t packed = pack_block_pos(x, y, z);
            EXPECT_EQ(unpack_block_x(packed), x);
            EXPECT_EQ(unpack_block_y(packed), y);
            EXPECT_EQ(unpack_block_z(packed), z);
        }
    }
}

TEST(MpProtocol, MalformedPayloadsAreRejected) {
    // Absurd payload length must throw (caught by the connection layer),
    // not attempt a multi-gigabyte allocation.
    ChunkDataPacket cd;
    PacketBuffer bad;
    bad.write_varint(0);
    bad.write_varint(0);
    bad.write_varint(1 << 30);
    EXPECT_THROW(cd.deserialize(bad), std::exception);

    // Oversized PlayerStates batch is rejected as well.
    PlayerStatesPacket ps;
    PacketBuffer bad2;
    bad2.write_varint(10000);
    EXPECT_THROW(ps.deserialize(bad2), std::exception);
}

} // namespace
