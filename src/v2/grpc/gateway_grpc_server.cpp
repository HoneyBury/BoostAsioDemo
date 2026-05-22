// gateway_grpc_server.cpp — Gateway gRPC service implementation.
//
// Uses the async gRPC API with a single completion queue. Each incoming RPC
// spawns a CallData object that manages the per-call state machine:
//
//   CREATE -> PROCESS -> FINISH -> (reclaim or re-register)

#ifdef BOOST_BUILD_GRPC

#include "v2/grpc/gateway_grpc_server.h"

#include <spdlog/spdlog.h>

#include "net/protocol.h"
#include "v2/gateway/gateway_command_parser.h"

namespace v2::grpc {

// ===================================================================
// RPC tag types for distinguishing completion queue events.
// ===================================================================
namespace {

enum class RpcTagType : std::uintptr_t {
  kLoginRequest = 0x1000,
  kLogoutRequest = 0x2000,
  kHealthRequest = 0x3000,
};

}  // namespace

// ===================================================================
// LoginCallData — handles RequestLogin RPCs
// ===================================================================
class LoginCallData final : public GatewayGrpcServer::CallData {
 public:
  LoginCallData(GatewayGrpcServer* server,
                boost::gateway::v3::Gateway::AsyncService* service,
                grpc::ServerCompletionQueue* cq)
      : server_(server), service_(service), cq_(cq), responder_(&ctx_),
        status_(CREATE) {
    service_->RequestRequestLogin(&ctx_, &request_, &responder_, cq_, cq_,
                                  reinterpret_cast<void*>(RpcTagType::kLoginRequest));
    status_ = PROCESS;
  }

  void proceed(bool /*ok*/) override {
    if (status_ == PROCESS) {
      // Build response from request fields
      response_.set_user_id(request_.user_id());
      response_.set_display_name(request_.display_name());

      std::string error_msg;
      std::int32_t error_code = 0;

      // Delegate to the login auth callback if set
      if (server_->login_auth_) {
        const bool allowed = server_->login_auth_(
            request_.user_id(), request_.token(), error_msg);
        if (!allowed) {
          error_code = static_cast<std::int32_t>(net::protocol::ErrorCode::kAuthRequired);
          response_.set_error_code(error_code);
          response_.set_error_message(error_msg);
          goto finish;
        }
      }

      // Use the existing command parser for validation
      {
        const std::string raw_body = request_.user_id() + "|" +
                                     request_.token() + "|" +
                                     request_.display_name();
        const auto parsed = v2::gateway::parse_login_command_body(raw_body);
        if (!parsed.has_value() ||
            !v2::gateway::validate_login_command_body(*parsed)) {
          error_code = static_cast<std::int32_t>(net::protocol::ErrorCode::kInvalidUserId);
          response_.set_error_code(error_code);
          response_.set_error_message(
              net::protocol::to_string(net::protocol::ErrorCode::kInvalidUserId));
          goto finish;
        }
      }

      // Success
      response_.set_error_code(0);
      response_.set_role("player");
      server_->active_sessions_.fetch_add(1, std::memory_order_relaxed);

      SPDLOG_INFO("GatewayGrpc: login ok user={}", request_.user_id());

    finish:
      responder_.Finish(response_, ErrorCodeMapper::from_error_code(error_code, error_msg),
                        reinterpret_cast<void*>(RpcTagType::kLoginRequest));
      status_ = FINISH;

    } else {
      // FINISH state: this CallData is done; spawn replacement if server lives
      if (server_->server_) {
        auto* replacement = new LoginCallData(server_, service_, cq_);
        (void)replacement;
      }
      delete this;
    }
  }

 private:
  enum CallStatus { CREATE, PROCESS, FINISH };

  GatewayGrpcServer* server_;
  boost::gateway::v3::Gateway::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;

  grpc::ServerContext ctx_;
  boost::gateway::v3::LoginRequest request_;
  boost::gateway::v3::LoginResponse response_;
  grpc::ServerAsyncResponseWriter<boost::gateway::v3::LoginResponse> responder_;

  CallStatus status_;
};

// ===================================================================
// LogoutCallData — handles RequestLogout RPCs
// ===================================================================
class LogoutCallData final : public GatewayGrpcServer::CallData {
 public:
  LogoutCallData(GatewayGrpcServer* server,
                 boost::gateway::v3::Gateway::AsyncService* service,
                 grpc::ServerCompletionQueue* cq)
      : server_(server), service_(service), cq_(cq), responder_(&ctx_),
        status_(CREATE) {
    service_->RequestRequestLogout(&ctx_, &request_, &responder_, cq_, cq_,
                                   reinterpret_cast<void*>(RpcTagType::kLogoutRequest));
    status_ = PROCESS;
  }

