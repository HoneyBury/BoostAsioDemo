#pragma once

// gateway_grpc_server.h — Gateway gRPC service implementation.
//
// Implements the Gateway::AsyncService with RequestLogin and RequestLogout
// RPCs. Delegates business logic to the existing gateway command parser and
// validation layer from v2/gateway/gateway_command_parser.h.
//
// Conditionally compiled only when BOOST_BUILD_GRPC is defined.

#ifdef BOOST_BUILD_GRPC

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>

#include "v2/grpc/grpc_server.h"

// Generated protobuf/gRPC headers — resolved via project_proto include path
#include <gateway.pb.h>
#include <gateway.grpc.pb.h>

namespace v2::grpc {

// -------------------------------------------------------------------
// GatewayGrpcServer: implements the Gateway gRPC service.
//
// Lifecycle:
//   1. Construct with a port and optional callback.
//   2. Call start() to begin serving.
//   3. Call shutdown() for graceful stop.
//
// The async gRPC API uses per-RPC CallData objects. Each CallData
// registers itself with the AsyncService to receive the next incoming
// request of its type. When a request arrives and is processed, the
// CallData spawns a replacement to handle the next request.
//
// External code drives the completion queue via:
//   auto* cq = server.completion_queue();
//   void* tag; bool ok;
//   while (cq->Next(&tag, &ok)) {
//       static_cast<GatewayGrpcServer::CallData*>(tag)->proceed(ok);
//   }
// -------------------------------------------------------------------
class GatewayGrpcServer final : public GrpcServer {
 public:
  /// Abstract base for per-RPC state machines.
  class CallData {
   public:
    virtual ~CallData() = default;
    /// Drive the state machine (called by the CQ polling loop).
    virtual void proceed(bool ok) = 0;
  };

  /// Callback invoked on successful login. Returns true if the login
  /// should proceed, false to reject. When set, the callback runs before
  /// the default validation logic.
  using LoginAuthCallback = std::function<bool(
      const std::string& user_id,
      const std::string& token,
      std::string& out_error)>;

  /// Callback invoked on logout.
  using LogoutCallback = std::function<void(
      const std::string& user_id,
      const std::string& session_id)>;

  explicit GatewayGrpcServer(
      std::uint16_t port,
      LoginAuthCallback login_auth = nullptr,
      LogoutCallback logout_cb = nullptr);

  ~GatewayGrpcServer() override;

  // Non-copyable, non-movable.
  GatewayGrpcServer(const GatewayGrpcServer&) = delete;
  GatewayGrpcServer& operator=(const GatewayGrpcServer&) = delete;

  /// Number of currently active (authenticated) sessions tracked by this
  /// server. Used for the health check response.
  std::uint32_t active_sessions() const noexcept { return active_sessions_; }

  /// Access the completion queue for external polling.
  grpc::ServerCompletionQueue* completion_queue() noexcept { return cq_.get(); }

  /// Access the async service (for seeding initial CallData objects).
  boost::gateway::v3::Gateway::AsyncService& async_service() noexcept {
    return service_;
  }

  /// Seed the completion queue with initial CallData instances for each
  /// RPC type. Must be called after start() and before the CQ poll loop.
  void seed_completion_queue();

 private:
  // GrpcServer interface
  void register_services(grpc::ServerBuilder& builder) override;

  boost::gateway::v3::Gateway::AsyncService service_;
  std::unique_ptr<grpc::ServerCompletionQueue> cq_;

  LoginAuthCallback login_auth_;
  LogoutCallback logout_cb_;

  std::atomic<std::uint32_t> active_sessions_{0};
};

}  // namespace v2::grpc

#endif  // BOOST_BUILD_GRPC
