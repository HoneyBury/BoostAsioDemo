#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "v2/service/backend_envelope.h"
#include "v2/service/error_codes.h"
#include "v2/service/service_id.h"
#include "v2/service/service_manifest.h"

// ─── ServiceId ─────────────────────────────────────────────────

TEST(V2ServiceBoundaryTest, ServiceIdToString) {
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceId::kGateway), "gateway");
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceId::kLogin), "login");
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceId::kRoom), "room");
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceId::kBattle), "battle");
}

// ─── MessageKind ───────────────────────────────────────────────

TEST(V2ServiceBoundaryTest, MessageKindToString) {
    EXPECT_STREQ(v2::service::to_string(v2::service::MessageKind::kRequest), "request");
    EXPECT_STREQ(v2::service::to_string(v2::service::MessageKind::kResponse), "response");
    EXPECT_STREQ(v2::service::to_string(v2::service::MessageKind::kPush), "push");
    EXPECT_STREQ(v2::service::to_string(v2::service::MessageKind::kError), "error");
}

// ─── BackendEnvelope: JSON Round-Trip ──────────────────────────

TEST(V2ServiceBoundaryTest, BackendEnvelopeJsonRoundTrip) {
    v2::service::BackendEnvelope original{
        .correlation_id = 42,
        .source_service = v2::service::ServiceId::kGateway,
        .target_service = v2::service::ServiceId::kLogin,
        .kind = v2::service::MessageKind::kRequest,
        .timeout_ms = 5000,
        .error_code = 0,
        .payload = R"({"user_id":"alice","token":"xyz"})",
    };

    const auto json = v2::service::to_json(original);
    const auto parsed = v2::service::from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->correlation_id, 42U);
    EXPECT_EQ(parsed->source_service, v2::service::ServiceId::kGateway);
    EXPECT_EQ(parsed->target_service, v2::service::ServiceId::kLogin);
    EXPECT_EQ(parsed->kind, v2::service::MessageKind::kRequest);
    EXPECT_EQ(parsed->timeout_ms, 5000U);
    EXPECT_EQ(parsed->payload, R"({"user_id":"alice","token":"xyz"})");
}

TEST(V2ServiceBoundaryTest, BackendEnvelopeMessageTypeRoundTrip) {
    v2::service::BackendEnvelope original{
        .correlation_id = 100,
        .source_service = v2::service::ServiceId::kGateway,
        .target_service = v2::service::ServiceId::kLogin,
        .kind = v2::service::MessageKind::kRequest,
        .payload = R"({"user_id":"bob"})",
        .message_type = "login_request",
    };

    const auto json = v2::service::to_json(original);
    const auto parsed = v2::service::from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->message_type, "login_request");
}

TEST(V2ServiceBoundaryTest, FromJsonMessageTypeDefaultsToEmpty) {
    // JSON without message_type (old format) still parses, message_type is ""
    const auto parsed = v2::service::from_json(
        R"({"correlation_id":1,"source_service":"gateway","target_service":"login","kind":"request","payload":"{}"})"
    );
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->message_type.empty());
}

TEST(V2ServiceBoundaryTest, BackendEnvelopeResponseRoundTrip) {
    v2::service::BackendEnvelope original{
        .correlation_id = 99,
        .source_service = v2::service::ServiceId::kLogin,
        .target_service = v2::service::ServiceId::kGateway,
        .kind = v2::service::MessageKind::kResponse,
        .timeout_ms = 0,
        .payload = R"({"status":"ok"})",
    };

    const auto json = v2::service::to_json(original);
    const auto parsed = v2::service::from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->kind, v2::service::MessageKind::kResponse);
    EXPECT_EQ(parsed->source_service, v2::service::ServiceId::kLogin);
    EXPECT_EQ(parsed->target_service, v2::service::ServiceId::kGateway);
}

TEST(V2ServiceBoundaryTest, BackendEnvelopeErrorRoundTrip) {
    v2::service::BackendEnvelope original{
        .correlation_id = 77,
        .source_service = v2::service::ServiceId::kBattle,
        .target_service = v2::service::ServiceId::kGateway,
        .kind = v2::service::MessageKind::kError,
        .timeout_ms = 0,
        .error_code = -1003,
        .payload = "",
    };

    const auto json = v2::service::to_json(original);
    const auto parsed = v2::service::from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->kind, v2::service::MessageKind::kError);
    EXPECT_EQ(parsed->error_code, -1003);
}

