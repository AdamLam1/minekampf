#pragma once

#include "network/buffer.hpp"
#include "network/packet.hpp"

#include <asio.hpp>
#include <zlib.h>
#include <memory>
#include <functional>
#include <optional>
#include <queue>
#include <mutex>
#include <thread>
#include <utility>

namespace mc::net {

// Decodes one length-prefixed frame body (after the varint length prefix has
// been consumed by the caller): [packet id][payload], with the optional
// zlib framing `[data_length][compressed payload]` when threshold >= 0.
// Pure function so the wire format is unit-testable without sockets.
[[nodiscard]] std::optional<std::pair<int32_t, PacketBuffer>> decode_frame(
    const std::vector<uint8_t>& frame, int compression_threshold);

class Connection : public std::enable_shared_from_this<Connection> {
public:
    using PacketHandler = std::function<void(std::shared_ptr<Connection>, std::shared_ptr<Packet>)>;
    using DisconnectHandler = std::function<void(std::shared_ptr<Connection>)>;

    explicit Connection(asio::ip::tcp::socket socket);
    ~Connection();

    void start();
    void stop();

    void send_packet(const Packet& packet);
    
    void set_packet_handler(PacketHandler handler) { packet_handler_ = std::move(handler); }
    void set_disconnect_handler(DisconnectHandler handler) { disconnect_handler_ = std::move(handler); }

    [[nodiscard]] int compression_threshold() const { return compression_threshold_; }
    void set_compression_threshold(int threshold) { compression_threshold_ = threshold; }

private:
    void do_read_header();
    void do_read_body(int32_t length);
    void handle_packet();

    void do_write();

    asio::ip::tcp::socket socket_;
    int compression_threshold_{-1};

    PacketBuffer read_buffer_;
    std::vector<uint8_t> read_data_;

    std::queue<std::vector<uint8_t>> write_queue_;
    std::mutex write_mutex_;
    bool is_writing_{false};

    PacketHandler packet_handler_;
    DisconnectHandler disconnect_handler_;
};

} // namespace mc::net
