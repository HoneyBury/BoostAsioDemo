#pragma once

#include "v2/aoi/spatial_grid.h"
#include "v2/ecs/component.h"
#include "v2/ecs/entity.h"
#include "v2/ecs/system.h"

#include <cstdint>
#include <vector>

namespace v2::aoi {

struct AoiViewComponent final : v2::ecs::Component {
    // Entity IDs visible to this entity this frame
    std::vector<v2::ecs::EntityId> visible_entities;
    // Number of entities currently in view
    std::uint32_t visible_count = 0;
};

class AoiSystem final : public v2::ecs::System {
public:
    explicit AoiSystem(std::int32_t world_width = 1000,
                       std::int32_t world_height = 1000,
                       std::int32_t cell_size = 100,
                       std::int32_t default_view_radius = 100);
    void run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) override;

private:
    std::int32_t world_width_;
    std::int32_t world_height_;
    std::int32_t cell_size_;
    std::int32_t default_view_radius_;
    SpatialGrid grid_;
};

}  // namespace v2::aoi
