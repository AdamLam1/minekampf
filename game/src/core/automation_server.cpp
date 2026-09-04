#include "core/automation_server.hpp"
#include "gameplay/game.hpp"
#include "core/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace mc {

namespace {
#ifdef _WIN32
using SocketType = SOCKET;
constexpr SocketType INVALID_SOCK = INVALID_SOCKET;
#else
using SocketType = int;
constexpr SocketType INVALID_SOCK = -1;
#endif

void set_non_blocking(SocketType sock) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
}
} // namespace

AutomationServer::~AutomationServer() {
    stop();
}

bool AutomationServer::start(uint16_t port) {
    port_ = port;
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MC_LOG_ERROR("AutomationServer: WSAStartup failed");
        return false;
    }
#endif

    SocketType server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCK) {
        MC_LOG_ERROR("AutomationServer: Failed to create socket");
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    set_non_blocking(server_fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        MC_LOG_ERROR("AutomationServer: Failed to bind port {}", port);
#ifdef _WIN32
        closesocket(server_fd);
#else
        close(server_fd);
#endif
        return false;
    }

    if (listen(server_fd, 4) < 0) {
        MC_LOG_ERROR("AutomationServer: Failed to listen on port {}", port);
        return false;
    }

    server_socket_ = static_cast<uintptr_t>(server_fd);
    running_ = true;
    MC_LOG_INFO("AutomationServer: Listening for AI commands on 127.0.0.1:{}", port);
    return true;
}

void AutomationServer::stop() {
    if (!running_) return;
    running_ = false;

    if (client_socket_ != ~0u) {
        SocketType c = static_cast<SocketType>(client_socket_);
#ifdef _WIN32
        closesocket(c);
#else
        close(c);
#endif
        client_socket_ = ~0u;
    }

    if (server_socket_ != ~0u) {
        SocketType s = static_cast<SocketType>(server_socket_);
#ifdef _WIN32
        closesocket(s);
        WSACleanup();
#else
        close(s);
#endif
        server_socket_ = ~0u;
    }
}

void AutomationServer::send_response(const std::string& json_str) {
    if (client_socket_ == ~0u) return;
    SocketType client_fd = static_cast<SocketType>(client_socket_);
    std::string msg = json_str + "\n";
    send(client_fd, msg.c_str(), static_cast<int>(msg.length()), 0);
}

