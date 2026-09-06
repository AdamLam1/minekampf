#include "network/client.hpp"

#include <array>
#include <iostream>

namespace mc::net {

Client::Client(asio::io_context& io_context)
    : io_context_(io_context), resolver_(io_context) {}

Client::~Client() {
    disconnect();
}

void Client::connect(const std::string& host, const std::string& port) {
    // Numeric addresses skip the resolver: its internal worker thread proved
    // fragile in-process (EDEADLK from _beginthreadex inside io_context::run),
    // and for "IP:port" gameplay it is pure overhead. Hostnames still take
    // the async_resolve path.
    asio::error_code addr_ec;
    asio::ip::address address = asio::ip::make_address(host, addr_ec);
    if (!addr_ec) {
        auto socket_ptr = std::make_shared<asio::ip::tcp::socket>(io_context_);
        std::array<asio::ip::tcp::endpoint, 1> endpoints{
            asio::ip::tcp::endpoint(address, static_cast<uint16_t>(std::stoi(port)))};
        asio::async_connect(*socket_ptr, endpoints,
            [this, socket_ptr](std::error_code ec, const asio::ip::tcp::endpoint&) {
                handle_connected(ec, std::move(socket_ptr));
            });
        return;
    }

    resolver_.async_resolve(host, port,
        [this](std::error_code ec, asio::ip::tcp::resolver::results_type results) {
            if (!ec) {
                auto socket_ptr = std::make_shared<asio::ip::tcp::socket>(io_context_);

                asio::async_connect(*socket_ptr, results,
                    [this, socket_ptr](std::error_code ec, const asio::ip::tcp::endpoint&) {
                        handle_connected(ec, std::move(socket_ptr));
                    });
            } else {
                std::cerr << "Resolve error: " << ec.message() << "\n";
                if (on_disconnect_) on_disconnect_();
            }
        });
}

void Client::handle_connected(std::error_code ec,
                              std::shared_ptr<asio::ip::tcp::socket> socket_ptr) {
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
        if (on_disconnect_) on_disconnect_();
    }
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
