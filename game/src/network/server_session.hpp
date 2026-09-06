#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "core/types.hpp"
#include "world/block.hpp"
#include "network/packet.hpp"

namespace asio {
class io_context;
}

namespace mc {

struct Chunk;
class Game;
class World;
struct BlockPos;

namespace net {
class Connection;
class Server;
}

// Listen-server session (host). Owns the asio acceptor + io thread; all
// gameplay state lives on the main thread and is driven by Game, which drains
// inbound events once per tick and asks for tick-end broadcasts.
//
// Threading contract (mirrors core/automation_server):
//  - io thread: accept/read/write + push decoded packets into the inbox only.
//  - main thread: drain_events() / tick_broadcast() / snapshot helpers.
class ServerSession {
public:
    ServerSession();
    ~ServerSession();

    bool start(uint16_t port, Game* game);
    void stop();
    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] uint16_t port() const { return port_; }
    [[nodiscard]] int player_count() const; // connected humans, excluding host

    // Process queued connection events + packets. Called once per tick on the
    // main thread.
    void drain_events();

    // Tick-end work: stream chunk queue (budgeted), broadcast player states
    // (host + remotes), pending block updates, time sync, keepalives.
    void tick_broadcast();

    // Block changes produced by host-side simulation this tick (also fed from
    // the Game-owned EventBus hook). Positions are pack_block_pos-packed.
    void record_block_change(int64_t packed_pos, int32_t block_id);

    // Broadcast a chat line announced by the host (already in host chat log).
    void broadcast_host_chat(const std::string& text);
    // System line visible to everyone (joins, leaves, kicks).
    void broadcast_system_chat(const std::string& text);

    // Kick everyone and stop (host leaving the world).
    void shutdown_with_message(const std::string& reason);

    // Iterate logged-in players (main thread). Fn(id, name, x, y, z, yaw, pitch, flags).
    template <typename Fn>
    void for_each_player(Fn&& fn) const {
        for (const auto& p : players_) {
            if (p.logged_in) fn(p.id, p.name, p.x, p.y, p.z, p.yaw, p.pitch, p.flags);
        }
    }

private:
    struct ServerPlayer {
        std::shared_ptr<net::Connection> conn;
        int32_t id = 0;
        std::string name;
        float x = 0, y = 100, z = 0;
        float prev_x = 0, prev_y = 100, prev_z = 0;
        float yaw = 0, pitch = 0;
        uint8_t flags = 0;
        bool logged_in = false; // LoginStart accepted
        bool ready = false;     // ClientReady received (world built client-side)

        // Interest management: chunks this client has already been sent +
        // the ordered queue of pending sends.
        std::unordered_set<uint64_t> sent_chunks; // packed (x << 32) | z
        std::vector<std::pair<int32_t, int32_t>> send_queue;
        int32_t last_center_x = INT32_MIN, last_center_z = INT32_MIN;
        int32_t last_radius = -1;

        [[nodiscard]] bool has_chunk(int32_t cx, int32_t cz) const; // implemented in cpp
    };

    struct Inbound {
        std::shared_ptr<net::Connection> conn;
        std::shared_ptr<net::Packet> packet; // null => connect/disconnect event
        bool connected = false;              // valid when packet == nullptr
    };

    void queue_inbound(Inbound in);
    ServerPlayer* player_for(const std::shared_ptr<net::Connection>& conn);
    void handle_handshake(ServerPlayer& p, const net::HandshakePacket& pkt);
    void handle_login_start(ServerPlayer& p, const net::LoginStartPacket& pkt);
    void handle_client_ready(ServerPlayer& p);
    void handle_request_chunks(ServerPlayer& p, const net::RequestChunksPacket& pkt);
    void handle_player_move(ServerPlayer& p, const net::PlayerMovePacket& pkt);
    void handle_player_action(ServerPlayer& p, const net::PlayerActionPacket& pkt);
    void handle_chat(ServerPlayer& p, const net::ChatMessagePacket& pkt);
    void handle_disconnect(const std::shared_ptr<net::Connection>& conn);

    void send_to(ServerPlayer& p, const net::Packet& pkt);
    void broadcast(const net::Packet& pkt);
    void send_player_list_to(ServerPlayer& p);
    void stream_chunks(ServerPlayer& p);
    [[nodiscard]] int next_player_id();

    asio::io_context* io_ = nullptr;      // owned; raw to avoid <asio.hpp> here
    std::unique_ptr<net::Server> server_;
    std::thread io_thread_;
    uint16_t port_ = 0;
    bool running_ = false;

    Game* game_ = nullptr;

    std::mutex inbox_mutex_;
    std::vector<Inbound> inbox_;

    std::vector<ServerPlayer> players_;
    int32_t next_id_ = 1;
    int64_t keepalive_counter_ = 0;
    int64_t ticks_since_start_ = 0;
    std::vector<std::pair<int64_t, int32_t>> pending_block_updates_;
};

} // namespace mc
