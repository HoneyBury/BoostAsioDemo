#pragma once

#include <cstdint>

namespace v2::ecs {

using EntityId = std::uint32_t;

struct EntityHandle {
    EntityId id = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] bool valid() const noexcept { return id != 0; }
};

}  // namespace v2::ecs
