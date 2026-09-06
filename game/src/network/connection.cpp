#include "network/connection.hpp"
#include <iostream>

namespace mc::net {

Connection::Connection(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)) {}

Connection::~Connection() {
    stop();
}

void Connection::start() {
    do_read_header();
}

void Connection::stop() {
    asio::error_code ec;
    socket_.close(ec);
    if (disconnect_handler_) {
        disconnect_handler_(shared_from_this());
    }
}

void Connection::send_packet(const Packet& packet) {
    PacketBuffer buf;
    packet.serialize(buf);

    PacketBuffer final_buf;
    bool start_write = false;
    
    // Compression is omitted for simplicity in this initial iteration unless threshold >= 0
    if (compression_threshold_ >= 0) {
        // Mock ZLIB compression for prototype, or we just send uncompressed if below threshold
        if (static_cast<int>(buf.data().size()) < compression_threshold_) {
            PacketBuffer payload;
            payload.write_varint(0); // Data length = 0 means uncompressed
            payload.write_bytes(buf.data());

            final_buf.write_varint(static_cast<int32_t>(payload.data().size()) + PacketBuffer::varint_size(packet.packet_id()));
            final_buf.write_varint(packet.packet_id());
            final_buf.write_bytes(payload.data());
        } else {
            // Zlib compress
            std::vector<uint8_t> out(buf.data().size() * 2 + 100); // upper bound
            uLongf out_len = static_cast<uLongf>(out.size());
            compress(out.data(), &out_len, buf.data().data(), static_cast<uLong>(buf.data().size()));
            
            PacketBuffer payload;
            payload.write_varint(static_cast<int32_t>(buf.data().size()));
            payload.write_bytes(std::span<const uint8_t>(out.data(), out_len));

            final_buf.write_varint(static_cast<int32_t>(payload.data().size()) + PacketBuffer::varint_size(packet.packet_id()));
            final_buf.write_varint(packet.packet_id());
            final_buf.write_bytes(payload.data());
        }
    } else {
        // No compression
        final_buf.write_varint(static_cast<int32_t>(buf.data().size()) + PacketBuffer::varint_size(packet.packet_id()));
        final_buf.write_varint(packet.packet_id());
        final_buf.write_bytes(buf.data());
    }

    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_queue_.push(final_buf.data());
        if (!is_writing_) {
            is_writing_ = true;
            start_write = true;
        }
    }
    // do_write() takes write_mutex_ itself — it must run with our lock
    // released (std::mutex is non-recursive; relocking here threw EDEADLK).
    if (start_write) {
        do_write();
    }
}

void Connection::do_write() {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (write_queue_.empty()) {
        is_writing_ = false;
        return;
    }

    auto& data = write_queue_.front();
    auto self(shared_from_this());
    asio::async_write(socket_, asio::buffer(data),
        [this, self](std::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                bool start_next = false;
                {
                    std::lock_guard<std::mutex> lock2(write_mutex_);
                    write_queue_.pop();
                    if (!write_queue_.empty()) {
                        start_next = true; // is_writing_ stays claimed
                    } else {
                        is_writing_ = false;
                    }
                }
                // do_write() takes write_mutex_ itself — it must not run
                // while we still hold the lock (std::mutex is non-recursive).
                if (start_next) {
                    do_write();
                }
            } else {
                stop();
            }
        });
}

void Connection::do_read_header() {
    auto self(shared_from_this());
    // Read VarInt length byte by byte (simplification)
    std::shared_ptr<int32_t> length = std::make_shared<int32_t>(0);
    std::shared_ptr<int> shift = std::make_shared<int>(0);

    auto read_byte_cb = std::make_shared<std::function<void(std::error_code, std::size_t)>>();
    *read_byte_cb = [this, self, length, shift, read_byte_cb](std::error_code ec, std::size_t /*length_read*/) {
        if (!ec) {
            uint8_t b = read_data_[0];
            *length |= (b & 0x7F) << (*shift);
            if ((b & 0x80) == 0) {
                do_read_body(*length);
            } else {
                *shift += 7;
                if (*shift >= 32) {
                    stop(); // VarInt too big
                    return;
                }
                asio::async_read(socket_, asio::buffer(read_data_, 1), *read_byte_cb);
            }
        } else {
            stop();
        }
    };

    read_data_.resize(1);
    asio::async_read(socket_, asio::buffer(read_data_, 1), *read_byte_cb);
}

