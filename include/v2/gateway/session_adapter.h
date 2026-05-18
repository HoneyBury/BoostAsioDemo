#pragma once

#include <vector>
#include <mutex>

#include "v2/actor/actor_ref.h"
#include "v2/gateway/gateway_actor.h"
#include "v2/runtime/actor_system.h"

namespace v2::gateway {

class SessionAdapter final : public SessionWriteSink {
public:
    explicit SessionAdapter(v2::runtime::ActorSystem& actor_system,
                            DownstreamSessionWriteSink* downstream = nullptr)
        : actor_system_(actor_system), downstream_(downstream) {}

    void bind_gateway(v2::actor::ActorRef gateway_actor) noexcept;
    [[nodiscard]] std::vector<SessionWrite> handle_incoming(ClientEnvelope envelope);

    void push(SessionWrite write) override;

private:
    v2::runtime::ActorSystem& actor_system_;
    v2::actor::ActorRef gateway_actor_;
    std::vector<SessionWrite> outbox_;
    std::mutex outbox_mutex_;
    DownstreamSessionWriteSink* downstream_ = nullptr;
};

}  // namespace v2::gateway
