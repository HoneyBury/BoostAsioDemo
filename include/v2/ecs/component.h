#pragma once

#include <cstdint>
#include <type_traits>

namespace v2::ecs {

// Tag base — all components inherit from this.
// Components are POD or trivially-movable value types stored contiguously.
struct Component {
    virtual ~Component() = default;
};

// Each component type gets a unique runtime ID for sparse-set lookup.
using ComponentTypeId = std::uint32_t;

[[nodiscard]] inline ComponentTypeId next_component_type_id() noexcept {
    static ComponentTypeId next = 1;
    return next++;
}

// Returns a globally unique ID per T, assigned on first call.
template <typename T>
[[nodiscard]] ComponentTypeId component_type_id() noexcept {
    static_assert(std::is_base_of_v<Component, T>,
                  "T must derive from v2::ecs::Component");
    static const ComponentTypeId id = next_component_type_id();
    return id;
}

}  // namespace v2::ecs
