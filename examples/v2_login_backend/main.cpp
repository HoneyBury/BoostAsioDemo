#include "v2/service/backend_server.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

std::unique_ptr<v2::service::BackendServer> g_server;
std::atomic<bool> g_running{true};

// Per-backend login state (would be a database in production)
struct BackendPlayerState {
    std::unordered_map<std::string, std::string> active_sessions_;  // user_id → session_id
    std::unordered_map<std::string, std::string> user_tokens_;      // user_id → token

    std::mutex mutex_;
};

BackendPlayerState g_state;

void handle_signal(int) {
    g_running = false;
    if (g_server) {
        std::cout << "\nv2_login_backend: shutting down..." << std::endl;
        g_server->stop();
    }
}

v2::service::BackendEnvelope make_error_response(int error_code,
                                                  const std::string& reason) {
    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kError;
    response.error_code = error_code;
    nlohmann::json body{{"status", "error"}, {"reason", reason}};
    response.payload = body.dump();
    return response;
}

v2::service::BackendEnvelope make_ok_response(const std::string& user_id,
                                               const std::string& display_name,
                                               bool is_duplicate = false) {
    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kResponse;
    nlohmann::json body{
        {"status", "ok"},
        {"user_id", user_id},
        {"display_name", display_name},
        {"is_duplicate", is_duplicate},
    };
    response.payload = body.dump();
    return response;
}

v2::service::BackendEnvelope handle_login_request(
    const v2::service::BackendEnvelope& request) {

    if (request.payload.empty()) {
        return make_error_response(-1004, "empty_payload");
    }

    auto doc = nlohmann::json::parse(request.payload, nullptr, false);
    if (doc.is_discarded() || !doc.contains("user_id") || !doc.contains("token")) {
        return make_error_response(-1004, "invalid_json");
    }

    std::string user_id = doc["user_id"].get<std::string>();
    std::string token = doc["token"].get<std::string>();
    std::string display_name = doc.value("display_name", user_id);

    if (user_id.empty()) {
        return make_error_response(-1004, "empty_user_id");
    }

    // Token validation: non-empty token required
    if (token.empty()) {
        return make_error_response(-1004, "empty_token");
    }

    // Minimal token format: "token:<user_id>" or any non-empty string
    auto colon_pos = token.find(':');
    std::string token_user_id = (colon_pos != std::string::npos)
        ? token.substr(colon_pos + 1) : token;

    if (token_user_id.empty()) {
        return make_error_response(-1004, "invalid_token_format");
    }

    // Accept the token (dev mode: any non-empty token passes)
    std::lock_guard<std::mutex> lock(g_state.mutex_);

    bool is_duplicate = false;
    auto it = g_state.active_sessions_.find(user_id);
    if (it != g_state.active_sessions_.end()) {
        is_duplicate = true;
    }

    // Record the active session and token
    g_state.active_sessions_[user_id] = token_user_id;
    g_state.user_tokens_[user_id] = token;

    return make_ok_response(user_id, display_name, is_duplicate);
}

v2::service::BackendEnvelope handle_token_validate(
    const v2::service::BackendEnvelope& request) {
    auto doc = nlohmann::json::parse(request.payload, nullptr, false);
    if (doc.is_discarded() || !doc.contains("token")) {
        return make_error_response(-1004, "invalid_json");
    }

    std::string token = doc["token"].get<std::string>();
    bool valid = !token.empty();

    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kResponse;
    nlohmann::json body{{"valid", valid}};
    response.payload = body.dump();
    return response;
}

v2::service::BackendEnvelope handle_session_bind(
    const v2::service::BackendEnvelope& request) {
    auto doc = nlohmann::json::parse(request.payload, nullptr, false);
    if (doc.is_discarded() || !doc.contains("user_id")) {
        return make_error_response(-1004, "invalid_json");
    }

    std::string user_id = doc["user_id"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_state.mutex_);
    g_state.active_sessions_[user_id] = user_id;

    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kResponse;
    response.payload = R"({"status":"ok","action":"session_bound"})";
    return response;
}

v2::service::BackendEnvelope handle_session_close(
    const v2::service::BackendEnvelope& request) {
    auto doc = nlohmann::json::parse(request.payload, nullptr, false);
    if (doc.is_discarded() || !doc.contains("user_id")) {
        return make_error_response(-1004, "invalid_json");
    }

    std::string user_id = doc["user_id"].get<std::string>();

    std::lock_guard<std::mutex> lock(g_state.mutex_);
    g_state.active_sessions_.erase(user_id);

    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kResponse;
    response.payload = R"({"status":"ok","action":"session_closed"})";
    return response;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::uint16_t port = 9202;
    if (argc > 1) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    v2::service::BackendServer::HandlerMap handlers;
    handlers["login_request"] = handle_login_request;
    handlers["token_validate"] = handle_token_validate;
    handlers["session_bind"] = handle_session_bind;
    handlers["session_close"] = handle_session_close;

    g_server = std::make_unique<v2::service::BackendServer>(port, std::move(handlers));
    std::cout << "v2_login_backend: starting on port " << port << std::endl;

    g_server->start();

    std::cout << "v2_login_backend: running (Ctrl+C to stop)" << std::endl;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    g_server->stop();
    std::cout << "v2_login_backend: stopped" << std::endl;
    return 0;
}
