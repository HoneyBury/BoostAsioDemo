#include "v2/service/backend_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

std::unique_ptr<v2::service::BackendServer> g_server;
std::atomic<bool> g_running{true};

void handle_signal(int) {
    g_running = false;
    if (g_server) {
        std::cout << "\nv2_login_backend: shutting down..." << std::endl;
        g_server->stop();
    }
}

v2::service::BackendEnvelope handle_login_request(
    const v2::service::BackendEnvelope& request) {
    auto& payload = request.payload;

    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kResponse;

    if (payload.empty()) {
        response.kind = v2::service::MessageKind::kError;
        response.error_code = -1004;
        return response;
    }

    auto first_pipe = payload.find('|');
    if (first_pipe == std::string::npos) {
        response.kind = v2::service::MessageKind::kError;
        response.error_code = -1004;
        return response;
    }

    std::string user_id = payload.substr(0, first_pipe);

    auto token_start = payload.find("token:");
    if (token_start == std::string::npos || token_start + 6 >= payload.size()) {
        response.kind = v2::service::MessageKind::kError;
        response.error_code = -1004;
        response.payload = "empty_token";
        return response;
    }

    response.payload = "login_ok:" + user_id;
    return response;
}

v2::service::BackendEnvelope handle_token_validate(
    const v2::service::BackendEnvelope& request) {
    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kResponse;

    auto& payload = request.payload;
    if (payload.empty() || payload.find("token:") == std::string::npos) {
        response.kind = v2::service::MessageKind::kError;
        response.error_code = -1004;
    } else {
        response.payload = "token_valid";
    }
    return response;
}

v2::service::BackendEnvelope handle_session_bind(
    const v2::service::BackendEnvelope& /*request*/) {
    v2::service::BackendEnvelope response;
    response.kind = v2::service::MessageKind::kResponse;
    response.payload = "session_bound";
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
