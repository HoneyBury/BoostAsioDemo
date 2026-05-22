#pragma once
// v2.3.0 G1: Match protocol definitions for the matchmaking-to-battle flow.
// Defines MatchFound push format, MatchToRoom request/response types.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace v2::match {

// ── MatchPlayerInfo ───────────────────────────────────────────────────
// Per-player information included in MatchFound push notifications.

struct MatchPlayerInfo {
    std::string user_id;
    std::int64_t mmr = 0;
};

// ── MatchFoundPayload ─────────────────────────────────────────────────
// Payload carried in the kMatchFoundPush message sent to all matched
// players when the matchmaking service finds a match.

struct MatchFoundPayload {
    std::string match_id;
    std::vector<MatchPlayerInfo> players;
    std::string mode;  // "1v1", "2v2", or "4v4"
    std::string room_id;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json players_json = nlohmann::json::array();
        for (const auto& p : players) {
            players_json.push_back({{"user_id", p.user_id}, {"mmr", p.mmr}});
        }
        return {
            {"match_id", match_id},
            {"players", std::move(players_json)},
            {"mode", mode},
            {"room_id", room_id},
        };
    }

    [[nodiscard]] std::string to_json_str() const {
        return to_json().dump();
    }

    static MatchFoundPayload from_json(const nlohmann::json& j) {
        MatchFoundPayload p;
        p.match_id = j.value("match_id", "");
        p.mode = j.value("mode", "1v1");
        p.room_id = j.value("room_id", "");
        if (j.contains("players") && j["players"].is_array()) {
            for (const auto& player_j : j["players"]) {
                MatchPlayerInfo info;
                info.user_id = player_j.value("user_id", "");
                info.mmr = player_j.value("mmr", std::int64_t{0});
                p.players.push_back(std::move(info));
            }
        }
        return p;
    }

    static MatchFoundPayload from_json_str(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str, nullptr, false);
        if (j.is_discarded()) return {};
        return from_json(j);
    }
};

// ── MatchToRoomRequest ────────────────────────────────────────────────
// Sent by the matchmaking service (or gateway) to the room backend to
// request automatic room creation after a successful match.

struct MatchToRoomRequest {
    std::string match_id;
    std::vector<MatchPlayerInfo> players;
    std::string mode;
    std::string room_id;

    [[nodiscard]] nlohmann::json to_json() const {
        nlohmann::json players_json = nlohmann::json::array();
        for (const auto& p : players) {
            players_json.push_back({{"user_id", p.user_id}, {"mmr", p.mmr}});
        }
        return {
            {"match_id", match_id},
            {"players", std::move(players_json)},
            {"mode", mode},
            {"room_id", room_id},
        };
    }

    [[nodiscard]] std::string to_json_str() const {
        return to_json().dump();
    }

    static MatchToRoomRequest from_json(const nlohmann::json& j) {
        MatchToRoomRequest req;
        req.match_id = j.value("match_id", "");
        req.mode = j.value("mode", "1v1");
        req.room_id = j.value("room_id", "");
        if (j.contains("players") && j["players"].is_array()) {
            for (const auto& player_j : j["players"]) {
                MatchPlayerInfo info;
                info.user_id = player_j.value("user_id", "");
                info.mmr = player_j.value("mmr", std::int64_t{0});
                req.players.push_back(std::move(info));
            }
        }
        return req;
    }

    static MatchToRoomRequest from_json_str(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str, nullptr, false);
        if (j.is_discarded()) return {};
        return from_json(j);
    }
};

// ── MatchToRoomResponse ───────────────────────────────────────────────
// Response from the room backend after processing a MatchToRoomRequest.

struct MatchToRoomResponse {
    bool success = false;
    std::string room_id;
    std::string error_message;

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"success", success},
            {"room_id", room_id},
            {"error_message", error_message},
        };
    }

    [[nodiscard]] std::string to_json_str() const {
        return to_json().dump();
    }

    static MatchToRoomResponse from_json(const nlohmann::json& j) {
        MatchToRoomResponse resp;
        resp.success = j.value("success", false);
        resp.room_id = j.value("room_id", "");
        resp.error_message = j.value("error_message", "");
        return resp;
    }

    static MatchToRoomResponse from_json_str(const std::string& json_str) {
        auto j = nlohmann::json::parse(json_str, nullptr, false);
        if (j.is_discarded()) return {};
        return from_json(j);
    }
};

// ── MatchTimeoutPayload ───────────────────────────────────────────────
// Sent when a player's matchmaking request times out.

struct MatchTimeoutPayload {
    std::string user_id;
    std::string mode;
    std::string reason;

    [[nodiscard]] nlohmann::json to_json() const {
        return {
            {"user_id", user_id},
            {"mode", mode},
            {"reason", reason},
        };
    }

    static MatchTimeoutPayload from_json(const nlohmann::json& j) {
        return {
            .user_id = j.value("user_id", ""),
            .mode = j.value("mode", "1v1"),
            .reason = j.value("reason", "timeout"),
        };
    }
};

}  // namespace v2::match
