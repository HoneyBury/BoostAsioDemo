#include "v2/battle/message_types.h"
#include "v2/battle/runtime_world.h"
#include "v2/ecs/world.h"
#include "v2/service/backend_envelope.h"
#include "v2/service/backend_server.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

std::unique_ptr<v2::service::BackendServer> g_server;
std::atomic<bool> g_running{true};

struct BattleManager {
    std::unordered_map<std::string, std::unique_ptr<v2::ecs::World>> battles_;
    std::mutex mutex_;

    v2::ecs::World* find(const std::string& battle_id) {
        auto it = battles_.find(battle_id);
        return it != battles_.end() ? it->second.get() : nullptr;
    }

    void insert(const std::string& battle_id, std::unique_ptr<v2::ecs::World> world) {
        battles_[battle_id] = std::move(world);
    }

    void erase(const std::string& battle_id) {
        battles_.erase(battle_id);
    }
};

BattleManager g_battles;

void handle_signal(int) {
    g_running = false;
    if (g_server) {
        std::cout << "\nv2_battle_backend: shutting down..." << std::endl;
        g_server->stop();
    }
}

v2::service::BackendEnvelope make_error(int code, const std::string& reason) {
    v2::service::BackendEnvelope resp;
    resp.kind = v2::service::MessageKind::kError;
    resp.error_code = code;
    nlohmann::json body{{"status", "error"}, {"reason", reason}};
    resp.payload = body.dump();
    return resp;
}

v2::service::BackendEnvelope make_ok(nlohmann::json extra = {}) {
    v2::service::BackendEnvelope resp;
    resp.kind = v2::service::MessageKind::kResponse;
    nlohmann::json body{{"status", "ok"}};
    if (!extra.empty()) {
        for (auto& [key, value] : extra.items()) {
            body[key] = std::move(value);
        }
    }
    resp.payload = body.dump();
    return resp;
}

v2::service::BackendEnvelope handle_battle_create(
    const v2::service::BackendEnvelope& request) {
    auto doc = nlohmann::json::parse(request.payload, nullptr, false);
    if (doc.is_discarded() || !doc.contains("battle_id") || !doc.contains("room_id") ||
        !doc.contains("player_ids")) {
        return make_error(-1004, "invalid_json");
    }

    std::string battle_id = doc["battle_id"].get<std::string>();
    std::string room_id = doc["room_id"].get<std::string>();
    auto player_ids_json = doc["player_ids"];
    std::uint32_t max_frames = doc.value("max_frames", 0);

    if (battle_id.empty() || room_id.empty()) {
        return make_error(-1004, "empty_fields");
    }
    if (!player_ids_json.is_array() || player_ids_json.size() < 2) {
        return make_error(-1004, "need_at_least_two_players");
    }

    std::vector<std::string> player_ids;
    for (const auto& pid : player_ids_json) {
        player_ids.push_back(pid.get<std::string>());
    }

    std::lock_guard<std::mutex> lock(g_battles.mutex_);

    if (g_battles.find(battle_id) != nullptr) {
        return make_error(-2004, "battle_already_exists");
    }

    auto world = v2::battle::create_battle_world(battle_id, room_id, player_ids, max_frames);
    g_battles.insert(battle_id, std::move(world));

    nlohmann::json push{
        {"kind", "battle_started"},
        {"battle_id", battle_id},
        {"room_id", room_id},
        {"player_ids", player_ids_json},
    };

    return make_ok({
        {"battle_id", battle_id},
        {"room_id", room_id},
        {"player_ids", player_ids_json},
        {"push_to_sessions", nlohmann::json::array({std::move(push)})},
    });
}

