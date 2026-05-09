#include "v2/actor/actor.h"

#include <algorithm>

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

void Actor::tell_after(const ActorRef& target,
                       Message message,
                       std::chrono::steady_clock::duration delay) const {
    if (!target.is_valid()) {
        return;
    }
    message.header.source_actor = id();
    message.header.target_actor = target.actor_id();
    target.tell_after(std::move(message), delay);
}

ScheduleId Actor::schedule_after(const ActorRef& target,
                                 Message message,
                                 std::chrono::steady_clock::duration delay) {
    if (!target.is_valid()) {
        return 0;
    }
    message.header.source_actor = id();
    message.header.target_actor = target.actor_id();
    const auto schedule_id = target.schedule_after(std::move(message), delay);
    track_schedule(schedule_id);
    return schedule_id;
}

ScheduleId Actor::schedule_every(const ActorRef& target,
                                 Message message,
                                 std::chrono::steady_clock::duration initial_delay,
                                 std::chrono::steady_clock::duration interval) {
    if (!target.is_valid()) {
        return 0;
    }
    message.header.source_actor = id();
    message.header.target_actor = target.actor_id();
    const auto schedule_id = target.schedule_every(std::move(message), initial_delay, interval);
    track_schedule(schedule_id);
    return schedule_id;
}

bool Actor::cancel_schedule(ScheduleId schedule_id) {
    if (schedule_id == 0 || !self_.is_valid()) {
        return false;
    }
    const auto cancelled = self_.cancel_schedule(schedule_id);
    if (cancelled) {
        owned_schedules_.erase(
            std::remove(owned_schedules_.begin(), owned_schedules_.end(), schedule_id),
            owned_schedules_.end());
    }
    return cancelled;
}

void Actor::track_schedule(ScheduleId schedule_id) {
    if (schedule_id != 0) {
        owned_schedules_.push_back(schedule_id);
    }
}

void Actor::cancel_all_owned_schedules() {
    if (!self_.is_valid()) {
        owned_schedules_.clear();
        return;
    }
    for (const auto schedule_id : owned_schedules_) {
        (void)self_.cancel_schedule(schedule_id);
    }
    owned_schedules_.clear();
}

}  // namespace v2::actor
