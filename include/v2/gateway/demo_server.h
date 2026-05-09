#pragma once

#include "net/session.h"
#include "v2/io/io_engine.h"
#include "v2/gateway/battle_archive_store.h"
#include "v2/gateway/runtime.h"
#include "v2/gateway/session_adapter.h"

#include <cstdint>
#include <mutex>
#include <memory>
#include <unordered_map>

namespace v2::gateway {

class DemoServer final : public DownstreamSessionWriteSink {
public:
    DemoServer(std::uint16_t port,
               net::SessionOptions session_options = {},
               std::unique_ptr<v2::io::IoEngine> io_engine = std::make_unique<v2::io::AsioIoEngine>(1));

    void start();
    void stop();
    void deliver(SessionWrite write) override;
    [[nodiscard]] std::uint16_t local_port() const;
    [[nodiscard]] std::uint32_t io_core_count() const;

private:
    void do_accept();

    std::uint16_t port_ = 0;
    net::SessionOptions session_options_;
    std::unique_ptr<v2::io::IoEngine> io_engine_;
    std::unique_ptr<v2::io::IoAcceptor> acceptor_;
    v2::runtime::ActorSystem actor_system_;
    SessionAdapter adapter_;
    Runtime runtime_;
    std::unique_ptr<JsonFileBattleArchiveStore> archive_store_;
    v2::actor::ActorRef gateway_actor_;
    std::unordered_map<SessionId, std::unique_ptr<v2::io::IoSession>> sessions_;
    mutable std::mutex sessions_mutex_;
    SessionId next_session_id_ = 1;
};

}  // namespace v2::gateway
