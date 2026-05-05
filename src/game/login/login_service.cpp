#include "game/login/login_service.h"

#include "net/protocol.h"

namespace game::login {

LoginService::LoginService(gateway::SessionManager& session_manager, gateway::GatewayMetrics& metrics)
    : session_manager_(session_manager), metrics_(metrics) {}

void LoginService::register_handlers(net::MessageDispatcher& dispatcher) const {
    dispatcher.register_handler(
        net::protocol::kLoginRequest,
        [this](const net::DispatchContext& context) {
            if (context.body.empty()) {
                context.session->send(net::protocol::kErrorResponse, "invalid_user_id");
                return;
            }

            auto replaced_session = session_manager_.authenticate(context.session, context.body);
            if (replaced_session && replaced_session != context.session) {
                replaced_session->send(net::protocol::kErrorResponse, "duplicate_login");
                replaced_session->stop();
            }

            metrics_.on_login_success();
            context.session->send(net::protocol::kLoginResponse, "login_ok:" + context.body);
        });
}

}  // namespace game::login
