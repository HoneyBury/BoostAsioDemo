#pragma once

#include "game/gateway/gateway_metrics.h"
#include "game/gateway/session_manager.h"
#include "net/message_dispatcher.h"

#include <unordered_set>

namespace game::gateway {

class GatewayService {
public:
    GatewayService(SessionManager& session_manager, GatewayMetrics& metrics);

    void register_handlers(net::MessageDispatcher& dispatcher) const;

private:
    [[nodiscard]] bool should_allow_message(const net::DispatchContext& context) const;

    SessionManager& session_manager_;
    GatewayMetrics& metrics_;
};

}  // namespace game::gateway
