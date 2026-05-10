#include "app/config.h"
#include "app/crash_handler.h"
#include "app/logging.h"
#include "game/battle/battle_manager.h"
#include "game/battle/battle_service.h"
#include "game/gateway/gateway_metrics.h"
#include "game/gateway/gateway_server.h"
#include "game/gateway/push_service.h"
#include "game/gateway/session_manager.h"
#include "game/room/room_manager.h"
#include "net/message_dispatcher.h"
#include "net/protocol.h"
#include "v2/io/io_engine.h"

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <vector>

namespace asio = boost::asio;

int main(int argc, char* argv[]) {
    app::logging::init("battle_server");
    app::crash::install_crash_handler();

    const auto config_path = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("config/gateway.json");
    auto config = app::config::load_gateway_config(config_path);
    if (argc > 2) config.port = static_cast<std::uint16_t>(std::atoi(argv[2]));

    net::SessionOptions session_opts;
    session_opts.max_packet_size = config.session_max_packet_size;
    session_opts.max_pending_write_bytes = config.session_max_pending_write_bytes;

    asio::io_context io;
    boost::asio::thread_pool pool(config.business_threads);
    net::MessageDispatcher dispatcher(pool);
    game::gateway::SessionManager session_mgr;
    game::gateway::GatewayMetrics metrics;
    game::gateway::PushService push;
    game::room::RoomManager room_mgr;
    game::battle::BattleManager battle_mgr;
    room_mgr.set_battle_active_query([&battle_mgr](const std::string& room_id) {
        return battle_mgr.battle_started(room_id);
    });

    game::battle::BattleService battle_svc(session_mgr, push, room_mgr, battle_mgr, metrics);
    battle_svc.register_handlers(dispatcher);

    game::gateway::GatewayServer server(io, dispatcher, session_mgr, room_mgr, battle_mgr,
                                         metrics, config.port, 0, session_opts, {},
                                         game::gateway::GatewayMetricsExportOptions{},
                                         std::make_unique<v2::io::AsioIoEngine>(
                                             static_cast<std::uint32_t>(config.io_threads)));
    push.set_write_scheduler(
        [&server](const game::gateway::PushService::SessionPtr& session,
                  game::gateway::PushService::SessionWriteTask task) {
            return server.dispatch_to_session_core(session, task);
        });
    server.set_connection_limits(config.max_connections, config.per_ip_connection_limit);
    server.start();

    LOG_INFO("IO cores: {}", server.io_core_count());
    std::thread control_worker([&] { io.run(); });
    control_worker.join();
    pool.join();
    return 0;
}