v2::service::BackendEnvelope handle_battle_input(
    const v2::service::BackendEnvelope& request) {
    auto doc = nlohmann::json::parse(request.payload, nullptr, false);
    if (doc.is_discarded() || !doc.contains("user_id") || !doc.contains("battle_id") ||
        !doc.contains("input_data")) {
        return make_error(-1004, "invalid_json");
    }

    std::string user_id = doc["user_id"].get<std::string>();
    std::string battle_id = doc["battle_id"].get<std::string>();
    std::string input_data = doc["input_data"].get<std::string>();
    std::int64_t score = doc.value("score", 0);
    std::uint32_t submitted_frame = doc.value("submitted_frame", 0);

    std::lock_guard<std::mutex> lock(g_battles.mutex_);

    auto* world = g_battles.find(battle_id);
    if (world == nullptr) {
        return make_error(-2003, "battle_not_found");
    }

    // Process input authoritatively
    auto input_result = v2::battle::battle_world_process_input(
        *world, user_id, input_data, score, submitted_frame);

    if (!input_result.accepted) {
        return make_error(-3002, input_result.reject_reason);
    }

    // Advance one frame
    auto current_frame = v2::battle::battle_world_frame_number(*world);
    auto next_frame = current_frame + 1;
    auto frame_result = v2::battle::battle_world_advance_frame(
        *world, next_frame, "input:" + user_id + ":" + std::to_string(input_result.input_seq));

    // Build push events
    nlohmann::json pushes = nlohmann::json::array();
    auto snapshot = v2::battle::battle_world_snapshot(*world);

    nlohmann::json frame_push{
        {"kind", "frame_advanced"},
        {"battle_id", battle_id},
        {"frame_number", frame_result.frame_number},
        {"trigger", frame_result.trigger},
    };

    // Enrich with participant state
    nlohmann::json participants_json = nlohmann::json::array();
    for (const auto& p : snapshot.participants) {
        participants_json.push_back({
            {"user_id", p.user_id},
            {"online", p.online},
            {"score", p.score},
            {"pos_x", p.pos_x},
            {"pos_y", p.pos_y},
            {"hp", p.hp},
            {"max_hp", p.max_hp},
        });
    }
    frame_push["participants"] = std::move(participants_json);
    pushes.push_back(std::move(frame_push));

    if (frame_result.should_finish) {
        auto participants = v2::battle::battle_world_participants(*world);
        auto summary = v2::battle::battle_world_build_result_summary(
            *world, battle_id,
            v2::battle::battle_world_room_id(*world),
            participants,
            frame_result.finish_reason,
            frame_result.frame_number);

        v2::battle::battle_world_set_lifecycle(
            *world, v2::battle::BattleLifecycleState::kFinished);

        nlohmann::json finish_push{
            {"kind", "battle_finished"},
            {"battle_id", battle_id},
            {"reason", v2::battle::to_string(frame_result.finish_reason)},
            {"total_frames", snapshot.clock.frame_number},
        };
        if (summary.winner_user_id.has_value()) {
            finish_push["winner_user_id"] = *summary.winner_user_id;
        }
        nlohmann::json scores_json = nlohmann::json::array();
        for (const auto& s : summary.scores) {
            scores_json.push_back({{"user_id", s.user_id}, {"score", s.score}});
        }
        finish_push["scores"] = std::move(scores_json);
        pushes.push_back(std::move(finish_push));
    }

    return make_ok({
        {"battle_id", battle_id},
        {"input_seq", input_result.input_seq},
        {"frame_number", frame_result.frame_number},
        {"should_finish", frame_result.should_finish},
        {"push_to_sessions", std::move(pushes)},
    });
}

v2::service::BackendEnvelope handle_battle_finish(
    const v2::service::BackendEnvelope& request) {
    auto doc = nlohmann::json::parse(request.payload, nullptr, false);
    if (doc.is_discarded() || !doc.contains("user_id") || !doc.contains("battle_id")) {
        return make_error(-1004, "invalid_json");
    }

    std::string user_id = doc["user_id"].get<std::string>();
    std::string battle_id = doc["battle_id"].get<std::string>();
    auto reason = doc.contains("reason")
        ? v2::battle::BattleFinishReason::kUserRequested
        : v2::battle::BattleFinishReason::kFinished;

    std::lock_guard<std::mutex> lock(g_battles.mutex_);

    auto* world = g_battles.find(battle_id);
    if (world == nullptr) {
        return make_error(-2003, "battle_not_found");
    }

    auto participants = v2::battle::battle_world_participants(*world);
    auto frame_number = v2::battle::battle_world_frame_number(*world);
    auto summary = v2::battle::battle_world_build_result_summary(
        *world, battle_id, v2::battle::battle_world_room_id(*world),
        participants, reason, frame_number);

    v2::battle::battle_world_set_lifecycle(
        *world, v2::battle::BattleLifecycleState::kFinished);

    nlohmann::json push{
        {"kind", "battle_finished"},
        {"battle_id", battle_id},
        {"reason", v2::battle::to_string(reason)},
        {"total_frames", frame_number},
    };
    if (summary.winner_user_id.has_value()) {
        push["winner_user_id"] = *summary.winner_user_id;
    }
    nlohmann::json scores_json = nlohmann::json::array();
    for (const auto& s : summary.scores) {
        scores_json.push_back({{"user_id", s.user_id}, {"score", s.score}});
    }
    push["scores"] = std::move(scores_json);

    return make_ok({
        {"battle_id", battle_id},
        {"reason", v2::battle::to_string(reason)},
        {"total_frames", frame_number},
        {"push_to_sessions", nlohmann::json::array({std::move(push)})},
    });
}

}  // namespace

int main(int argc, char* argv[]) {
    std::uint16_t port = 9303;
    if (argc > 1) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    v2::service::BackendServer::HandlerMap handlers;
    handlers["battle_create"] = handle_battle_create;
    handlers["battle_input"] = handle_battle_input;
    handlers["battle_finish"] = handle_battle_finish;

    g_server = std::make_unique<v2::service::BackendServer>(port, std::move(handlers));
    std::cout << "v2_battle_backend: starting on port " << port << std::endl;

    g_server->start();

    std::cout << "v2_battle_backend: running (Ctrl+C to stop)" << std::endl;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    g_server->stop();
    std::cout << "v2_battle_backend: stopped" << std::endl;
    return 0;
}
