#pragma once

#include <chrono>
#include <cstddef>
#include <vector>
#include <utility>

#include "v2/actor/actor_ref.h"

namespace v2::actor {

}  // namespace v2::actor

namespace v2::runtime {

class ActorSystem;
class ScheduleHandle;

}  // namespace v2::runtime

namespace v2::actor {

class Actor {
public:
    virtual ~Actor() = default;

    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_message(Message&& message) = 0;

    ActorId id() const noexcept { return self_.actor_id(); }
    ActorRef self() const noexcept { return self_; }
    ActorRef parent() const noexcept { return parent_; }

protected:
    void tell(const ActorRef& target, Message message) const;
    void tell_after(const ActorRef& target, Message message, std::size_t dispatch_delay) const;
    void tell_after(const ActorRef& target, Message message, std::chrono::steady_clock::duration delay) const;
    [[nodiscard]] ScheduleId schedule_after(const ActorRef& target,
                                            Message message,
                                            std::chrono::steady_clock::duration delay);
    [[nodiscard]] ScheduleId schedule_after(const ActorRef& target,
                                            Message message,
                                            std::chrono::steady_clock::time_point ready_at);
    [[nodiscard]] ScheduleId schedule_every(const ActorRef& target,
                                            Message message,
                                            std::chrono::steady_clock::duration initial_delay,
                                            std::chrono::steady_clock::duration interval);
    [[nodiscard]] ScheduleId schedule_every(const ActorRef& target,
                                            Message message,
                                            std::chrono::steady_clock::duration initial_delay,
                                            std::chrono::steady_clock::duration interval,
                                            std::size_t max_repetitions);
    [[nodiscard]] v2::runtime::ScheduleHandle schedule_owned(const ActorRef& target,
                                                             Message message,
                                                             std::chrono::steady_clock::duration interval);
    [[nodiscard]] v2::runtime::ScheduleHandle schedule_owned(const ActorRef& target,
                                                             Message message,
                                                             std::chrono::steady_clock::duration initial_delay,
                                                             std::chrono::steady_clock::duration interval);
    bool cancel_schedule(ScheduleId schedule_id);

private:
    friend class v2::runtime::ActorSystem;

    void bind(ActorRef self_ref, ActorRef parent_ref) noexcept {
        self_ = self_ref;
        parent_ = parent_ref;
    }
    void track_schedule(ScheduleId schedule_id);
    void cancel_all_owned_schedules();

    ActorRef self_;
    ActorRef parent_;
    std::vector<ScheduleId> owned_schedules_;
};

}  // namespace v2::actor
