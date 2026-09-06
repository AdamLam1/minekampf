#include "network/server_session.hpp"

#include "core/config.hpp"
#include "core/logger.hpp"
#include "gameplay/game.hpp"
#include "gameplay/block_interaction.hpp"
#include "gameplay/player.hpp"
#include "network/chunk_codec.hpp"
#include "network/connection.hpp"
#include "network/server.hpp"
#include "world/world.hpp"

#include <asio.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mc {
using net::BlockUpdatesPacket;
using net::ChatBroadcastPacket;
using net::ChatMessagePacket;
using net::ChunkDataPacket;
using net::ClientReadyPacket;
using net::Connection;
using net::DespawnPlayerPacket;
using net::PacketId;
using net::PLAYER_FLAG_ON_GROUND;
using net::PLAYER_FLAG_SNEAKING;
using net::PLAYER_FLAG_SPRINTING;
using net::PLAYER_FLAG_SWINGING;
using net::MP_PROTOCOL_VERSION;
using net::MP_MAX_PLAYERS;
using net::unpack_block_x;
using net::unpack_block_z;
using net::DisconnectPacket;
using net::HandshakePacket;
using net::KeepAlivePacket;
using net::LoginAcceptedPacket;
using net::LoginStartPacket;
using net::Packet;
using net::PlayerActionPacket;
using net::PlayerActionType;
using net::PlayerMovePacket;
using net::PlayerStatesPacket;
using net::PlayerStateEntry;
using net::RequestChunksPacket;
using net::SpawnPlayerPacket;
using net::TimeSyncPacket;

namespace {
constexpr int kChunkSendBudgetPerTick = 2;  // serialized chunks per tick/player
constexpr int kChunkStreamQueueCap = 4096;  // safety cap on a player's queue
constexpr int64_t kKeepAliveEveryTicks = 40;
constexpr int64_t kTimeSyncEveryTicks = 100;

uint64_t pack_chunk_key(int32_t cx, int32_t cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint32_t>(cz);
}

uint8_t player_flags(bool on_ground, bool sneaking, bool sprinting, bool swinging) {
    uint8_t f = 0;
    if (on_ground) f |= PLAYER_FLAG_ON_GROUND;
    if (sneaking) f |= PLAYER_FLAG_SNEAKING;
    if (sprinting) f |= PLAYER_FLAG_SPRINTING;
    if (swinging) f |= PLAYER_FLAG_SWINGING;
    return f;
}
} // namespace

bool ServerSession::ServerPlayer::has_chunk(int32_t cx, int32_t cz) const {
    return sent_chunks.contains(pack_chunk_key(cx, cz));
}

ServerSession::ServerSession() = default;

ServerSession::~ServerSession() {
    stop();
}

bool ServerSession::start(uint16_t port, Game* game) {
    if (running_) return true;
    game_ = game;

    io_ = new asio::io_context();
    try {
        server_ = std::make_unique<net::Server>(*io_, port);
    } catch (const std::exception& e) {
        MC_LOG_ERROR("ServerSession: cannot bind port {}: {}", port, e.what());
        delete io_;
        io_ = nullptr;
        server_.reset();
        return false;
    }

    server_->set_packet_received_handler(
        [this](std::shared_ptr<net::Connection> conn, std::shared_ptr<net::Packet> pkt) {
            Inbound in;
            in.conn = std::move(conn);
            in.packet = std::move(pkt);
            queue_inbound(std::move(in));
        });
    server_->set_client_connected_handler(
        [this](std::shared_ptr<net::Connection> conn) {
            Inbound in;
            in.conn = std::move(conn);
            in.connected = true;
            queue_inbound(std::move(in));
        });
    server_->set_client_disconnected_handler(
        [this](std::shared_ptr<net::Connection> conn) {
            Inbound in;
            in.conn = std::move(conn);
            in.connected = false;
            queue_inbound(std::move(in));
        });

    players_.clear();
    next_id_ = 1;
    ticks_since_start_ = 0;
    pending_block_updates_.clear();

    server_->start();
    io_thread_ = std::thread([this] {
        try {
            io_->run();
        } catch (const std::exception& e) {
            MC_LOG_ERROR("ServerSession: io thread died: {}", e.what());
        }
    });
    running_ = true;
    port_ = port;
    MC_LOG_INFO("ServerSession: listening on port {} (protocol v{})", port, MP_PROTOCOL_VERSION);
    return true;
}

