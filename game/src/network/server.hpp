#pragma once

#include "network/connection.hpp"
#include <asio.hpp>
#include <unordered_set>
#include <memory>

namespace mc::net {

class Server {
public:
    Server(asio::io_context& io_context, uint16_t port);

    void start();
    void stop();
    
    void broadcast(const Packet& packet);

    // Callbacks
    using ClientConnectedHandler = std::function<void(std::shared_ptr<Connection>)>;
    using ClientDisconnectedHandler = std::function<void(std::shared_ptr<Connection>)>;
    using PacketReceivedHandler = std::function<void(std::shared_ptr<Connection>, std::shared_ptr<Packet>)>;

    void set_client_connected_handler(ClientConnectedHandler handler) { on_connect_ = std::move(handler); }
    void set_client_disconnected_handler(ClientDisconnectedHandler handler) { on_disconnect_ = std::move(handler); }
    void set_packet_received_handler(PacketReceivedHandler handler) { on_packet_ = std::move(handler); }

private:
    void do_accept();

    asio::ip::tcp::acceptor acceptor_;
    std::unordered_set<std::shared_ptr<Connection>> connections_;

    ClientConnectedHandler on_connect_;
    ClientDisconnectedHandler on_disconnect_;
    PacketReceivedHandler on_packet_;
};

} // namespace mc::net
