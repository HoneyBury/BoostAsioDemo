#include "game/gateway/gateway_service.h"

#include "net/protocol.h"

#include <array>

namespace game::gateway {

GatewayService::GatewayService(SessionManager& session_manager, GatewayMetrics& metrics)
    : session_manager_(session_manager), metrics_(metrics) {}

void GatewayService::register_handlers(net::MessageDispatcher& dispatcher) const {
    dispatcher.register_middleware(
        "auth_whitelist",
        [this](const net::DispatchContext& context) { return should_allow_message(context); });

    dispatcher.register_handler(
        net::protocol::kHeartbeatRequest,
        [](const net::DispatchContext& context) {
            // 心跳包不进入复杂业务流程，直接由网关层回包。
            context.session->send(net::protocol::kHeartbeatResponse, "pong");
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
    context.session->send(net::protocol::kErrorResponse, "auth_required");
    return false;
}

}  // namespace game::gateway