void ServerSession::stop() {
    if (!running_ && !io_) return;
    running_ = false;

    if (io_) {
        io_->stop();
    }
    if (server_) {
        server_->stop();
        server_.reset();
    }
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    delete io_;
    io_ = nullptr;
    players_.clear();
    MC_LOG_INFO("ServerSession: stopped");
}

void ServerSession::shutdown_with_message(const std::string& reason) {
    DisconnectPacket d;
    d.reason = reason;
    for (auto& p : players_) {
        if (p.conn) p.conn->send_packet(d);
    }
    stop();
}

int ServerSession::player_count() const {
    int n = 0;
    for (const auto& p : players_) {
        if (p.logged_in) ++n;
    }
    return n;
}

void ServerSession::queue_inbound(Inbound in) {
    std::lock_guard<std::mutex> lock(inbox_mutex_);
    inbox_.push_back(std::move(in));
}

int ServerSession::next_player_id() {
    // Find the lowest free id in [1, 255]; ids are small so the PlayerStates
    // snapshot stays cheap.
    for (int32_t candidate = 1; candidate < 256; ++candidate) {
        bool taken = false;
        for (const auto& p : players_) {
            if (p.id == candidate) taken = true;
        }
        if (!taken) return candidate;
    }
    return -1;
}

ServerSession::ServerPlayer* ServerSession::player_for(const std::shared_ptr<Connection>& conn) {
    for (auto& p : players_) {
        if (p.conn == conn) return &p;
    }
    return nullptr;
}

void ServerSession::send_to(ServerPlayer& p, const Packet& pkt) {
    if (p.conn) p.conn->send_packet(pkt);
}

void ServerSession::broadcast(const net::Packet& pkt) {
    for (auto& p : players_) {
        if (p.logged_in && p.conn) p.conn->send_packet(pkt);
    }
}

void ServerSession::drain_events() {
    if (!running_) return;
    std::vector<Inbound> batch;
    {
        std::lock_guard<std::mutex> lock(inbox_mutex_);
        batch.swap(inbox_);
    }
    for (auto& in : batch) {
        if (!in.packet) {
            if (in.connected) {
                // Fresh connection: create a placeholder record until login.
                ServerPlayer p;
                p.conn = in.conn;
                players_.push_back(std::move(p));
            } else {
                handle_disconnect(in.conn);
            }
            continue;
        }

        ServerPlayer* p = player_for(in.conn);
        if (!p) continue;
        switch (in.packet->packet_id()) {
            case static_cast<int32_t>(PacketId::Handshake):
                handle_handshake(*p, static_cast<const HandshakePacket&>(*in.packet));
                break;
            case static_cast<int32_t>(PacketId::LoginStart):
                handle_login_start(*p, static_cast<const LoginStartPacket&>(*in.packet));
                break;
            case static_cast<int32_t>(PacketId::ClientReady):
                handle_client_ready(*p);
                break;
            case static_cast<int32_t>(PacketId::RequestChunks):
                handle_request_chunks(*p, static_cast<const RequestChunksPacket&>(*in.packet));
                break;
            case static_cast<int32_t>(PacketId::PlayerMove):
                handle_player_move(*p, static_cast<const PlayerMovePacket&>(*in.packet));
                break;
            case static_cast<int32_t>(PacketId::PlayerAction):
                handle_player_action(*p, static_cast<const PlayerActionPacket&>(*in.packet));
                break;
            case static_cast<int32_t>(PacketId::ChatMessage):
                handle_chat(*p, static_cast<const ChatMessagePacket&>(*in.packet));
                break;
            default:
                break; // S→C ids arriving here mean a broken client; ignore.
        }
    }
}