void AutomationServer::update(Game& game) {
    if (!running_) return;

    SocketType server_fd = static_cast<SocketType>(server_socket_);

    // Accept new client if none connected
    if (client_socket_ == ~0u) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        SocketType new_client = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (new_client != INVALID_SOCK) {
            set_non_blocking(new_client);
            client_socket_ = static_cast<uintptr_t>(new_client);
            MC_LOG_INFO("AutomationServer: AI Client connected");
            send_response("{\"status\":\"connected\",\"msg\":\"Minekampf Automation API Ready\"}");
        }
    }

    // Read pending commands from client
    if (client_socket_ != ~0u) {
        SocketType client_fd = static_cast<SocketType>(client_socket_);
        char buffer[2048];
        int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            std::string req(buffer);

            // Minimal JSON field extraction (requests come from trusted local testers).
            auto find_str_field = [&](const std::string& key) -> std::string {
                std::string pat = "\"" + key + "\"";
                auto p = req.find(pat);
                if (p == std::string::npos) return {};
                p = req.find(':', p + pat.size());
                if (p == std::string::npos) return {};
                ++p;
                while (p < req.size() && (req[p] == ' ' || req[p] == '\t')) ++p;
                if (p >= req.size() || req[p] != '"') return {};
                ++p;
                std::string out;
                while (p < req.size() && req[p] != '"') {
                    if (req[p] == '\\' && p + 1 < req.size()) ++p;
                    out += req[p];
                    ++p;
                }
                return out;
            };
            auto find_num_field = [&](const std::string& key, bool& ok) -> double {
                ok = false;
                std::string pat = "\"" + key + "\"";
                auto p = req.find(pat);
                if (p == std::string::npos) return 0.0;
                p = req.find(':', p + pat.size());
                if (p == std::string::npos) return 0.0;
                ++p;
                while (p < req.size() && (req[p] == ' ' || req[p] == '\t')) ++p;
                char* end = nullptr;
                double v = std::strtod(req.c_str() + p, &end);
                if (end == req.c_str() + p) return 0.0;
                ok = true;
                return v;
            };

            std::string cmd = find_str_field("cmd");
            if (cmd.empty()) {
                // Backward-compatible plain-text requests.
                if (req.find("get_state") != std::string::npos) cmd = "get_state";
                else if (req.find("screenshot") != std::string::npos) cmd = "screenshot";
            }

            if (cmd == "get_state") {
                send_response(game.automation_state_json());
            } else if (cmd == "get_perf") {
                bool has_reset = false;
                double reset = find_num_field("reset", has_reset);
                send_response(game.automation_perf_json(has_reset && reset != 0.0));
            } else if (cmd == "screenshot") {
                std::string file = find_str_field("filename");
                if (file.empty()) file = "automation_frame.bmp";
                // Resolve relative paths into the game's screenshot directory.
                if (file.find(':') == std::string::npos && file.front() != '/' && file.front() != '\\') {
                    file = Game::screenshot_dir() + "/" + file;
                }
                // Forward slashes keep the JSON response valid on Windows.
                for (char& c : file) { if (c == '\\') c = '/'; }
                game.queue_frame_capture(file);
                send_response("{\"status\":\"ok\",\"file\":\"" + file + "\"}");
            } else if (cmd == "ui") {
                std::string panel = find_str_field("panel");
                if (panel == "settings") {
                    game.automation_open_settings();
                    send_response("{\"status\":\"ok\"}");
                } else {
                    send_response("{\"status\":\"error\",\"msg\":\"unknown panel\"}");
                }
            } else if (cmd == "exec") {
                std::string command = find_str_field("command");
                if (command.empty()) {
                    send_response("{\"status\":\"error\",\"msg\":\"missing command\"}");
                } else {
                    send_response(game.automation_exec(command));
                }
            } else if (cmd == "look") {
                bool has_yaw = false, has_pitch = false;
                double yaw = find_num_field("yaw", has_yaw);
                double pitch = find_num_field("pitch", has_pitch);
                // Angles are accepted in degrees for tester convenience.
                constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
                if (!has_yaw && !has_pitch) {
                    send_response("{\"status\":\"error\",\"msg\":\"missing yaw/pitch\"}");
                } else {
                    auto num_in = [&](const std::string& src, const std::string& k) -> double {
                        auto p = src.find("\"" + k + "\":");
                        if (p == std::string::npos) return 0.0;
                        return std::strtod(src.c_str() + p + k.size() + 3, nullptr);
                    };
                    float new_yaw = 0.0f, new_pitch = 0.0f;
                    if (has_yaw && has_pitch) {
                        new_yaw = static_cast<float>(yaw * kDegToRad);
                        new_pitch = static_cast<float>(pitch * kDegToRad);
                    } else {
                        // Partial update: keep the current angle for the missing axis.
                        std::string full = game.automation_state_json();
                        new_yaw = has_yaw ? static_cast<float>(yaw * kDegToRad)
                                          : static_cast<float>(num_in(full, "yaw"));
                        new_pitch = has_pitch ? static_cast<float>(pitch * kDegToRad)
                                              : static_cast<float>(num_in(full, "pitch"));
                    }
                    game.automation_look(new_yaw, new_pitch);
                    send_response("{\"status\":\"ok\"}");
                }
            } else {
                send_response("{\"status\":\"error\",\"msg\":\"unknown cmd\",\"received\":\"" + req + "\"}");
            }
        } else if (bytes_read == 0) {
            // Disconnected
#ifdef _WIN32
            closesocket(client_fd);
#else
            close(client_fd);
#endif
            client_socket_ = ~0u;
            MC_LOG_INFO("AutomationServer: AI Client disconnected");
        }
    }
}

} // namespace mc
