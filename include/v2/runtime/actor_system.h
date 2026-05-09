#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <vector>
#include <unordered_map>

#include "v2/actor/actor.h"

namespace v2::runtime {

class ActorSystem {
public:
    ActorSystem() = default;
    ~ActorSystem();

    ActorSystem(const ActorSystem&) = delete;
    ActorSystem& operator=(const ActorSystem&) = delete;

    v2::actor::ActorRef create_actor(
        std::unique_ptr<v2::actor::Actor> actor,
        v2::actor::ActorRef parent = {});

    void send(v2::actor::Message message);
    void send_after(v2::actor::Message message, std::size_t dispatch_delay);
    std::size_t dispatch_all();
    void shutdown();

private:
    struct ActorCell {
        std::unique_ptr<v2::actor::Actor> actor;
        std::deque<v2::actor::Message> mailbox;
        bool started = false;
        bool queued = false;
    };

    ActorCell* find_cell(v2::actor::ActorId actor_id) noexcept;
    void enqueue_ready_actor(v2::actor::ActorId actor_id, ActorCell& cell);
    void promote_scheduled_messages();

    struct ScheduledMessage {
        std::size_t ready_after_dispatch = 0;
        v2::actor::Message message;
    };

    std::unordered_map<v2::actor::ActorId, ActorCell> actors_;
    std::deque<v2::actor::ActorId> ready_actors_;
    std::vector<ScheduledMessage> scheduled_messages_;
    std::size_t dispatch_round_ = 0;
    v2::actor::ActorId next_actor_id_ = 1;
    bool shutting_down_ = false;
};

}  // namespace v2::runtime
