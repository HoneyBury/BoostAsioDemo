#include "game/gateway/gateway_service.h"

#include "app/audit_log.h"
#include "net/protocol.h"

#include <array>
#include <chrono>

namespace game::gateway {

GatewayService::GatewayService(SessionManager& session_manager,
                               GatewayMetrics& metrics,
                               PushService& push_service)
    : session_manager_(session_manager),
      metrics_(metrics),
      push_service_(push_service) {}

void GatewayService::register_handlers(net::MessageDispatcher& dispatcher) const {
    // v1.1.3 / T05: 白名单与限频必须是 client ingress 层策略，在投递到 business_pool
    // 之前在调用线程（Session strand）上同步执行，避免未鉴权/超限流量仍占用业务线程。
    // nullptr session 的 dispatch（实验性 InternalBus）不跑这套中间件 —— 见 message_dispatcher.h。
    dispatcher.register_ingress_middleware(
        "auth_whitelist",
        [this](const net::DispatchContext& context) { return should_allow_message(context); });
    dispatcher.register_ingress_middleware(
        "rate_limit",
        [this](const net::DispatchContext& context) { return check_rate_limit(context); });

    dispatcher.register_handler(
        net::protocol::kHeartbeatRequest,
        [this](const net::DispatchContext& context) {
            // 心跳包不进入复杂业务流程，直接由网关层回包。
            push_service_.send_ok(context.session,
                                  net::protocol::kHeartbeatResponse,
                                  context.request_id,
                                  "pong");
        });
}

bool GatewayService::should_allow_message(const net::DispatchContext& context) const {
    static constexpr std::array<std::uint16_t, 3> kWhitelist = {
        net::protocol::kHeartbeatRequest,
        net::protocol::kLoginRequest,
        net::protocol::kEchoRequest,
    };

    for (const auto message_id : kWhitelist) {
        if (context.message_id == message_id) {
            return true;
        }
    }

    if (session_manager_.is_authenticated(context.session)) {
        return true;
    }

    metrics_.on_packet_blocked();
    push_service_.send_error(context.session,
                             context.request_id,
                             net::protocol::ErrorCode::kAuthRequired);
    return false;
}

bool GatewayService::check_rate_limit(const net::DispatchContext& context) const {
    if (context.message_id == net::protocol::kHeartbeatRequest) {
        return true;
    }

    const auto session_key = context.session.get();
    const auto now = std::chrono::steady_clock::now();
    bool allowed = false;

    {
        std::scoped_lock lock(rate_limit_mutex_);
        auto& entry = rate_limits_[session_key];
        if (now - entry.window_started_at >= kRateLimitWindow) {
            entry.window_started_at = now;
            entry.message_count = 0;
        }

        if (entry.message_count < kMaxMessagesPerWindow) {
            ++entry.message_count;
            allowed = true;
        }
    }

    if (allowed) {
        return true;
    }

    metrics_.on_packet_blocked();
    AUDIT_LOG("rate_limited", "session=" + context.session->remote_endpoint());
    push_service_.send_error(context.session,
                             context.request_id,
                             net::protocol::ErrorCode::kRateLimited);
    return false;
}

}  // namespace game::gateway