void ServerSession::handle_handshake(ServerPlayer& p, const HandshakePacket& pkt) {
    if (pkt.protocol_version != MP_PROTOCOL_VERSION) {
        DisconnectPacket d;
        d.reason = "Niezgodna wersja protokolu (server v" + std::to_string(MP_PROTOCOL_VERSION) +
                   ", klient v" + std::to_string(pkt.protocol_version) + ")";
        send_to(p, d);
        if (p.conn) p.conn->stop();
    }
}

void ServerSession::handle_login_start(ServerPlayer& p, const LoginStartPacket& pkt) {
    if (p.logged_in) return;
    DisconnectPacket d;
    if (static_cast<int>(players_.size()) - 1 >= MP_MAX_PLAYERS) { // -1: this placeholder
        d.reason = "Serwer pelny";
        send_to(p, d);
        if (p.conn) p.conn->stop();
        return;
    }

    int32_t id = next_player_id();
    if (id < 0) {
        d.reason = "Serwer pelny";
        send_to(p, d);
        if (p.conn) p.conn->stop();
        return;
    }

    // Deduplicate names so chat and the player list stay unambiguous.
    std::string name = pkt.username.empty() ? "Gracz" : pkt.username;
    int suffix = 2;
    bool clash = true;
    while (clash) {
        clash = false;
        for (const auto& o : players_) {
            if (&o != &p && o.name == name) clash = true;
        }
        if (clash) name = pkt.username + std::to_string(suffix++);
    }

    p.id = id;
    p.name = name;
    p.logged_in = true;
    p.ready = false;

    // Spawn at the host's current position (co-op start).
    const Vec3& host_pos = game_->mp_host_player().pos;
    p.x = host_pos.x;
    p.y = host_pos.y;
    p.z = host_pos.z;

    LoginAcceptedPacket acc;
    acc.player_id = p.id;
    acc.seed = game_->mp_world() ? game_->mp_world()->seed : 0;
    acc.game_mode = static_cast<int32_t>(game_->mp_game_mode());
    acc.time_of_day = game_->mp_time_of_day();
    acc.spawn_x = host_pos.x;
    acc.spawn_y = host_pos.y;
    acc.spawn_z = host_pos.z;
    send_to(p, acc);

    // Tell the newcomer about everyone already here, and everyone here about
    // the newcomer.
    send_player_list_to(p);
    SpawnPlayerPacket sp;
    sp.player_id = p.id;
    sp.name = p.name;
    sp.x = p.x;
    sp.y = p.y;
    sp.z = p.z;
    for (auto& o : players_) {
        if (&o == &p || !o.logged_in || !o.conn) continue;
        o.conn->send_packet(sp);
    }
    broadcast_system_chat(p.name + " dolaczyl do gry");
    MC_LOG_INFO("ServerSession: '{}' logged in as player {}", name, id);
}

void ServerSession::send_player_list_to(ServerPlayer& p) {
    // The host itself (id 0) first, so the client renders it like any other
    // player instead of synthesizing a name from a bare snapshot.
    {
        SpawnPlayerPacket host;
        host.player_id = 0;
        host.name = "Host";
        const Player& lp = game_->mp_host_player();
        host.x = lp.pos.x;
        host.y = lp.pos.y;
        host.z = lp.pos.z;
        host.yaw = lp.yaw;
        host.pitch = lp.pitch;
        if (p.conn) p.conn->send_packet(host);
    }
    for (const auto& o : players_) {
        if (&o == &p || !o.logged_in || !o.conn) continue;
        SpawnPlayerPacket sp;
        sp.player_id = o.id;
        sp.name = o.name;
        sp.x = o.x;
        sp.y = o.y;
        sp.z = o.z;
        sp.yaw = o.yaw;
        sp.pitch = o.pitch;
        if (p.conn) p.conn->send_packet(sp);
    }
}

void ServerSession::handle_client_ready(ServerPlayer& p) {
    p.ready = true;
    MC_LOG_INFO("ServerSession: player {} ({}) ready", p.id, p.name);
}

