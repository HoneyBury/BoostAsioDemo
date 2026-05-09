#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "v2/ecs/entity.h"
#include "v2/ecs/component.h"

namespace v2::ecs {

using FrameNumber = std::uint32_t;

struct FrameContext {
    std::string battle_id;
    std::string room_id;
    FrameNumber frame_number = 0;
    std::chrono::milliseconds tick_interval{33};
};

class World {
public:
    World() = default;
    virtual ~World() = default;

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    virtual EntityHandle create_entity() = 0;
    virtual void destroy_entity(EntityHandle entity) = 0;
    [[nodiscard]] virtual bool exists(EntityHandle entity) const = 0;

    template <typename T, typename... Args>
    T& add_component(EntityHandle entity, Args&&... args);

    template <typename T>
    [[nodiscard]] T* get_component(EntityHandle entity);

    template <typename T>
    bool remove_component(EntityHandle entity);

    virtual void tick(const FrameContext& ctx) = 0;
};

}  // namespace v2::ecs
