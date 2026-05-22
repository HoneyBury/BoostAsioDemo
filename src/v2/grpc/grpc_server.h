#pragma once

// grpc_server.h — Generic gRPC server base class with lifecycle management,
// health check endpoint, and error code translation.
//
// Conditionally compiled only when BOOST_BUILD_GRPC is defined.

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <spdlog/spdlog.h>

namespace v2::grpc {

// -------------------------------------------------------------------
// ErrorCodeMapper: maps net::protocol::ErrorCode to gRPC StatusCode.
// -------------------------------------------------------------------
struct ErrorCodeMapper {
  /// Convert a project-internal error code to a gRPC status code.
  /// kOk (0) -> OK, positive values -> specific codes, negative -> Unknown.
  static grpc::StatusCode to_grpc_status(std::int32_t error_code) noexcept {
    using net::protocol::ErrorCode;

    switch (static_cast<ErrorCode>(error_code)) {
      case ErrorCode::kOk:
        return grpc::StatusCode::OK;
      case ErrorCode::kAuthRequired:
      case ErrorCode::kInvalidToken:
      case ErrorCode::kTokenExpired:
        return grpc::StatusCode::UNAUTHENTICATED;
      case ErrorCode::kInvalidUserId:
      case ErrorCode::kInvalidRoomId:
      case ErrorCode::kNotInRoom:
      case ErrorCode::kNotRoomOwner:
        return grpc::StatusCode::INVALID_ARGUMENT;
      case ErrorCode::kDuplicateLogin:
        return grpc::StatusCode::ALREADY_EXISTS;
      case ErrorCode::kRoomNotFound:
        return grpc::StatusCode::NOT_FOUND;
      case ErrorCode::kRoomAlreadyExists:
        return grpc::StatusCode::ALREADY_EXISTS;
      case ErrorCode::kRateLimited:
        return grpc::StatusCode::RESOURCE_EXHAUSTED;
      case ErrorCode::kSessionNotFound:
        return grpc::StatusCode::NOT_FOUND;
      default:
        return grpc::StatusCode::UNKNOWN;
    }
  }

  /// Build a grpc::Status from a project error code and optional message.
  static grpc::Status from_error_code(std::int32_t error_code,
                                      std::string message = {}) {
    if (error_code == 0) {
      return grpc::Status::OK;
    }
    if (message.empty()) {
      message = net::protocol::to_string(static_cast<net::protocol::ErrorCode>(error_code));
    }
    return grpc::Status(to_grpc_status(error_code), std::move(message));
  }
};

// -------------------------------------------------------------------
// GrpcServer base class
// -------------------------------------------------------------------
class GrpcServer {
 public:
  explicit GrpcServer(std::string server_name, std::uint16_t port)
      : server_name_(std::move(server_name)),
        port_(port) {}

  virtual ~GrpcServer() = default;

  // Non-copyable, non-movable.
  GrpcServer(const GrpcServer&) = delete;
  GrpcServer& operator=(const GrpcServer&) = delete;

  /// Start the gRPC server on the configured port.
  /// Returns true if the server started successfully.
  bool start() {
    if (server_) {
      SPDLOG_WARN("{}: gRPC server already running", server_name_);
      return false;
    }

    start_time_ = std::chrono::steady_clock::now();

    // Build server address string
    const std::string address = fmt::format("0.0.0.0:{}", port_);
    SPDLOG_INFO("{}: starting gRPC server on {}", server_name_, address);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    // Let subclasses register services before building
    register_services(builder_);

    builder_.AddListeningPort(address, grpc::InsecureServerCredentials());

    // Optional: set server-level resource limits
    builder_.SetMaxReceiveMessageSize(4 * 1024 * 1024);   // 4 MB
    builder_.SetMaxSendMessageSize(4 * 1024 * 1024);      // 4 MB

    server_ = builder_.BuildAndStart();
    if (!server_) {
      SPDLOG_ERROR("{}: failed to start gRPC server on {}", server_name_, address);
      return false;
    }

    SPDLOG_INFO("{}: gRPC server listening on {}", server_name_, address);
    return true;
  }

  /// Gracefully shut down the server.
  void shutdown() {
    if (server_) {
      SPDLOG_INFO("{}: shutting down gRPC server", server_name_);
      server_->Shutdown();
      server_.reset();
    }
  }

  /// Block until the server is shut down (for main-loop integration).
  void wait() {
    if (server_) {
      server_->Wait();
    }
  }

  /// Get local port.
  std::uint16_t port() const noexcept { return port_; }

  /// Get server name.
  const std::string& server_name() const noexcept { return server_name_; }

  /// Uptime in seconds since start() was called.
  std::uint64_t uptime_seconds() const {
    const auto now = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count());
  }

 protected:
  /// Subclasses override this to register gRPC services on the builder.
  virtual void register_services(grpc::ServerBuilder& builder) = 0;

  grpc::ServerBuilder builder_;
  std::unique_ptr<grpc::Server> server_;
  std::chrono::steady_clock::time_point start_time_;

 private:
  std::string server_name_;
  std::uint16_t port_;
};

}  // namespace v2::grpc