void ServerSession::handle_request_chunks(ServerPlayer& p, const RequestChunksPacket& pkt) {
    p.last_center_x = pkt.center_x;
    p.last_center_z = pkt.center_z;
    p.last_radius = std::clamp(pkt.radius, 2, 12);

    World* w = game_->mp_world();
    if (!w) return;
    const int r = p.last_radius;
    // Rebuild the send queue: every in-bounds disc chunk the host has loaded
    // and the client has not acked yet, nearest-last so terrain under the
    // player arrives first.
    p.send_queue.clear();
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dz * dz > r * r) continue;
            int32_t cx = pkt.center_x + dx;
            int32_t cz = pkt.center_z + dz;
            if (!in_world_bounds(ChunkPos{cx, cz})) continue;
            if (p.has_chunk(cx, cz)) continue;
            if (!w->has_chunk(ChunkPos{cx, cz})) continue;
            p.send_queue.emplace_back(cx, cz);
        }
    }
    if (p.send_queue.size() > kChunkStreamQueueCap) {
        p.send_queue.resize(kChunkStreamQueueCap);
    }
}

void ServerSession::handle_player_move(ServerPlayer& p, const PlayerMovePacket& pkt) {
    if (!p.logged_in) return;
    if (!std::isfinite(pkt.x) || !std::isfinite(pkt.y) || !std::isfinite(pkt.z)) return;
    p.prev_x = p.x;
    p.prev_y = p.y;
    p.prev_z = p.z;
    p.x = pkt.x;
    p.y = pkt.y;
    p.z = pkt.z;
    p.yaw = pkt.yaw;
    p.pitch = pkt.pitch;
    p.flags = pkt.flags;
}

void ServerSession::handle_player_action(ServerPlayer& p, const PlayerActionPacket& pkt) {
    if (!p.logged_in) return;
    const BlockPos pos{pkt.x, pkt.y, pkt.z};
    Vec3 player_pos{p.x, p.y, p.z};
    switch (static_cast<PlayerActionType>(pkt.action)) {
        case PlayerActionType::BreakBlock: {
            std::string err;
            if (!game_->mp_apply_remote_break(pos, player_pos, &err)) {
                MC_LOG_WARN("ServerSession: break by {} rejected at ({}, {}, {}): {}",
                            p.name, pos.x, pos.y, pos.z, err);
            }
            break;
        }
        case PlayerActionType::PlaceBlock: {
            std::string err;
            if (!game_->mp_apply_remote_place(pos, static_cast<BlockId>(pkt.block_id),
                                              player_pos, &err)) {
                MC_LOG_WARN("ServerSession: place by {} rejected at ({}, {}, {}): {}",
                            p.name, pos.x, pos.y, pos.z, err);
            }
            break;
        }
        default:
            break;
    }
}

void ServerSession::handle_chat(ServerPlayer& p, const ChatMessagePacket& pkt) {
    if (!p.logged_in) return;
    if (!pkt.text.empty() && pkt.text[0] == '/') {
        // Remote commands run in a future phase; acknowledge so testers see
        // the round trip.
        ChatBroadcastPacket reply;
        reply.from_name = "Serwer";
        reply.text = "Komendy dzialaja tylko u hosta (MVP).";
        send_to(p, reply);
        return;
    }
    ChatBroadcastPacket out;
    out.from_name = p.name;
    out.text = pkt.text;
    broadcast(out);
    game_->mp_chat_from_remote(p.name, pkt.text);
}

void ServerSession::handle_disconnect(const std::shared_ptr<Connection>& conn) {
    auto it = std::find_if(players_.begin(), players_.end(),
                           [&](const ServerPlayer& p) { return p.conn == conn; });
    if (it == players_.end()) return;
    std::string name = it->name;
    int32_t id = it->id;
    bool was_logged_in = it->logged_in;
    players_.erase(it);
    if (was_logged_in) {
        DespawnPlayerPacket d;
        d.player_id = id;
        broadcast(d);
        broadcast_system_chat(name + " opuscil gre");
        MC_LOG_INFO("ServerSession: '{}' disconnected", name);
    }
}

