#pragma once

#include "net/session.h"
#include "v2/gateway/battle_archive_store.h"
#include "v2/gateway/runtime.h"
#include "v2/gateway/session_adapter.h"

#include <boost/asio.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace v2::gateway {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class DemoServer final : public DownstreamSessionWriteSink {
public:
    DemoServer(asio::io_context& io_context,
               std::uint16_t port,
               net::SessionOptions session_options = {});

    void start();
    void stop();
    void deliver(SessionWrite write) override;
    [[nodiscard]] std::uint16_t local_port() const;

private:
    void do_accept();

    tcp::acceptor acceptor_;
    net::SessionOptions session_options_;
    v2::runtime::ActorSystem actor_system_;
    SessionAdapter adapter_;
    Runtime runtime_;
    std::unique_ptr<JsonFileBattleArchiveStore> archive_store_;
    v2::actor::ActorRef gateway_actor_;
    std::unordered_map<SessionId, std::shared_ptr<net::Session>> sessions_;
    SessionId next_session_id_ = 1;
};

}  // namespace v2::gateway