TEST(V2ServiceBoundaryTest, BackendEnvelopePushRoundTrip) {
    v2::service::BackendEnvelope original{
        .correlation_id = 1,
        .source_service = v2::service::ServiceId::kRoom,
        .target_service = v2::service::ServiceId::kGateway,
        .kind = v2::service::MessageKind::kPush,
        .payload = R"({"event":"player_joined"})",
    };

    const auto json = v2::service::to_json(original);
    const auto parsed = v2::service::from_json(json);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->kind, v2::service::MessageKind::kPush);
}

// ─── BackendEnvelope: Validation ────────────────────────────────

TEST(V2ServiceBoundaryTest, IsValidRejectsZeroCorrelationId) {
    v2::service::BackendEnvelope envelope{
        .correlation_id = 0,
        .source_service = v2::service::ServiceId::kGateway,
        .target_service = v2::service::ServiceId::kLogin,
        .kind = v2::service::MessageKind::kRequest,
        .payload = "{}",
    };
    EXPECT_FALSE(v2::service::is_valid(envelope));
}

TEST(V2ServiceBoundaryTest, IsValidAcceptsErrorWithoutPayload) {
    v2::service::BackendEnvelope envelope{
        .correlation_id = 5,
        .source_service = v2::service::ServiceId::kLogin,
        .target_service = v2::service::ServiceId::kGateway,
        .kind = v2::service::MessageKind::kError,
        .error_code = -1001,
    };
    EXPECT_TRUE(v2::service::is_valid(envelope));
}

TEST(V2ServiceBoundaryTest, IsValidRejectsRequestWithoutPayload) {
    v2::service::BackendEnvelope envelope{
        .correlation_id = 5,
        .source_service = v2::service::ServiceId::kGateway,
        .target_service = v2::service::ServiceId::kLogin,
        .kind = v2::service::MessageKind::kRequest,
    };
    EXPECT_FALSE(v2::service::is_valid(envelope));
}

TEST(V2ServiceBoundaryTest, FromJsonRejectsInvalidJson) {
    EXPECT_FALSE(v2::service::from_json("not json").has_value());
}

TEST(V2ServiceBoundaryTest, FromJsonRejectsMissingFields) {
    EXPECT_FALSE(v2::service::from_json(R"({"correlation_id":1})").has_value());
}

TEST(V2ServiceBoundaryTest, FromJsonRejectsUnknownService) {
    EXPECT_FALSE(v2::service::from_json(
        R"({"correlation_id":1,"source_service":"unknown","target_service":"gateway","kind":"request","payload":"{}"})"
    ).has_value());
}

// ─── CorrelationId Generation ──────────────────────────────────

TEST(V2ServiceBoundaryTest, GenerateCorrelationIdIsMonotonic) {
    const auto first = v2::service::generate_correlation_id();
    const auto second = v2::service::generate_correlation_id();
    EXPECT_LT(first, second);
}

// ─── Service Manifest: Ownership ───────────────────────────────

TEST(V2ServiceBoundaryTest, GatewayManifestOwnsSessions) {
    const auto manifest = v2::service::gateway_manifest();
    EXPECT_EQ(manifest.service_id, v2::service::ServiceId::kGateway);

    bool owns_session = false;
    for (const auto& state : manifest.owned_state) {
        if (state == "session") owns_session = true;
    }
    EXPECT_TRUE(owns_session);
}

TEST(V2ServiceBoundaryTest, LoginManifestOwnsPlayerAuth) {
    const auto manifest = v2::service::login_manifest();
    EXPECT_EQ(manifest.service_id, v2::service::ServiceId::kLogin);

    bool owns_auth = false;
    for (const auto& state : manifest.owned_state) {
        if (state == "player_auth") owns_auth = true;
    }
    EXPECT_TRUE(owns_auth);
}

TEST(V2ServiceBoundaryTest, RoomManifestOwnsRooms) {
    const auto manifest = v2::service::room_manifest();
    EXPECT_EQ(manifest.service_id, v2::service::ServiceId::kRoom);

    bool owns_room = false;
    for (const auto& state : manifest.owned_state) {
        if (state == "room") owns_room = true;
    }
    EXPECT_TRUE(owns_room);
}

