#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "network/packet.hpp"

namespace asio {
class io_context;
}

namespace mc {

class Connection;
class Game;

namespace net {
class Client;
}

// Client-side multiplayer session. Connects to a listen server, performs the
// handshake/login, and shuttles decoded packets to the main thread where
// Game applies them (chunk payloads, block updates, player snapshots, chat).
//
// Threading: the asio context is pumped from the MAIN thread (drain_events
// calls io_context::poll each frame). The client's io work is a single
// low-rate connection, and keeping completions on the main thread sidesteps
// io_context's internal thread creation (which fails EDEADLK in this process)
// and matches the engine's single-threaded world mutation rule.
class ClientSession {
public:
    ClientSession();
    ~ClientSession();

    void connect(const std::string& host, uint16_t port, const std::string& username, Game* game);
    void disconnect(const std::string& reason = {});
    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] bool in_game() const { return in_game_; }
    [[nodiscard]] int my_player_id() const { return my_id_; }
    [[nodiscard]] const std::string& status() const { return status_; }

    // Drain connection events + packets. Main thread, once per frame/tick.
    void drain_events();

    // C→S sends (thread-safe through the connection write queue).
    void send_ready();
    void send_request_chunks(int32_t center_x, int32_t center_z, int32_t radius);
    void send_move(const net::PlayerMovePacket& move);
    void send_action(net::PlayerActionType action, int32_t x, int32_t y, int32_t z,
                     int32_t block_id);
    void send_chat(const std::string& text);

private:
    struct Inbound {
        std::shared_ptr<net::Packet> packet; // null => connection state change
        bool connected = false;
    };

    void queue_inbound(Inbound in);
    void handle_packet(const std::shared_ptr<net::Packet>& pkt);

    asio::io_context* io_ = nullptr; // owned; raw to avoid <asio.hpp> here
    std::unique_ptr<net::Client> client_;

    Game* game_ = nullptr;
    std::string username_;
    bool running_ = false;
    bool in_game_ = false;
    int my_id_ = -1;
    std::string status_ = "Rozłączono";

    std::mutex inbox_mutex_;
    std::vector<Inbound> inbox_;
};

} // namespace mc
