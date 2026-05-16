#include "v2/service/backend_envelope.h"

#include <nlohmann/json.hpp>

#include <atomic>

namespace v2::service {

namespace {

std::atomic<std::uint64_t> g_next_correlation_id{1};

std::optional<ServiceId> parse_service_id(const std::string& value) {
    if (value == "gateway") return ServiceId::kGateway;
    if (value == "login") return ServiceId::kLogin;
    if (value == "room") return ServiceId::kRoom;
    if (value == "battle") return ServiceId::kBattle;
    if (value == "match" || value == "matchmaking") return ServiceId::kMatchmaking;
    if (value == "leaderboard") return ServiceId::kLeaderboard;
    return std::nullopt;
}

}  // namespace

std::string to_json(const BackendEnvelope& envelope) {
    nlohmann::json doc{
        {"correlation_id", envelope.correlation_id},
        {"source_service", to_string(envelope.source_service)},
        {"target_service", to_string(envelope.target_service)},
        {"kind", to_string(envelope.kind)},
        {"timeout_ms", envelope.timeout_ms},
        {"error_code", envelope.error_code},
        {"payload", envelope.payload},
        {"message_type", envelope.message_type},
        {"trace_id", envelope.trace_id},
        {"span_id", envelope.span_id},
    };
    return doc.dump();
}

std::optional<BackendEnvelope> from_json(std::string_view json) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }

    if (!doc.contains("correlation_id") || !doc.contains("source_service") ||
        !doc.contains("target_service") || !doc.contains("kind") ||
        !doc.contains("payload")) {
        return std::nullopt;
    }

    BackendEnvelope envelope;
    envelope.correlation_id = doc["correlation_id"].get<std::uint64_t>();

    const auto source_str = doc["source_service"].get<std::string>();
    const auto target_str = doc["target_service"].get<std::string>();
    const auto kind_str = doc["kind"].get<std::string>();

    const auto source_service = parse_service_id(source_str);
    const auto target_service = parse_service_id(target_str);
    if (!source_service.has_value() || !target_service.has_value()) {
        return std::nullopt;
    }
    envelope.source_service = *source_service;
    envelope.target_service = *target_service;

    // Parse message kind
    if (kind_str == "request") {
        envelope.kind = MessageKind::kRequest;
    } else if (kind_str == "response") {
        envelope.kind = MessageKind::kResponse;
    } else if (kind_str == "push") {
        envelope.kind = MessageKind::kPush;
    } else if (kind_str == "error") {
        envelope.kind = MessageKind::kError;
    } else {
        return std::nullopt;
    }

    envelope.timeout_ms = doc.value("timeout_ms", 0U);
    envelope.error_code = doc.value("error_code", 0);
    envelope.payload = doc["payload"].get<std::string>();
    envelope.message_type = doc.value("message_type", std::string{});
    envelope.trace_id = doc.value("trace_id", 0ULL);
    envelope.span_id = doc.value("span_id", 0ULL);

    return envelope;
}

bool is_valid(const BackendEnvelope& envelope) {
    if (envelope.correlation_id == 0) {
        return false;
    }
    return !envelope.payload.empty() || envelope.kind == MessageKind::kError;
}

std::uint64_t generate_correlation_id() {
    return g_next_correlation_id.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace v2::service
