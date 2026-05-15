#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace v3::proto {

struct EnvelopeMeta {
    std::uint64_t correlation_id = 0;
    std::string source_service;
    std::string target_service;
    std::uint32_t timeout_ms = 0;
    std::int32_t error_code = 0;
    std::uint64_t trace_id = 0;
    std::uint64_t span_id = 0;
};

struct DecodedEnvelope {
    EnvelopeMeta meta;
    std::string domain;
    std::string message_name;
    nlohmann::json payload;
};

inline std::string encode_envelope(const EnvelopeMeta& meta,
                                   const std::string& domain,
                                   const std::string& message_name,
                                   const nlohmann::json& payload) {
    nlohmann::json doc{
        {"correlation_id", meta.correlation_id},
        {"source_service", meta.source_service},
        {"target_service", meta.target_service},
        {"timeout_ms", meta.timeout_ms},
        {"error_code", meta.error_code},
        {"trace_id", meta.trace_id},
        {"span_id", meta.span_id},
        {"payload", nlohmann::json::object()},
    };
    doc["payload"][domain] = nlohmann::json::object();
    doc["payload"][domain][message_name] = payload;
    return doc.dump();
}

inline std::optional<DecodedEnvelope> decode_envelope(const std::string& encoded) {
    auto doc = nlohmann::json::parse(encoded, nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("payload") ||
        !doc["payload"].is_object()) {
        return std::nullopt;
    }

    DecodedEnvelope decoded;
    decoded.meta.correlation_id = doc.value("correlation_id", std::uint64_t{0});
    decoded.meta.source_service = doc.value("source_service", std::string{});
    decoded.meta.target_service = doc.value("target_service", std::string{});
    decoded.meta.timeout_ms = doc.value("timeout_ms", std::uint32_t{0});
    decoded.meta.error_code = doc.value("error_code", std::int32_t{0});
    decoded.meta.trace_id = doc.value("trace_id", std::uint64_t{0});
    decoded.meta.span_id = doc.value("span_id", std::uint64_t{0});

    const auto& payloads = doc["payload"];
    if (payloads.size() != 1U) {
        return std::nullopt;
    }
    const auto domain_it = payloads.begin();
    decoded.domain = domain_it.key();
    if (!domain_it.value().is_object() || domain_it.value().size() != 1U) {
        return std::nullopt;
    }
    const auto message_it = domain_it.value().begin();
    decoded.message_name = message_it.key();
    decoded.payload = message_it.value();
    return decoded;
}

inline bool is_envelope_payload(const std::string& encoded) {
    return decode_envelope(encoded).has_value();
}

inline std::string maybe_wrap_response(const std::optional<DecodedEnvelope>& request_envelope,
                                       const std::string& domain,
                                       const std::string& message_name,
                                       const nlohmann::json& payload,
                                       std::int32_t error_code = 0) {
    if (!request_envelope.has_value()) {
        return payload.dump();
    }
    auto meta = request_envelope->meta;
    meta.error_code = error_code;
    return encode_envelope(meta, domain, message_name, payload);
}

}  // namespace v3::proto
