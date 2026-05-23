#include "app/config.h"
#include "app/logging.h"
#include "v2/battle/battle_backend_service.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};
v2::battle::BattleBackendService* g_service = nullptr;

void handle_signal(int) {
    g_running = false;
    if (g_service) {
        std::cout << "\nv2_battle_backend: shutting down..." << std::endl;
        g_service->stop();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    app::logging::init("v2_battle_backend");

    const auto config_path = app::config::resolve_backend_config_path(
        "battle", argc, argv, "config/environments/local/battle.json");
    auto config = app::config::load_backend_service_config("battle", config_path, 9303);
    if (argc > 1 && std::string(argv[1]) != "--config") {
        config.port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    // Parse optional --plugin argument
    std::string plugin_type = "battle";
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--plugin" && i + 1 < argc) {
            plugin_type = argv[i + 1];
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    v2::battle::BattleBackendService service(config.port);
    service.set_tls_config(config.tls_config);
    if (plugin_type == "tank_battle") {
        service.set_instance_type("tank_battle");
        std::cout << "v2_battle_backend: using TankBattlePlugin" << std::endl;
    } else {
        std::cout << "v2_battle_backend: using BattleInstancePlugin (default)" << std::endl;
    }

    if (!config.archive_path.empty()) {
        service.set_archive_path(config.archive_path);
        service.set_replay_storage_dir(config.archive_path);
        std::cout << "v2_battle_backend: archive path set to "
                  << config.archive_path << std::endl;
    }

    g_service = &service;

    service.start();
    std::cout << "v2_battle_backend: listening on port " << service.local_port() << std::endl;
    std::cout << "v2_battle_backend: running (Ctrl+C to stop)" << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    service.stop();
    std::cout << "v2_battle_backend: stopped" << std::endl;
    return 0;
}
