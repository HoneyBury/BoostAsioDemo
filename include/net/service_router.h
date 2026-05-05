#pragma once

#include "net/message_dispatcher.h"
#include "net/protocol.h"
#include "net/session.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace net {

// Internal service identifiers for multi-process routing
enum class ServiceId : std::uint16_t {
    kGateway = 1,
    kLogin = 2,
    kRoom = 3,
    kBattle = 4,
};

class ServiceRouter {
public:
    using ForwardHandler = std::function<void(const DispatchContext&)>;

    void register_service(ServiceId service_id, ForwardHandler handler) {
        std::unique_lock lock(mutex_);
        services_[service_id] = std::move(handler);
    }

    // Route a message to the handler registered for the target service.
    // Returns false if no handler is registered for the given service.
    bool route(ServiceId target, const DispatchContext& context) const {
        ForwardHandler handler;
        {
            std::shared_lock lock(mutex_);
            const auto it = services_.find(target);
            if (it == services_.end()) {
                return false;
            }
            handler = it->second;
        }
        handler(context);
        return true;
    }

    [[nodiscard]] std::size_t service_count() const {
        std::shared_lock lock(mutex_);
        return services_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<ServiceId, ForwardHandler> services_;
};

}  // namespace net
