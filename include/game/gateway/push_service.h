#pragma once

#include "net/protocol.h"
#include "net/session.h"

#include <memory>
#include <string>
#include <vector>

namespace game::gateway {

class PushService {
public:
    using SessionPtr = std::shared_ptr<net::Session>;

    void send_ok(const SessionPtr& session,
                 std::uint16_t message_id,
                 std::uint32_t request_id,
                 std::string body) const;
    void send_error(const SessionPtr& session,
                    std::uint32_t request_id,
                    net::protocol::ErrorCode error_code,
                    std::string body = {}) const;
    void send_push(const SessionPtr& session,
                   std::uint16_t message_id,
                   std::string body) const;
    void broadcast(const std::vector<SessionPtr>& sessions,
                   std::uint16_t message_id,
                   std::string body,
                   const SessionPtr& exclude_session = {}) const;
};

}  // namespace game::gateway
