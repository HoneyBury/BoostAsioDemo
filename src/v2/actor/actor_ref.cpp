#include "v2/actor/actor_ref.h"

#include "v2/runtime/actor_system.h"

namespace v2::actor {

void ActorRef::tell(Message message) const {
    if (!is_valid()) {
        return;
    }
    if (message.header.target_actor == 0) {
        message.header.target_actor = actor_id_;
    }
    system_->send(std::move(message));
}

void ActorRef::tell_after(Message message, std::size_t dispatch_delay) const {
    if (!is_valid()) {
        return;
    }
    if (message.header.target_actor == 0) {
        message.header.target_actor = actor_id_;
    }
    system_->send_after(std::move(message), dispatch_delay);
}

void ActorRef::tell_after(Message message, std::chrono::steady_clock::duration delay) const {
    if (!is_valid()) {
        return;
    }
    if (message.header.target_actor == 0) {
        message.header.target_actor = actor_id_;
    }
    system_->send_after(std::move(message), delay);
}

}  // namespace v2::actor
