#include <gtest/gtest.h>
#include "network/buffer.hpp"
#include "network/packet.hpp"

using namespace mc::net;

TEST(NetworkBuffer, WriteReadByte) {
    PacketBuffer buf;
    buf.write_byte(42);
    buf.write_byte(255);
    
    EXPECT_EQ(buf.read_byte(), 42);
    EXPECT_EQ(buf.read_byte(), 255);
}

TEST(NetworkBuffer, WriteReadVarInt) {
    PacketBuffer buf;
    
    // Values from the protocol specification
    buf.write_varint(0);
    buf.write_varint(1);
    buf.write_varint(127);
    buf.write_varint(128);
    buf.write_varint(255);
    buf.write_varint(25565);
    buf.write_varint(2097151);
    buf.write_varint(-1);

    EXPECT_EQ(buf.read_varint(), 0);
    EXPECT_EQ(buf.read_varint(), 1);
    EXPECT_EQ(buf.read_varint(), 127);
    EXPECT_EQ(buf.read_varint(), 128);
    EXPECT_EQ(buf.read_varint(), 255);
    EXPECT_EQ(buf.read_varint(), 25565);
    EXPECT_EQ(buf.read_varint(), 2097151);
    EXPECT_EQ(buf.read_varint(), -1);
}

TEST(NetworkBuffer, VarIntEncodedSize) {
    EXPECT_EQ(PacketBuffer::varint_size(0), 1);
    EXPECT_EQ(PacketBuffer::varint_size(127), 1);
    EXPECT_EQ(PacketBuffer::varint_size(128), 2);
    EXPECT_EQ(PacketBuffer::varint_size(25565), 3);
    EXPECT_EQ(PacketBuffer::varint_size(2097151), 3);
}

TEST(NetworkBuffer, WriteReadString) {
    PacketBuffer buf;
    buf.write_string("Hello, Minecraft!");
    EXPECT_EQ(buf.read_string(), "Hello, Minecraft!");
}
