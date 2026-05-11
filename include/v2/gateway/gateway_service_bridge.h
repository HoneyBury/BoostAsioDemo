#pragma once

#include "v2/service/backend_connection.h"
#include "v2/service/error_codes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace v2::gateway {

class GatewayServiceBridge {
public:
    struct BackendRoutingResult {
        bool success = false;
        std::string response_payload;
        v2::service::ServiceErrorCode error = v2::service::ServiceErrorCode::kOk;
        std::uint64_t correlation_id = 0;
    };

    struct BackendConfig {
        std::string host = "127.0.0.1";
        std::uint16_t port = 0;
    };

    explicit GatewayServiceBridge(
        std::optional<BackendConfig> login_config = std::nullopt);
    ~GatewayServiceBridge();

    GatewayServiceBridge(const GatewayServiceBridge&) = delete;
    GatewayServiceBridge& operator=(const GatewayServiceBridge&) = delete;
    GatewayServiceBridge(GatewayServiceBridge&&) = delete;
    GatewayServiceBridge& operator=(GatewayServiceBridge&&) = delete;

    [[nodiscard]] BackendRoutingResult route(
        v2::service::ServiceId target,
        const std::string& message_type,
        const std::string& payload);

    [[nodiscard]] bool is_backend_available(v2::service::ServiceId service) const;

    void shutdown();

private:
    struct BackendSlot {
        std::optional<BackendConfig> config;
        std::unique_ptr<v2::service::BackendConnection> connection;
    };

    v2::service::BackendConnection* ensure_connection(v2::service::ServiceId service);
    BackendSlot& slot_for(v2::service::ServiceId service);

    BackendSlot login_slot_;
};

}  // namespace v2::gateway
