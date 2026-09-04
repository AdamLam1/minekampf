#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <queue>
#include <mutex>

namespace mc {

class Game;

struct AutomationCommand {
    std::string action;
    std::string payload_json;
};

// In-engine non-blocking TCP socket Automation Server.
// Allows AI agents and external scripts to control the player, query state,
// place/break blocks, run console commands, and trigger screen frame captures.
class AutomationServer {
public:
    AutomationServer() = default;
    ~AutomationServer();

    bool start(uint16_t port);
    void stop();
    
    // Call once per frame on the main engine thread to process queued commands.
    void update(Game& game);

    // Queue a response JSON string to be sent back to the client
    void send_response(const std::string& json_str);

private:
    uint16_t port_ = 0;
    bool running_ = false;

    // Platform socket handle
    uintptr_t server_socket_ = ~0u;
    uintptr_t client_socket_ = ~0u;

    std::mutex queue_mutex_;
    std::queue<std::string> incoming_requests_;
};

} // namespace mc
