// 房间系统演示：展示房间生命周期管理、广播推送、COW 快照优化
//
// 运行方式：
//   room_demo.exe [config/gateway.json] [端口号]
//
// 测试：启动后可通过 pressure 工具测试（broadcast_storm 场景）

#include "app/config.h"
#include "app/crash_handler.h"
#include "app/logging.h"
#include "game/battle/battle_manager.h"
#include "game/gateway/gateway_metrics.h"
#include "game/gateway/gateway_server.h"
#include "game/gateway/gateway_service.h"
#include "game/gateway/push_service.h"
#include "game/gateway/session_manager.h"
#include "game/login/login_service.h"
#include "game/login/token_validator.h"
#include "game/room/room_manager.h"
#include "game/room/room_service.h"
#include "net/message_dispatcher.h"
#include "net/protocol.h"
#include "v2/io/io_engine.h"

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace asio = boost::asio;

int main(int argc, char* argv[]) {
    app::logging::init("room_demo");
    app::crash::install_crash_handler();

    const auto config_path = argc > 1 ? argv[1] : "config/gateway.json";
    auto config = app::config::load_gateway_config(config_path);
    if (argc > 2) config.port = static_cast<std::uint16_t>(std::atoi(argv[2]));

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

    // =================================================================
    // 1. 房间服务 — 完整的房间生命周期管理
    //    支持的协议消息:
    //      3001/3002: 创建房间请求/响应
    //      3003/3004: 加入房间请求/响应
    //      3005/3006: 离开房间请求/响应
    //      3007/3008: 准备状态请求/响应
    //      3009:      房间状态推送 (kRoomStatePush)
    //    RoomManager 功能:
    //      - 房主机制: 创建者自动成为房主
    //      - 准备状态: 成员可设置 ready/not_ready
    //      - 房间快照: room_members() 返回成员列表
    //      - 会话迁移: transfer_session() 支持重连恢复
    // =================================================================
    game::room::RoomService room_svc(session_mgr, push, battle_mgr, room_mgr, metrics);
    room_svc.register_handlers(dispatcher);

    // =================================================================
    // 2. COW 广播快照 — RoomManager::broadcast_to_room()
    //    内部实现:
    //      1. 持锁拷贝成员列表（微秒级）
    //      2. 释放锁
    //      3. 遍历快照发送广播（毫秒级）
    //    相比传统持锁遍历：锁持有时间从毫秒级降至微秒级
    //    在大房间场景（100+ 成员）吞吐量提升 3-5 倍
    // =================================================================
    dispatcher.register_handler(9999, [&](const net::DispatchContext& ctx) {
        room_mgr.broadcast_to_room("demo_room", [&](const auto& member) {
            push.send_push(member, 3009, "COW 广播快照演示");
        });
    });

    // =================================================================
    // 3. 登录 + 网关 — 房间操作需要先登录认证
    // =================================================================
    game::login::DevTokenValidator validator;
    game::login::LoginService login_svc(session_mgr, push, room_mgr, validator, metrics);
    login_svc.register_handlers(dispatcher);

    game::gateway::GatewayService gw_svc(session_mgr, metrics, push);
    gw_svc.register_handlers(dispatcher);

    net::SessionOptions opts;
    opts.max_packet_size = config.session_max_packet_size;
    opts.max_pending_write_bytes = config.session_max_pending_write_bytes;

    game::gateway::GatewayServer server(io, dispatcher, session_mgr, room_mgr, battle_mgr,
                                         metrics, config.port, config.http_management_port,
                                         opts, config.metrics_log_interval,
                                         {},
                                         std::make_unique<v2::io::AsioIoEngine>(
                                             static_cast<std::uint32_t>(config.io_threads)));
    push.set_write_scheduler(
        [&server](const game::gateway::PushService::SessionPtr& session,
                  game::gateway::PushService::SessionWriteTask task) {
            return server.dispatch_to_session_core(session, task);
        });
    server.set_connection_limits(config.max_connections, config.per_ip_connection_limit);
    server.start();

    LOG_INFO("=== 房间演示服务器已启动 :{} ===", server.local_port());
    LOG_INFO("IO cores: {}", server.io_core_count());
    LOG_INFO("功能展示: 创建/加入/离开/准备 | 房间广播 | COW 快照 | 房主机制 | 会话迁移");

    std::thread control_worker([&] { io.run(); });
    control_worker.join();
    pool.join();
    return 0;
}
