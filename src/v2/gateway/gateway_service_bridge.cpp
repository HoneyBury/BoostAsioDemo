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
    std::optional<BackendConfig> login_config,
    std::optional<BackendConfig> room_config,
    std::optional<BackendConfig> battle_config,
    std::shared_ptr<BackendMetrics> metrics)
    : metrics_(std::move(metrics)) {
    if (login_config) {
        login_slot_.config = std::move(*login_config);
    }
    if (room_config) {
        room_slot_.config = std::move(*room_config);
    }
    if (battle_config) {
        battle_slot_.config = std::move(*battle_config);
    }
}

GatewayServiceBridge::~GatewayServiceBridge() { shutdown(); }

GatewayServiceBridge::BackendSlot& GatewayServiceBridge::slot_for(
    v2::service::ServiceId service) {
    switch (service) {
        case v2::service::ServiceId::kLogin:
            return login_slot_;
        case v2::service::ServiceId::kRoom:
            return room_slot_;
        case v2::service::ServiceId::kBattle:
            return battle_slot_;
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
        if (registry_) {
            registry_->heartbeat(service, slot.config->host, slot.config->port);
        }
    }

    if (!slot.connection->is_connected()) {
        if (registry_) {
            registry_->mark_unhealthy(service, slot.config->host,
                                      slot.config->port);
        }
        slot.connection.reset();
        return nullptr;
    }

    if (registry_) {
        registry_->heartbeat(service, slot.config->host, slot.config->port);
    }

    return slot.connection.get();
}

void GatewayServiceBridge::record_route_result(
    v2::service::ServiceId target,
    const BackendRoutingResult& result) {
    if (!metrics_) return;

    if (result.success) {
        metrics_->record_success(target);
    } else if (result.error == v2::service::ServiceErrorCode::kTimeout) {
        metrics_->record_timeout(target);
    } else if (result.error == v2::service::ServiceErrorCode::kUnavailable) {
        metrics_->record_unavailable(target);
    } else {
        metrics_->record_error(target);
    }
}

GatewayServiceBridge::BackendRoutingResult GatewayServiceBridge::route(
    v2::service::ServiceId target,
    const std::string& message_type,
    const std::string& payload) {
    BackendRoutingResult result;

    if (metrics_) {
        metrics_->record_request(target);
    }

    auto* conn = ensure_connection(target);
    if (!conn) {
        result.error = v2::service::ServiceErrorCode::kUnavailable;
        record_route_result(target, result);
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
        record_route_result(target, result);
        return result;
    }

    result.correlation_id = response->correlation_id;

    if (response->kind == v2::service::MessageKind::kError) {
        result.error = static_cast<v2::service::ServiceErrorCode>(response->error_code);
        record_route_result(target, result);
        return result;
    }

    result.success = true;
    result.response_payload = std::move(response->payload);
    record_route_result(target, result);
    return result;
}

void GatewayServiceBridge::set_service_registry(
    std::shared_ptr<v2::service::ServiceRegistry> registry) {
    registry_ = std::move(registry);
}

std::shared_ptr<BackendMetrics> GatewayServiceBridge::get_metrics() const {
    return metrics_;
}

std::shared_ptr<v2::service::ServiceRegistry> GatewayServiceBridge::get_registry() const {
    return registry_;
}

bool GatewayServiceBridge::is_backend_available(
    v2::service::ServiceId service) const {
    // Attempt lazy connect first
    auto* conn = const_cast<GatewayServiceBridge*>(this)->ensure_connection(service);
    return conn != nullptr;
}

void GatewayServiceBridge::shutdown() {
    for (auto* slot : {&login_slot_, &room_slot_, &battle_slot_}) {
        if (slot->connection) {
            slot->connection->close();
            slot->connection.reset();
        }
    }
}

}  // namespace v2::gateway
