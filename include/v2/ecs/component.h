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

// Returns a globally unique ID per T, assigned on first call.
template <typename T>
[[nodiscard]] ComponentTypeId component_type_id() noexcept {
    static_assert(std::is_base_of_v<Component, T>,
                  "T must derive from v2::ecs::Component");
    static const ComponentTypeId id = [] {
        static std::uint32_t next = 1;
        return next++;
    }();
    return id;
}

}  // namespace v2::ecs
