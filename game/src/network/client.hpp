#pragma once

#include "network/connection.hpp"
#include <asio.hpp>
#include <memory>
#include <string>

namespace mc::net {

class Client {
public:
    explicit Client(asio::io_context& io_context);
    ~Client();

    void connect(const std::string& host, const std::string& port);
    void disconnect();

    void send_packet(const Packet& packet);

    // Callbacks
    using ConnectedHandler = std::function<void()>;
    using DisconnectedHandler = std::function<void()>;
    using PacketReceivedHandler = std::function<void(std::shared_ptr<Packet>)>;

    void set_connected_handler(ConnectedHandler handler) { on_connect_ = std::move(handler); }
    void set_disconnected_handler(DisconnectedHandler handler) { on_disconnect_ = std::move(handler); }
    void set_packet_received_handler(PacketReceivedHandler handler) { on_packet_ = std::move(handler); }

private:
    asio::io_context& io_context_;
    asio::ip::tcp::resolver resolver_;
    std::shared_ptr<Connection> connection_;

    ConnectedHandler on_connect_;
    DisconnectedHandler on_disconnect_;
    PacketReceivedHandler on_packet_;
};

} // namespace mc::net
