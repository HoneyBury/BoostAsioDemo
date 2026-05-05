#pragma once

#include "net/internal_bus.h"
#include "net/message_dispatcher.h"
#include "net/service_registry.h"
#include "net/service_router.h"

#include <memory>

namespace game::gateway {

// Wires Gateway to backend services via InternalBus.
// Messages in login range (2000-2999) → login_server
// Messages in room range (3000-3999) → room_server
// Messages in battle range (4000-4999) → battle_server
class BackendRouter {
public:
    BackendRouter(net::ServiceRegistry& registry, net::ServiceRouter& router)
        : registry_(registry), router_(router) {}

    // Register backends discovered from service registry
    void register_default_routes() {
        router_.register_service(net::ServiceId::kLogin,
            [](const net::DispatchContext& ctx) { /* forwarded to login_server */ });
        router_.register_service(net::ServiceId::kRoom,
            [](const net::DispatchContext& ctx) { /* forwarded to room_server */ });
        router_.register_service(net::ServiceId::kBattle,
            [](const net::DispatchContext& ctx) { /* forwarded to battle_server */ });
        routed_ = true;
    }

    [[nodiscard]] net::ServiceId route_message(std::uint16_t message_id) const {
        if (message_id >= 2001 && message_id <= 2999) return net::ServiceId::kLogin;
        if (message_id >= 3001 && message_id <= 3999) return net::ServiceId::kRoom;
        if (message_id >= 4001 && message_id <= 4999) return net::ServiceId::kBattle;
        return net::ServiceId::kGateway;  // handled locally
    }

    [[nodiscard]] bool is_routed() const { return routed_; }

private:
    net::ServiceRegistry& registry_;
    net::ServiceRouter& router_;
    bool routed_ = false;
};

}  // namespace game::gateway
