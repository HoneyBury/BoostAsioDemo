#pragma once

#include "v2/service/backend_envelope.h"
#include "v3/proto/envelope_codec.h"

#include <optional>
#include <string>
#include <string_view>

namespace v2::service {

struct DecodedHandlerPayload {
    std::optional<v3::proto::TypedEnvelope> typed_request;
    nlohmann::json payload;
};

[[nodiscard]] std::optional<v3::proto::EnvelopeMessageKind>
message_kind_from_backend_type(std::string_view message_type);

[[nodiscard]] std::string backend_type_from_message_kind(v3::proto::EnvelopeMessageKind kind);

[[nodiscard]] std::optional<v3::proto::TypedEnvelope>
to_typed_envelope(const BackendEnvelope& envelope);

[[nodiscard]] BackendEnvelope to_backend_envelope(const v3::proto::TypedEnvelope& envelope,
                                                  MessageKind kind = MessageKind::kRequest);

[[nodiscard]] std::optional<DecodedHandlerPayload>
decode_handler_payload(const BackendEnvelope& envelope);

[[nodiscard]] BackendEnvelope wrap_typed_response_if_needed(
    const std::optional<v3::proto::TypedEnvelope>& request_envelope,
    BackendEnvelope response,
    v3::proto::EnvelopeMessageKind response_kind);

}  // namespace v2::service
