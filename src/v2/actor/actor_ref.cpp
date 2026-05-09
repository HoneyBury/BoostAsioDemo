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

ScheduleId ActorRef::schedule_after(Message message, std::chrono::steady_clock::duration delay) const {
    if (!is_valid()) {
        return 0;
    }
    if (message.header.target_actor == 0) {
        message.header.target_actor = actor_id_;
    }
    return system_->schedule_after(std::move(message), delay);
}

ScheduleId ActorRef::schedule_after(Message message, std::chrono::steady_clock::time_point ready_at) const {
    if (!is_valid()) {
        return 0;
    }
    if (message.header.target_actor == 0) {
        message.header.target_actor = actor_id_;
    }
    return system_->schedule_after(std::move(message), ready_at);
}

ScheduleId ActorRef::schedule_every(Message message,
                                    std::chrono::steady_clock::duration initial_delay,
                                    std::chrono::steady_clock::duration interval) const {
    if (!is_valid()) {
        return 0;
    }
    if (message.header.target_actor == 0) {
        message.header.target_actor = actor_id_;
    }
    return system_->schedule_every(std::move(message), initial_delay, interval);
}

ScheduleId ActorRef::schedule_every(Message message,
                                    std::chrono::steady_clock::duration initial_delay,
                                    std::chrono::steady_clock::duration interval,
                                    std::size_t max_repetitions) const {
    if (!is_valid()) {
        return 0;
    }
    if (message.header.target_actor == 0) {
        message.header.target_actor = actor_id_;
    }
    return system_->schedule_every(std::move(message), initial_delay, interval, max_repetitions);
}

bool ActorRef::cancel_schedule(ScheduleId schedule_id) const {
    if (!is_valid()) {
        return false;
    }
    return system_->cancel_schedule(schedule_id);
}

}  // namespace v2::actor
