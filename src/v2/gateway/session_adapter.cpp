#include "v2/gateway/session_adapter.h"

#include <utility>

namespace v2::gateway {

void SessionAdapter::bind_gateway(v2::actor::ActorRef gateway_actor) noexcept {
    gateway_actor_ = gateway_actor;
}

std::vector<SessionWrite> SessionAdapter::handle_incoming(ClientEnvelope envelope) {
    {
        std::scoped_lock lock(outbox_mutex_);
        outbox_.clear();
    }
    if (!gateway_actor_.is_valid()) {
        return {};
    }

    v2::actor::Message message;
    message.header.kind = v2::actor::MessageKind::kUser;
    message.payload = std::move(envelope);
    gateway_actor_.tell(std::move(message));
    actor_system_.dispatch_all();
    std::scoped_lock lock(outbox_mutex_);
    return outbox_;
}

void SessionAdapter::push(SessionWrite write) {
    SessionWrite delivered = write;
    {
        std::scoped_lock lock(outbox_mutex_);
        outbox_.push_back(std::move(write));
    }
    if (downstream_ != nullptr) {
        downstream_->deliver(std::move(delivered));
    }
}

}  // namespace v2::gateway
