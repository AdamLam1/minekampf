#include "network/client_session.hpp"

#include "core/logger.hpp"
#include "gameplay/game.hpp"
#include "network/client.hpp"
#include "network/connection.hpp"

#include <asio.hpp>


namespace mc {
using net::BlockUpdatesPacket;
using net::ChatBroadcastPacket;
using net::ChatMessagePacket;
using net::ChunkDataPacket;
using net::ClientReadyPacket;
using net::DespawnPlayerPacket;
using net::DisconnectPacket;
using net::HandshakePacket;
using net::KeepAlivePacket;
using net::LoginAcceptedPacket;
using net::LoginStartPacket;
using net::Packet;
using net::PacketId;
using net::PlayerActionType;
using net::PlayerActionPacket;
using net::PlayerMovePacket;
using net::PlayerStatesPacket;
using net::RequestChunksPacket;
using net::SpawnPlayerPacket;
using net::TimeSyncPacket;

ClientSession::ClientSession() = default;

ClientSession::~ClientSession() {
    disconnect();
}

void ClientSession::connect(const std::string& host, uint16_t port, const std::string& username,
                            Game* game) {
    if (running_) return;
    game_ = game;
    username_ = username;
    my_id_ = -1;
    in_game_ = false;
    status_ = "Łączenie z " + host + "...";

    io_ = new asio::io_context();
    client_ = std::make_unique<net::Client>(*io_);
    client_->set_connected_handler([this] {
        // Wire is up: negotiate protocol and log in. (io thread; send_packet
        // is thread-safe.)
        HandshakePacket hs;
        hs.username = username_;
        client_->send_packet(hs);
        LoginStartPacket ls;
        ls.username = username_;
        client_->send_packet(ls);
        status_ = "Logowanie...";
    });
    client_->set_disconnected_handler([this] {
        Inbound in;
        in.connected = false;
        queue_inbound(std::move(in));
    });
    client_->set_packet_received_handler(
        [this](std::shared_ptr<Packet> pkt) {
            Inbound in;
            in.connected = true;
            in.packet = std::move(pkt);
            queue_inbound(std::move(in));
        });

    client_->connect(host, std::to_string(port));
    MC_LOG_INFO("ClientSession: connect posted (main-thread io pumping)");
    running_ = true;
}

void ClientSession::disconnect(const std::string& reason) {
    if (!running_ && !io_) return;
    running_ = false;
    in_game_ = false;

    if (client_) {
        client_->disconnect();
        client_.reset();
    }
    delete io_;
    io_ = nullptr;
    if (!reason.empty()) {
        MC_LOG_INFO("ClientSession: disconnected ({})", reason);
    }
}

void ClientSession::queue_inbound(Inbound in) {
    std::lock_guard<std::mutex> lock(inbox_mutex_);
    inbox_.push_back(std::move(in));
}

void ClientSession::drain_events() {
    if (!running_ || !io_) return;
    // Execute ready socket completions on the calling (main) thread.
    try {
        io_->poll();
    } catch (const std::exception& e) {
        MC_LOG_ERROR("ClientSession: io poll threw: {}", e.what());
        return;
    }
    if (io_->stopped()) io_->restart();
    std::vector<Inbound> batch;
    {
        std::lock_guard<std::mutex> lock(inbox_mutex_);
        batch.swap(inbox_);
    }
    for (auto& in : batch) {
        if (!in.packet) {
            if (!in.connected) {
                // Server dropped the connection.
                status_ = "Rozłączono";
                game_->mp_client_disconnected("Połączenie z serwerem zostało zerwane.");
            }
            continue;
        }
        handle_packet(in.packet);
    }
}

void ClientSession::handle_packet(const std::shared_ptr<Packet>& pkt) {
    switch (pkt->packet_id()) {
        case static_cast<int32_t>(PacketId::LoginAccepted): {
            const auto& acc = static_cast<const LoginAcceptedPacket&>(*pkt);
            my_id_ = acc.player_id;
            in_game_ = true;
            status_ = "W grze";
            game_->mp_client_accepted(acc);
            break;
        }
        case static_cast<int32_t>(PacketId::ChunkData):
            game_->mp_client_chunk(static_cast<const ChunkDataPacket&>(*pkt));
            break;
        case static_cast<int32_t>(PacketId::BlockUpdates):
            game_->mp_client_blocks(static_cast<const BlockUpdatesPacket&>(*pkt));
            break;
        case static_cast<int32_t>(PacketId::SpawnPlayer):
            game_->mp_client_spawn_player(static_cast<const SpawnPlayerPacket&>(*pkt));
            break;
        case static_cast<int32_t>(PacketId::DespawnPlayer):
            game_->mp_client_despawn_player(static_cast<const DespawnPlayerPacket&>(*pkt));
            break;
        case static_cast<int32_t>(PacketId::PlayerStates):
            game_->mp_client_states(static_cast<const PlayerStatesPacket&>(*pkt));
            break;
        case static_cast<int32_t>(PacketId::TimeSync):
            game_->mp_set_time_of_day(static_cast<const TimeSyncPacket&>(*pkt).time_of_day);
            break;
        case static_cast<int32_t>(PacketId::ChatBroadcast): {
            const auto& c = static_cast<const ChatBroadcastPacket&>(*pkt);
            game_->mp_chat_from_remote(c.from_name, c.text);
            break;
        }
        case static_cast<int32_t>(PacketId::Disconnect): {
            const auto& d = static_cast<const DisconnectPacket&>(*pkt);
            status_ = "Odrzucono: " + d.reason;
            game_->mp_client_disconnected(d.reason);
            break;
        }
        case static_cast<int32_t>(PacketId::KeepAlive):
            // Transport-level liveness; nothing to do beyond keeping the
            // connection's read loop draining (asio handles the rest).
            break;
        default:
            break;
    }
}

void ClientSession::send_ready() {
    if (client_) client_->send_packet(ClientReadyPacket{});
}

void ClientSession::send_request_chunks(int32_t center_x, int32_t center_z, int32_t radius) {
    RequestChunksPacket pkt;
    pkt.center_x = center_x;
    pkt.center_z = center_z;
    pkt.radius = radius;
    if (client_) client_->send_packet(pkt);
}

void ClientSession::send_move(const PlayerMovePacket& move) {
    if (client_ && in_game_) client_->send_packet(move);
}

void ClientSession::send_action(PlayerActionType action, int32_t x, int32_t y, int32_t z,
                                int32_t block_id) {
    if (client_ && in_game_) {
        PlayerActionPacket pkt;
        pkt.action = static_cast<int32_t>(action);
        pkt.x = x;
        pkt.y = y;
        pkt.z = z;
        pkt.block_id = block_id;
        client_->send_packet(pkt);
    }
}

void ClientSession::send_chat(const std::string& text) {
    if (client_ && in_game_) {
        ChatMessagePacket pkt;
        pkt.text = text;
        client_->send_packet(pkt);
    }
}

} // namespace mc
