#include "v2/actor/actor.h"

namespace v2::actor {

void Actor::tell(const ActorRef& target, Message message) const {
    if (!target.is_valid()) {
        return;
    }
    message.header.source_actor = id();
    message.header.target_actor = target.actor_id();
    target.tell(std::move(message));
}

void Actor::tell_after(const ActorRef& target, Message message, std::size_t dispatch_delay) const {
    if (!target.is_valid()) {
        return;
    }
    message.header.source_actor = id();
    message.header.target_actor = target.actor_id();
    target.tell_after(std::move(message), dispatch_delay);
}

}  // namespace v2::actor