void Connection::do_read_body(int32_t length) {
    if (length <= 0 || length > 2097151) {
        stop();
        return;
    }

    auto self(shared_from_this());
    read_data_.resize(length);
    asio::async_read(socket_, asio::buffer(read_data_, length),
        [this, self](std::error_code ec, std::size_t /*length*/) {
            if (!ec) {
                handle_packet();
                do_read_header(); // Next packet
            } else {
                stop();
            }
        });
}

std::optional<std::pair<int32_t, PacketBuffer>> decode_frame(const std::vector<uint8_t>& frame,
                                                             int compression_threshold) {
    try {
        PacketBuffer buf(frame);
        int32_t id = buf.read_varint();

        if (compression_threshold >= 0) {
            int32_t data_length = buf.read_varint();
            if (data_length != 0) {
                // Sender compressed the raw body; the id was written BEFORE
                // compression and must not be re-read from the inflated body.
                std::vector<uint8_t> uncompressed(static_cast<size_t>(data_length));
                uLongf dest_len = data_length;
                auto remaining = buf.read_bytes(buf.readable_bytes());
                if (uncompress(uncompressed.data(), &dest_len,
                               remaining.data(), static_cast<uLong>(remaining.size())) != Z_OK) {
                    return std::nullopt;
                }
                buf = PacketBuffer(std::move(uncompressed));
            }
        }
        return std::make_pair(id, std::move(buf));
    } catch (...) {
        return std::nullopt;
    }
}

void Connection::handle_packet() {
    auto parsed = decode_frame(read_data_, compression_threshold_);
    if (!parsed) {
        stop();
        return;
    }

    const int32_t id = parsed->first;
    PacketBuffer& body = parsed->second;

    auto pkt = make_packet(id);
    if (pkt) {
        try {
            pkt->deserialize(body);
            if (packet_handler_) {
                packet_handler_(shared_from_this(), pkt);
            }
        } catch (const std::exception& e) {
            std::cerr << "Packet parsing error: " << e.what() << "\n";
            stop();
        }
    } else {
        // Unknown packet id: ignore (forward compatibility).
    }
}

std::shared_ptr<Packet> make_packet(int32_t id) {
    switch (static_cast<PacketId>(id)) {
        case PacketId::Handshake: return std::make_shared<HandshakePacket>();
        case PacketId::LoginStart: return std::make_shared<LoginStartPacket>();
        case PacketId::ClientReady: return std::make_shared<ClientReadyPacket>();
        case PacketId::PlayerMove: return std::make_shared<PlayerMovePacket>();
        case PacketId::PlayerAction: return std::make_shared<PlayerActionPacket>();
        case PacketId::ChatMessage: return std::make_shared<ChatMessagePacket>();
        case PacketId::RequestChunks: return std::make_shared<RequestChunksPacket>();
        case PacketId::LoginAccepted: return std::make_shared<LoginAcceptedPacket>();
        case PacketId::ChunkData: return std::make_shared<ChunkDataPacket>();
        case PacketId::BlockUpdates: return std::make_shared<BlockUpdatesPacket>();
        case PacketId::SpawnPlayer: return std::make_shared<SpawnPlayerPacket>();
        case PacketId::DespawnPlayer: return std::make_shared<DespawnPlayerPacket>();
        case PacketId::PlayerStates: return std::make_shared<PlayerStatesPacket>();
        case PacketId::TimeSync: return std::make_shared<TimeSyncPacket>();
        case PacketId::ChatBroadcast: return std::make_shared<ChatBroadcastPacket>();
        case PacketId::KeepAlive: return std::make_shared<KeepAlivePacket>();
        case PacketId::Disconnect: return std::make_shared<DisconnectPacket>();
    }
    return nullptr;
}

} // namespace mc::net
