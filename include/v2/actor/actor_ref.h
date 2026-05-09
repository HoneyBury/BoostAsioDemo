#pragma once

#include <chrono>
#include <cstdint>

#include "v2/actor/message.h"

namespace v2::actor {

}  // namespace v2::actor

namespace v2::runtime {

class ActorSystem;

}  // namespace v2::runtime

namespace v2::actor {

using ScheduleId = std::uint64_t;

class ActorRef {
public:
    ActorRef() = default;

    ActorId actor_id() const noexcept { return actor_id_; }
    bool is_valid() const noexcept { return system_ != nullptr && actor_id_ != 0; }

    void tell(Message message) const;
    void tell_after(Message message, std::size_t dispatch_delay) const;
    void tell_after(Message message, std::chrono::steady_clock::duration delay) const;
    [[nodiscard]] ScheduleId schedule_after(Message message, std::chrono::steady_clock::duration delay) const;
    [[nodiscard]] ScheduleId schedule_every(Message message,
                                           std::chrono::steady_clock::duration initial_delay,
                                           std::chrono::steady_clock::duration interval) const;
    bool cancel_schedule(ScheduleId schedule_id) const;

private:
    friend class v2::runtime::ActorSystem;

    ActorRef(v2::runtime::ActorSystem* system, ActorId actor_id) noexcept
        : system_(system), actor_id_(actor_id) {}

    v2::runtime::ActorSystem* system_ = nullptr;
    ActorId actor_id_ = 0;
};

}  // namespace v2::actor
