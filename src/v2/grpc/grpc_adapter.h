#pragma once

// grpc_adapter.h — GrpcGatewayAdapter: dual-stack adapter that wraps
// GatewayGrpcServer into the same lifecycle conventions used by existing
// TCP-based services (room_backend_service, leaderboard_service, etc.).
//
// The adapter lets the demo main function choose between TCP and gRPC at
// startup via a --grpc flag, without modifying any existing TCP code path.
//
// Usage:
//   v2::grpc::GrpcGatewayAdapter adapter(50051);
//   adapter.start();
//   // ... main loop ...
//   adapter.stop();
//
// Conditionally compiled only when BOOST_BUILD_GRPC is defined.

#ifdef BOOST_BUILD_GRPC

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "v2/grpc/gateway_grpc_server.h"

namespace v2::grpc {

// -------------------------------------------------------------------
// GrpcGatewayAdapter: adapter between existing v2 service conventions
// and the gRPC server lifecycle.
//
// The adapter owns a GatewayGrpcServer and a dedicated thread for
// polling the gRPC completion queue. This mirrors the pattern used
// by BackendServer and other TCP-based services.
// -------------------------------------------------------------------
class GrpcGatewayAdapter {
 public:
  /// Construct a gRPC gateway adapter.
  /// @param port  gRPC server port (default 50051).
  explicit GrpcGatewayAdapter(std::uint16_t port = 50051)
      : port_(port) {}

  ~GrpcGatewayAdapter() {
    stop();
  }

  // Non-copyable, non-movable.
  GrpcGatewayAdapter(const GrpcGatewayAdapter&) = delete;
  GrpcGatewayAdapter& operator=(const GrpcGatewayAdapter&) = delete;

  /// Start the gRPC server and a background polling thread.
  bool start() {
    if (running_.load(std::memory_order_relaxed)) {
      SPDLOG_WARN("GrpcGatewayAdapter: already running");
      return false;
    }

    SPDLOG_INFO("GrpcGatewayAdapter: starting gRPC gateway on port {}", port_);

    // Create and start the server
    grpc_server_ = std::make_unique<GatewayGrpcServer>(
        port_,
        // Default login auth callback (always allow)
        [](const std::string& user_id,
           const std::string& token,
           std::string& /*out_error*/) -> bool {
          SPDLOG_DEBUG("GrpcGatewayAdapter: login auth user={} token={}",
                       user_id, token);
          return true;  // Accept all; real auth would validate the token
        },
        // Default logout callback
        [](const std::string& user_id, const std::string& session_id) {
          SPDLOG_INFO("GrpcGatewayAdapter: logout user={} session={}",
                      user_id, session_id);
        });

    if (!grpc_server_->start()) {
      SPDLOG_ERROR("GrpcGatewayAdapter: failed to start gRPC server");
      grpc_server_.reset();
      return false;
    }

    // Seed the completion queue with initial CallData instances
    grpc_server_->seed_completion_queue();

    running_.store(true, std::memory_order_release);

    // Start background polling thread for the completion queue.
    poll_thread_ = std::thread([this]() {
      SPDLOG_DEBUG("GrpcGatewayAdapter: poll thread started");
      auto* cq = grpc_server_->completion_queue();
      if (!cq) {
        SPDLOG_ERROR("GrpcGatewayAdapter: no completion queue available");
        return;
      }

      void* tag = nullptr;
      bool ok = false;

      while (running_.load(std::memory_order_relaxed)) {
        if (cq->Next(&tag, &ok)) {
          auto* call_data = static_cast<GatewayGrpcServer::CallData*>(tag);
          call_data->proceed(ok);
        } else {
          // CQ shut down
          break;
        }
      }

      SPDLOG_DEBUG("GrpcGatewayAdapter: poll thread exiting");
    });

    SPDLOG_INFO("GrpcGatewayAdapter: gRPC gateway started on port {}", port_);
    return true;
  }

  /// Gracefully stop the gRPC server.
  void stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
      return;
    }

    SPDLOG_INFO("GrpcGatewayAdapter: stopping gRPC gateway");

    if (grpc_server_) {
      grpc_server_->shutdown();
    }

    auto* cq = grpc_server_ ? grpc_server_->completion_queue() : nullptr;
    if (cq) {
      cq->Shutdown();
    }

    if (poll_thread_.joinable()) {
      poll_thread_.join();
    }

    grpc_server_.reset();
    SPDLOG_INFO("GrpcGatewayAdapter: gRPC gateway stopped");
  }

  /// Get the local port.
  std::uint16_t port() const noexcept { return port_; }

  /// Whether the adapter is currently running.
  bool is_running() const noexcept {
    return running_.load(std::memory_order_relaxed);
  }

  /// Number of active sessions tracked by the gRPC server.
  std::uint32_t active_sessions() const noexcept {
    return grpc_server_ ? grpc_server_->active_sessions() : 0;
  }

 private:
  std::uint16_t port_;
  std::unique_ptr<GatewayGrpcServer> grpc_server_;
  std::thread poll_thread_;
  std::atomic<bool> running_{false};
};

}  // namespace v2::grpc

#endif  // BOOST_BUILD_GRPC
