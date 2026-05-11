#include "v2/gateway/gateway_service_bridge.h"

namespace v2::gateway {

namespace {

v2::service::BackendConnectionOptions make_options(
    const GatewayServiceBridge::BackendConfig& config) {
    return v2::service::BackendConnectionOptions{
        .host = config.host,
        .port = config.port,
    };
}

}  // namespace

GatewayServiceBridge::GatewayServiceBridge(
    std::optional<BackendConfig> login_config) {
    if (login_config) {
        login_slot_.config = std::move(*login_config);
    }
}

GatewayServiceBridge::~GatewayServiceBridge() { shutdown(); }

GatewayServiceBridge::BackendSlot& GatewayServiceBridge::slot_for(
    v2::service::ServiceId service) {
    switch (service) {
        case v2::service::ServiceId::kLogin:
            return login_slot_;
        default:
            return login_slot_;
    }
}

v2::service::BackendConnection* GatewayServiceBridge::ensure_connection(
    v2::service::ServiceId service) {
    auto& slot = slot_for(service);
    if (!slot.config) return nullptr;

    if (!slot.connection) {
        slot.connection = std::make_unique<v2::service::BackendConnection>(
            make_options(*slot.config));
        if (!slot.connection->connect()) {
            slot.connection.reset();
            return nullptr;
        }
    }

    if (!slot.connection->is_connected()) {
        slot.connection.reset();
        return nullptr;
    }

    return slot.connection.get();
}

GatewayServiceBridge::BackendRoutingResult GatewayServiceBridge::route(
    v2::service::ServiceId target,
    const std::string& message_type,
    const std::string& payload) {
    BackendRoutingResult result;

    auto* conn = ensure_connection(target);
    if (!conn) {
        result.error = v2::service::ServiceErrorCode::kUnavailable;
        return result;
    }

    v2::service::BackendEnvelope request{
        .target_service = target,
        .kind = v2::service::MessageKind::kRequest,
        .payload = payload,
        .message_type = message_type,
    };

    auto response = conn->send_request(request);
    if (!response) {
        result.error = v2::service::ServiceErrorCode::kTimeout;
        return result;
    }

    result.correlation_id = response->correlation_id;

    if (response->kind == v2::service::MessageKind::kError) {
        result.error = static_cast<v2::service::ServiceErrorCode>(response->error_code);
        return result;
    }

    result.success = true;
    result.response_payload = std::move(response->payload);
    return result;
}

bool GatewayServiceBridge::is_backend_available(
    v2::service::ServiceId service) const {
    const auto& slot = const_cast<GatewayServiceBridge*>(this)->slot_for(service);
    return slot.connection && slot.connection->is_connected();
}

void GatewayServiceBridge::shutdown() {
    if (login_slot_.connection) {
        login_slot_.connection->close();
        login_slot_.connection.reset();
    }
}

}  // namespace v2::gateway
