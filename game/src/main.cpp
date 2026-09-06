#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/logger.hpp"
#include "gameplay/game.hpp"
#include "gameplay/crafting.hpp"
#include "core/mod_manager.hpp"
#include "network/packet.hpp"

int main(int argc, char* argv[]) {
    bool screenshot_mode = false;
    bool auto_play = false;
    std::string screenshot_path;
    int wait_ticks = 500; // ~25 seconds at 20 Hz to allow all chunks to generate
    uint16_t automation_port = 0;
    bool host_mode = false;
    uint16_t host_port = mc::net::MP_DEFAULT_PORT;
    bool join_mode = false;
    std::string join_host;
    uint16_t join_port = mc::net::MP_DEFAULT_PORT;
    std::string join_name = "Gracz";

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0) {
            screenshot_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                screenshot_path = argv[++i];
            }
        } else if (std::strcmp(argv[i], "--auto-play") == 0) {
            auto_play = true;
        } else if (std::strcmp(argv[i], "--automation-port") == 0) {
            if (i + 1 < argc) automation_port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--ticks") == 0 || std::strcmp(argv[i], "--wait-ticks") == 0) {
            if (i + 1 < argc) wait_ticks = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--host") == 0) {
            // Headless-friendly hosting: pair with --auto-play to serve an
            // in-memory world, or let the player pick a world via the menu.
            host_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                host_port = static_cast<uint16_t>(std::atoi(argv[++i]));
            }
        } else if (std::strcmp(argv[i], "--join") == 0) {
            join_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') join_host = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                join_port = static_cast<uint16_t>(std::atoi(argv[++i]));
            }
            if (i + 1 < argc && argv[i + 1][0] != '-') join_name = argv[++i];
        } else if (std::strcmp(argv[i], "--name") == 0) {
            if (i + 1 < argc) join_name = argv[++i];
        }
    }

    mc::ModManager::init_mods();
    // Builtin recipes first, then data-driven overrides from mods/.
    mc::RecipeManager::init_recipes();
    mc::ModManager::load_content();
    mc::Game game;
    if (!game.init()) {
        return 1;
    }

    if (screenshot_mode) {
        game.request_screenshot(wait_ticks, screenshot_path);
    } else if (auto_play) {
        game.auto_play();
    }
    if (host_mode && !join_mode) {
        if (auto_play) {
            // The in-memory world is already up: serve it immediately.
            game.mp_host_start(host_port);
        } else {
            // Menu-driven world pick: arm the server for start_game.
            game.mp_host_after_load(host_port);
        }
    }
    if (join_mode) {
        game.mp_client_join(join_host, join_port, join_name);
    }
    if (automation_port != 0) {
        game.enable_automation(automation_port);
    }

    game.run();
    game.shutdown();
    return 0;
}
