#pragma once

#include "game/gateway/gateway_metrics.h"
#include "game/gateway/push_service.h"
#include "game/gateway/session_manager.h"

#include <cstdint>
#include <functional>
#include <string>

namespace net {

class MessageDispatcher;

}  // namespace net

namespace game::gateway {

// TCP 消息号 5001–5005：演示/示例用（demo-only）；默认 GatewayServer **不注册**。
// **无令牌/角色 ACL**——任意已连线会话皆可触发（须在受信链路使用）。成熟度冻结：docs/v1-governance-layers.md §6。
// **v1.1.11**：调用前提与最小审计口径见 **`docs/v1-admin-audit-rules.md`**；边界侧在 `AdminService::register_handlers` 写 **`admin_invoke`**。
class AdminService {
public:
    using KickCallback = std::function<void(const std::string& user_id)>;
    using BanCallback = std::function<void(const std::string& ip, std::uint32_t duration_sec)>;
    using StatusCallback = std::function<std::string()>;
    using ReloadCallback = std::function<void()>;

    AdminService(SessionManager& sm, GatewayMetrics& m, PushService& push_service)
        : push_service_(push_service) {
        (void)sm;
        (void)m;
    }

    void set_kick_callback(KickCallback cb) { on_kick_ = std::move(cb); }
    void set_ban_callback(BanCallback cb) { on_ban_ = std::move(cb); }
    void set_status_callback(StatusCallback cb) { on_status_ = std::move(cb); }
    void set_reload_callback(ReloadCallback cb) { on_reload_ = std::move(cb); }

    void register_handlers(net::MessageDispatcher& dispatcher);

private:
    PushService& push_service_;
    KickCallback on_kick_;
    BanCallback on_ban_;
    StatusCallback on_status_;
    ReloadCallback on_reload_;
};

}  // namespace game::gateway
