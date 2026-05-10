#pragma once

#include "net/protocol.h"
#include "net/session.h"

#include <memory>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace game::gateway {

class PushService {
public:
    using SessionPtr = std::shared_ptr<net::Session>;
    using SessionWriteTask = std::function<void()>;
    using SessionWriteScheduler = std::function<bool(const SessionPtr&, SessionWriteTask)>;

    void set_write_scheduler(SessionWriteScheduler scheduler);

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

private:
    void dispatch_write(const SessionPtr& session, SessionWriteTask task) const;

    mutable std::mutex scheduler_mutex_;
    SessionWriteScheduler write_scheduler_;
};

}  // namespace game::gateway
