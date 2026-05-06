#include "game/gateway/push_service.h"

namespace game::gateway {

void PushService::send_ok(const SessionPtr& session,
                          std::uint16_t message_id,
                          std::uint32_t request_id,
                          std::string body) const {
    session->send(message_id,
                  request_id,
                  static_cast<std::int32_t>(net::protocol::ErrorCode::kOk),
                  std::move(body));
}

void PushService::send_error(const SessionPtr& session,
                             std::uint32_t request_id,
                             net::protocol::ErrorCode error_code,
                             std::string body) const {
    if (body.empty()) {
        body = net::protocol::to_string(error_code);
    }

    session->send(net::protocol::kErrorResponse,
                  request_id,
                  static_cast<std::int32_t>(error_code),
                  std::move(body));
}

void PushService::send_push(const SessionPtr& session,
                            std::uint16_t message_id,
                            std::string body) const {
    session->send(message_id,
                  0,
                  static_cast<std::int32_t>(net::protocol::ErrorCode::kOk),
                  std::move(body));
}

void PushService::broadcast(const std::vector<SessionPtr>& sessions,
                            std::uint16_t message_id,
                            std::string body,
                            const SessionPtr& exclude_session) const {
    for (const auto& session : sessions) {
        if (exclude_session && session.get() == exclude_session.get()) {
            continue;
        }

        send_push(session, message_id, body);
    }
}

}  // namespace game::gateway
