#pragma once

#include "game/gateway/gateway_metrics.h"
#include "game/gateway/push_service.h"
#include "game/gateway/session_manager.h"
#include "game/login/token_validator.h"
#include "game/room/room_manager.h"
#include "net/message_dispatcher.h"

namespace game::login {

class LoginService {
public:
    LoginService(gateway::SessionManager& session_manager,
                 gateway::PushService& push_service,
                 room::RoomManager& room_manager,
                 const TokenValidator& token_validator,
                 gateway::GatewayMetrics& metrics);

    void register_handlers(net::MessageDispatcher& dispatcher) const;

private:
    gateway::SessionManager& session_manager_;
    gateway::PushService& push_service_;
    room::RoomManager& room_manager_;
    const TokenValidator& token_validator_;
    gateway::GatewayMetrics& metrics_;
};

}  // namespace game::login
