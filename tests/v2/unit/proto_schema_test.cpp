// v3.0.0 Phase 12: Proto schema validation tests.
// Validates that the .proto definitions are complete and consistent.

#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>

namespace {

#ifndef PROJECT_SOURCE_DIR
#define PROJECT_SOURCE_DIR "."
#endif

std::string proto_path(const std::string& rel) {
    return std::string(PROJECT_SOURCE_DIR) + "/" + rel;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    return content;
}

bool proto_has_message(const std::string& content, const std::string& name) {
    return content.find("message " + name) != std::string::npos;
}

bool proto_has_field(const std::string& content, const std::string& field_name) {
    return content.find(field_name + " =") != std::string::npos;
}

}  // namespace

// ─── Proto file existence ────────────────────────────────────────────────

TEST(ProtoSchemaTest, AllProtoFilesExist) {
    std::vector<std::string> files = {
        "proto/v3/common.proto",
        "proto/v3/login.proto",
        "proto/v3/room.proto",
        "proto/v3/battle.proto",
        "proto/v3/match.proto",
        "proto/v3/leaderboard.proto",
    };
    for (const auto& f : files) {
        auto content = read_file(proto_path(f));
        EXPECT_FALSE(content.empty()) << "Missing proto file: " << f;
    }
}

// ─── Common.proto schema ─────────────────────────────────────────────────

TEST(ProtoSchemaTest, CommonProtoHasServiceEnvelope) {
    auto c = read_file(proto_path("proto/v3/common.proto"));
    EXPECT_TRUE(proto_has_message(c, "ServiceEnvelope"));
    EXPECT_TRUE(proto_has_field(c, "correlation_id"));
    EXPECT_TRUE(proto_has_field(c, "trace_id"));
    EXPECT_TRUE(proto_has_field(c, "span_id"));
    EXPECT_TRUE(proto_has_field(c, "login"));
    EXPECT_TRUE(proto_has_field(c, "room"));
    EXPECT_TRUE(proto_has_field(c, "battle"));
    EXPECT_TRUE(proto_has_field(c, "match"));
    EXPECT_TRUE(proto_has_field(c, "leaderboard"));
}

// ─── Login.proto schema ──────────────────────────────────────────────────

TEST(ProtoSchemaTest, LoginProtoHasAllMessages) {
    auto c = read_file(proto_path("proto/v3/login.proto"));
    EXPECT_TRUE(proto_has_message(c, "LoginRequest"));
    EXPECT_TRUE(proto_has_message(c, "LoginResponse"));
    EXPECT_TRUE(proto_has_message(c, "TokenValidateRequest"));
    EXPECT_TRUE(proto_has_field(c, "user_id"));
    EXPECT_TRUE(proto_has_field(c, "token"));
    EXPECT_TRUE(proto_has_field(c, "role"));
}

// ─── Room.proto schema ───────────────────────────────────────────────────

TEST(ProtoSchemaTest, RoomProtoHasAllMessages) {
    auto c = read_file(proto_path("proto/v3/room.proto"));
    EXPECT_TRUE(proto_has_message(c, "RoomCreateRequest"));
    EXPECT_TRUE(proto_has_message(c, "RoomJoinRequest"));
    EXPECT_TRUE(proto_has_message(c, "RoomLeaveRequest"));
    EXPECT_TRUE(proto_has_message(c, "RoomReadyRequest"));
    EXPECT_TRUE(proto_has_message(c, "RoomStatePush"));
}

// ─── Battle.proto schema ─────────────────────────────────────────────────

TEST(ProtoSchemaTest, BattleProtoHasAllMessages) {
    auto c = read_file(proto_path("proto/v3/battle.proto"));
    EXPECT_TRUE(proto_has_message(c, "BattleCreateRequest"));
    EXPECT_TRUE(proto_has_message(c, "BattleInputRequest"));
    EXPECT_TRUE(proto_has_message(c, "BattleStatePush"));
    EXPECT_TRUE(proto_has_field(c, "input_data"));
    EXPECT_TRUE(proto_has_field(c, "kind"));
    EXPECT_TRUE(proto_has_field(c, "frame_number"));
}

// ─── Match.proto schema ──────────────────────────────────────────────────

TEST(ProtoSchemaTest, MatchProtoHasAllMessages) {
    auto c = read_file(proto_path("proto/v3/match.proto"));
    EXPECT_TRUE(proto_has_message(c, "MatchJoinRequest"));
    EXPECT_TRUE(proto_has_message(c, "MatchFoundPush"));
    EXPECT_TRUE(proto_has_field(c, "mmr"));
    EXPECT_TRUE(proto_has_field(c, "mode"));
    EXPECT_TRUE(proto_has_field(c, "player_ids"));
}

// ─── Leaderboard.proto schema ────────────────────────────────────────────

TEST(ProtoSchemaTest, LeaderboardProtoHasAllMessages) {
    auto c = read_file(proto_path("proto/v3/leaderboard.proto"));
    EXPECT_TRUE(proto_has_message(c, "LeaderboardSubmitRequest"));
    EXPECT_TRUE(proto_has_message(c, "LeaderboardTopRequest"));
    EXPECT_TRUE(proto_has_message(c, "LeaderboardEntry"));
    EXPECT_TRUE(proto_has_field(c, "score"));
    EXPECT_TRUE(proto_has_field(c, "rank"));
}

// ─── Cross-file consistency ──────────────────────────────────────────────

TEST(ProtoSchemaTest, AllProtosUseProto3) {
    std::vector<std::string> files = {
        "proto/v3/common.proto", "proto/v3/login.proto",
        "proto/v3/room.proto", "proto/v3/battle.proto",
        "proto/v3/match.proto", "proto/v3/leaderboard.proto",
    };
    for (const auto& f : files) {
        auto c = read_file(proto_path(f));
        EXPECT_NE(c.find("syntax = \"proto3\""), std::string::npos)
            << f << " missing proto3 syntax";
        EXPECT_NE(c.find("package boost.gateway.v3"), std::string::npos)
            << f << " missing package declaration";
    }
}
