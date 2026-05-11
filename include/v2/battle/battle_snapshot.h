#pragma once

#include <string>
#include <string_view>

namespace v2::ecs {
class World;
}  // namespace v2::ecs

namespace v2::battle {

// Serialize the full ECS world state to a JSON snapshot.
[[nodiscard]] std::string battle_world_snapshot_to_json(v2::ecs::World& world);

// Restore ECS world state from a previously taken JSON snapshot.
// The world must already have been initialized via create_battle_world().
// Returns false if the snapshot is malformed or incompatible.
[[nodiscard]] bool battle_world_restore_from_json(v2::ecs::World& world,
                                                   std::string_view json);

}  // namespace v2::battle