void ServerSession::record_block_change(int64_t packed_pos, int32_t block_id) {
    pending_block_updates_.emplace_back(packed_pos, block_id);
}

void ServerSession::broadcast_host_chat(const std::string& text) {
    ChatBroadcastPacket out;
    out.from_name = "Host";
    out.text = text;
    broadcast(out);
}

void ServerSession::broadcast_system_chat(const std::string& text) {
    ChatBroadcastPacket out;
    out.from_name = "Serwer";
    out.text = text;
    broadcast(out);
}

void ServerSession::stream_chunks(ServerPlayer& p) {
    World* w = game_->mp_world();
    if (!w) return;
    int sent = 0;
    while (sent < kChunkSendBudgetPerTick && !p.send_queue.empty()) {
        auto [cx, cz] = p.send_queue.back();
        p.send_queue.pop_back();
        if (p.has_chunk(cx, cz)) continue;
        const Chunk* chunk = w->get_chunk(ChunkPos{cx, cz});
        if (!chunk) continue; // unloaded since request; a later request catches it

        std::vector<uint8_t> payload = zlib_compress(serialize_chunk(*chunk));
        if (payload.empty()) continue;
        ChunkDataPacket pkt;
        pkt.chunk_x = cx;
        pkt.chunk_z = cz;
        pkt.data = std::move(payload);
        send_to(p, pkt);
        p.sent_chunks.insert(pack_chunk_key(cx, cz));
        ++sent;
    }
}

void ServerSession::tick_broadcast() {
    if (!running_) return;
    ++ticks_since_start_;

    // 1. Chunk streaming (budgeted) — before block updates so fresh chunks
    //    already contain any changes applied this tick.
    for (auto& p : players_) {
        if (p.logged_in && p.ready) stream_chunks(p);
    }

    // 2. Block updates batch → only to clients holding the chunk.
    if (!pending_block_updates_.empty()) {
        BlockUpdatesPacket updates;
        updates.updates = std::move(pending_block_updates_);
        pending_block_updates_.clear();
        for (auto& p : players_) {
            if (!p.logged_in || !p.conn) continue;
            bool any = false;
            for (const auto& [pos, block] : updates.updates) {
                // packed positions are BLOCK coords; interest management is
                // keyed by CHUNK coords (floor division via >> 4).
                if (p.has_chunk(unpack_block_x(pos) >> 4, unpack_block_z(pos) >> 4)) {
                    any = true;
                    break;
                }
            }
            if (any) p.conn->send_packet(updates);
        }
    }

    // 3. Player snapshot (everyone, including the host).
    PlayerStatesPacket states;
    states.states.reserve(players_.size() + 1);
    {
        PlayerStateEntry host;
        host.player_id = 0;
        const Player& lp = game_->mp_host_player();
        host.x = lp.pos.x;
        host.y = lp.pos.y;
        host.z = lp.pos.z;
        host.yaw = lp.yaw;
        host.pitch = lp.pitch;
        host.flags = player_flags(lp.on_ground, lp.sneaking, lp.sprinting, lp.is_swinging);
        states.states.push_back(host);
    }
    for (const auto& p : players_) {
        if (!p.logged_in) continue;
        PlayerStateEntry e;
        e.player_id = p.id;
        e.x = p.x;
        e.y = p.y;
        e.z = p.z;
        e.yaw = p.yaw;
        e.pitch = p.pitch;
        e.flags = p.flags;
        states.states.push_back(e);
    }
    broadcast(states);

    // 4. Slow-periodic sync.
    if (ticks_since_start_ % kTimeSyncEveryTicks == 0) {
        TimeSyncPacket t;
        t.time_of_day = game_->mp_time_of_day();
        broadcast(t);
    }
    if (ticks_since_start_ % kKeepAliveEveryTicks == 0) {
        KeepAlivePacket ka;
        ka.keep_alive_id = ++keepalive_counter_;
        broadcast(ka);
    }
}

} // namespace mc