  void proceed(bool /*ok*/) override {
    if (status_ == PROCESS) {
      response_.set_success(true);
      response_.set_error_code(0);

      if (server_->logout_cb_) {
        server_->logout_cb_(request_.user_id(), request_.session_id());
      }

      server_->active_sessions_.fetch_sub(1, std::memory_order_relaxed);

      SPDLOG_INFO("GatewayGrpc: logout user={}", request_.user_id());

      responder_.Finish(response_, grpc::Status::OK,
                        reinterpret_cast<void*>(RpcTagType::kLogoutRequest));
      status_ = FINISH;

    } else {
      if (server_->server_) {
        auto* replacement = new LogoutCallData(server_, service_, cq_);
        (void)replacement;
      }
      delete this;
    }
  }

 private:
  enum CallStatus { CREATE, PROCESS, FINISH };

  GatewayGrpcServer* server_;
  boost::gateway::v3::Gateway::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;

  grpc::ServerContext ctx_;
  boost::gateway::v3::LogoutRequest request_;
  boost::gateway::v3::LogoutResponse response_;
  grpc::ServerAsyncResponseWriter<boost::gateway::v3::LogoutResponse> responder_;

  CallStatus status_;
};

// ===================================================================
// HealthCallData — handles Health RPCs
// ===================================================================
class HealthCallData final : public GatewayGrpcServer::CallData {
 public:
  HealthCallData(GatewayGrpcServer* server,
                 boost::gateway::v3::Gateway::AsyncService* service,
                 grpc::ServerCompletionQueue* cq)
      : server_(server), service_(service), cq_(cq), responder_(&ctx_),
        status_(CREATE) {
    service_->RequestHealth(&ctx_, &request_, &responder_, cq_, cq_,
                            reinterpret_cast<void*>(RpcTagType::kHealthRequest));
    status_ = PROCESS;
  }

  void proceed(bool /*ok*/) override {
    if (status_ == PROCESS) {
      response_.set_status("SERVING");
      response_.set_uptime_seconds(server_->uptime_seconds());
      response_.set_active_sessions(server_->active_sessions());

      responder_.Finish(response_, grpc::Status::OK,
                        reinterpret_cast<void*>(RpcTagType::kHealthRequest));
      status_ = FINISH;

    } else {
      if (server_->server_) {
        auto* replacement = new HealthCallData(server_, service_, cq_);
        (void)replacement;
      }
      delete this;
    }
  }

 private:
  enum CallStatus { CREATE, PROCESS, FINISH };

  GatewayGrpcServer* server_;
  boost::gateway::v3::Gateway::AsyncService* service_;
  grpc::ServerCompletionQueue* cq_;

  grpc::ServerContext ctx_;
  boost::gateway::v3::HealthRequest request_;
  boost::gateway::v3::HealthResponse response_;
  grpc::ServerAsyncResponseWriter<boost::gateway::v3::HealthResponse> responder_;

  CallStatus status_;
};

// ===================================================================
// GatewayGrpcServer implementation
// ===================================================================

GatewayGrpcServer::GatewayGrpcServer(
    std::uint16_t port,
    LoginAuthCallback login_auth,
    LogoutCallback logout_cb)
    : GrpcServer("GatewayGrpc", port),
      login_auth_(std::move(login_auth)),
      logout_cb_(std::move(logout_cb)) {}

GatewayGrpcServer::~GatewayGrpcServer() {
  shutdown();
}

void GatewayGrpcServer::register_services(grpc::ServerBuilder& builder) {
  builder.RegisterService(&service_);
  cq_ = builder.AddCompletionQueue();
}

void GatewayGrpcServer::seed_completion_queue() {
  if (!cq_) {
    SPDLOG_WARN("GatewayGrpc: cannot seed CQ — not started");
    return;
  }

  // Create one CallData per RPC type so the CQ has initial handlers.
  auto* login = new LoginCallData(this, &service_, cq_.get());
  auto* logout = new LogoutCallData(this, &service_, cq_.get());
  auto* health = new HealthCallData(this, &service_, cq_.get());

  // Suppress unused-variable warnings — the CallData objects register
  // themselves with the AsyncService in their constructors.
  (void)login;
  (void)logout;
  (void)health;

  SPDLOG_DEBUG("GatewayGrpc: CQ seeded with Login/Logout/Health handlers");
}

}  // namespace v2::grpc

#endif  // BOOST_BUILD_GRPC