TEST(V2ServiceBoundaryTest, BattleManifestOwnsFrames) {
    const auto manifest = v2::service::battle_manifest();
    EXPECT_EQ(manifest.service_id, v2::service::ServiceId::kBattle);

    bool owns_frame = false;
    bool owns_replay = false;
    for (const auto& state : manifest.owned_state) {
        if (state == "frame") owns_frame = true;
        if (state == "replay") owns_replay = true;
    }
    EXPECT_TRUE(owns_frame);
    EXPECT_TRUE(owns_replay);
}

// ─── Owner / Handler Lookup ────────────────────────────────────

TEST(V2ServiceBoundaryTest, OwnerLookupReturnsCorrectService) {
    EXPECT_EQ(v2::service::owner_of("session"), v2::service::ServiceId::kGateway);
    EXPECT_EQ(v2::service::owner_of("player_auth"), v2::service::ServiceId::kLogin);
    EXPECT_EQ(v2::service::owner_of("room"), v2::service::ServiceId::kRoom);
    EXPECT_EQ(v2::service::owner_of("battle"), v2::service::ServiceId::kBattle);
    EXPECT_EQ(v2::service::owner_of("replay"), v2::service::ServiceId::kBattle);
}

TEST(V2ServiceBoundaryTest, HandlerLookupReturnsCorrectService) {
    EXPECT_EQ(v2::service::handler_of("login_request"), v2::service::ServiceId::kLogin);
    EXPECT_EQ(v2::service::handler_of("room_create"), v2::service::ServiceId::kRoom);
    EXPECT_EQ(v2::service::handler_of("battle_input"), v2::service::ServiceId::kBattle);
}

TEST(V2ServiceBoundaryTest, OwnerLookupUnknownReturnsGateway) {
    EXPECT_EQ(v2::service::owner_of("nonexistent_state"), v2::service::ServiceId::kGateway);
}

// ─── Error Codes ───────────────────────────────────────────────

TEST(V2ServiceBoundaryTest, ErrorCodeToString) {
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceErrorCode::kOk), "ok");
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceErrorCode::kTimeout), "timeout");
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceErrorCode::kUnavailable), "unavailable");
    EXPECT_STREQ(v2::service::to_string(v2::service::ServiceErrorCode::kRejected), "rejected");
}

TEST(V2ServiceBoundaryTest, ErrorCodeToClientMapping) {
    EXPECT_EQ(v2::service::to_client_error(v2::service::ServiceErrorCode::kOk), 0);
    EXPECT_EQ(v2::service::to_client_error(v2::service::ServiceErrorCode::kTimeout), -2001);
    EXPECT_EQ(v2::service::to_client_error(v2::service::ServiceErrorCode::kUnavailable), -2002);
    EXPECT_EQ(v2::service::to_client_error(v2::service::ServiceErrorCode::kRejected), -2003);
    EXPECT_EQ(v2::service::to_client_error(v2::service::ServiceErrorCode::kInvalidRequest), -2004);
    EXPECT_EQ(v2::service::to_client_error(v2::service::ServiceErrorCode::kInternalError), -2005);
    EXPECT_EQ(v2::service::to_client_error(v2::service::ServiceErrorCode::kNotImplemented), -2006);
}

// ─── All Manifests Are Consistent ───────────────────────────────

TEST(V2ServiceBoundaryTest, AllManifestsHaveUniqueServiceIds) {
    std::vector<v2::service::ServiceId> ids;
    ids.push_back(v2::service::gateway_manifest().service_id);
    ids.push_back(v2::service::login_manifest().service_id);
    ids.push_back(v2::service::room_manifest().service_id);
    ids.push_back(v2::service::battle_manifest().service_id);

    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            EXPECT_NE(ids[i], ids[j]);
        }
    }
}

TEST(V2ServiceBoundaryTest, AllManifestsHaveDescriptions) {
    EXPECT_FALSE(v2::service::gateway_manifest().description.empty());
    EXPECT_FALSE(v2::service::login_manifest().description.empty());
    EXPECT_FALSE(v2::service::room_manifest().description.empty());
    EXPECT_FALSE(v2::service::battle_manifest().description.empty());
}
