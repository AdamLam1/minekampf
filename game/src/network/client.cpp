#include "network/client.hpp"
#include <iostream>

namespace mc::net {

Client::Client(asio::io_context& io_context)
    : io_context_(io_context), resolver_(io_context) {}

Client::~Client() {
    disconnect();
}

void Client::connect(const std::string& host, const std::string& port) {
    resolver_.async_resolve(host, port,
        [this](std::error_code ec, asio::ip::tcp::resolver::results_type results) {
            if (!ec) {
                auto socket_ptr = std::make_shared<asio::ip::tcp::socket>(io_context_);

                asio::async_connect(*socket_ptr, results,
                    [this, socket_ptr](std::error_code ec, asio::ip::tcp::endpoint /*endpoint*/) {
                        if (!ec) {
                            connection_ = std::make_shared<Connection>(std::move(*socket_ptr));
                            
                            connection_->set_packet_handler(
                                [this](std::shared_ptr<Connection>, std::shared_ptr<Packet> p) {
                                    if (on_packet_) on_packet_(std::move(p));
                                }
                            );

                            connection_->set_disconnect_handler(
                                [this](std::shared_ptr<Connection>) {
                                    if (on_disconnect_) on_disconnect_();
                                    connection_.reset();
                                }
                            );

                            connection_->start();
                            if (on_connect_) on_connect_();
                        } else {
                            std::cerr << "Client connection error: " << ec.message() << "\n";
                        }
                    });
            } else {
                std::cerr << "Resolve error: " << ec.message() << "\n";
            }
        });
}

void Client::disconnect() {
    if (connection_) {
        connection_->stop();
        connection_.reset();
    }
}

void Client::send_packet(const Packet& packet) {
    if (connection_) {
        connection_->send_packet(packet);
    }
}

} // namespace mc::net
