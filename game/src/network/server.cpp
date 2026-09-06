#include "network/server.hpp"

namespace mc::net {

Server::Server(asio::io_context& io_context, uint16_t port)
    : acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {}

void Server::start() {
    do_accept();
}

void Server::stop() {
    acceptor_.close();
    // Copy first: Connection::stop() fires the disconnect handler, which
    // erases from connections_ — mutating while iterating would be UB.
    auto conns = connections_;
    connections_.clear();
    for (auto& conn : conns) {
        conn->stop();
    }
}

void Server::broadcast(const Packet& packet) {
    for (auto& conn : connections_) {
        conn->send_packet(packet);
    }
}

void Server::do_accept() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                auto conn = std::make_shared<Connection>(std::move(socket));
                connections_.insert(conn);

                conn->set_packet_handler(
                    [this](std::shared_ptr<Connection> c, std::shared_ptr<Packet> p) {
                        if (on_packet_) on_packet_(std::move(c), std::move(p));
                    }
                );

                conn->set_disconnect_handler(
                    [this](std::shared_ptr<Connection> c) {
                        connections_.erase(c);
                        if (on_disconnect_) on_disconnect_(c);
                    }
                );

                if (on_connect_) on_connect_(conn);
                conn->start();
            }
            // Keep accepting
            if (acceptor_.is_open()) {
                do_accept();
            }
        });
}

} // namespace mc::net
