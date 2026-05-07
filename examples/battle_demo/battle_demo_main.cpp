// 战斗系统演示：帧同步、输入路由、结算、回放录制与播放
//
// 运行方式：
//   battle_demo.exe [config/gateway.json] [端口号]

#include "app/config.h"
#include "app/crash_handler.h"
#include "app/logging.h"
#include "game/battle/battle_manager.h"
#include "game/battle/battle_service.h"
#include "game/battle/replay_player.h"
#include "game/gateway/gateway_metrics.h"
#include "game/gateway/gateway_server.h"
#include "game/gateway/gateway_service.h"
#include "game/gateway/push_service.h"
#include "game/gateway/session_manager.h"
#include "game/login/login_service.h"
#include "game/login/token_validator.h"
#include "game/persistence/player_store.h"
#include "game/room/room_manager.h"
#include "game/room/room_service.h"
#include "net/message_dispatcher.h"
#include "net/protocol.h"

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace asio = boost::asio;

int main(int argc, char* argv[]) {
    app::logging::init("battle_demo");
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
    // 1. 战斗服务 — 完整战斗流程
    //    协议消息:
    //      4001/4002: 起战斗请求/响应 — 需 2 人在同一房间
    //      4003/4004: 战斗输入请求/响应 — 带输入序号
    //      4005:      战斗输入广播 (kBattleInputPush) — 推送给双方
    //      4006:      战斗状态广播 (kBattleStatePush) — 帧同步推送
    // =================================================================
    game::battle::BattleService battle_svc(session_mgr, push, room_mgr, battle_mgr, metrics);
    battle_svc.register_handlers(dispatcher);

    // =================================================================
    // 2. 帧同步 — BattleManager::advance_frame()
    //    每帧收集玩家输入，打包为 FrameSnapshot 返回
    //    输入按 frame_number 分桶，支持帧锁定步进
    //
    //    InputEvent 结构:
    //      sequence:    全局自增序号
    //      frame_number: 所属帧号
    //      user_id:      输入来源玩家
    //      payload:      输入数据
    // =================================================================

    // =================================================================
    // 3. 战斗结算 — BattleManager::end_battle()
    //    返回 BattleResult:
    //      winner_id:    胜利者（输入数最多者）
    //      total_frames: 总帧数
    //      total_inputs: 总输入数
    //      player_scores: 各玩家输入统计
    // =================================================================

    // =================================================================
    // 4. 回放录制 — IBattleReplayStore
    //    战斗结束后将全部 InputEvent 序列化为 JSON 保存
    //    ReplayPlayer 可加载回放文件逐帧播放
    // =================================================================
    game::persistence::JsonFileBattleReplayStore replay_store("runtime/replays");
    LOG_INFO("战斗回放存储目录: runtime/replays");

    // =================================================================
    // 5. 观战模式 — BattleManager 支持旁观者
    //    add_spectator() / remove_spectator()
    //    最多 100 个旁观者 (kMaxSpectatorsPerBattle)
    //    旁观者接收战斗状态推送但不能发送输入
    // =================================================================

    // 房间 + 登录服务
    game::login::DevTokenValidator validator;
    game::login::LoginService login_svc(session_mgr, push, room_mgr, validator, metrics);
    login_svc.register_handlers(dispatcher);

    game::room::RoomService room_svc(session_mgr, push, battle_mgr, room_mgr, metrics);
    room_svc.register_handlers(dispatcher);

    game::gateway::GatewayService gw_svc(session_mgr, metrics);
    gw_svc.register_handlers(dispatcher);

    net::SessionOptions opts;
    opts.max_packet_size = config.session_max_packet_size;
    opts.max_pending_write_bytes = config.session_max_pending_write_bytes;

    game::gateway::GatewayServer server(io, dispatcher, session_mgr, room_mgr, battle_mgr,
                                         metrics, config.port, config.http_management_port,
                                         opts, config.metrics_log_interval);
    server.set_connection_limits(config.max_connections, config.per_ip_connection_limit);
    server.start();

    LOG_INFO("=== 战斗演示服务器已启动 :{} ===", server.local_port());
    LOG_INFO("功能展示: 起战斗 | 帧同步 | 输入路由 | 结算 | 回放录制 | 观战");

    std::vector<std::thread> workers(config.io_threads);
    for (auto& w : workers) w = std::thread([&] { io.run(); });
    for (auto& w : workers) w.join();
    pool.join();
    return 0;
}
