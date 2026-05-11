#include "v2/aoi/aoi_system.h"

#include "v2/battle/runtime_components.h"
#include "v2/ecs/world.h"

namespace v2::aoi {

AoiSystem::AoiSystem(std::int32_t world_width,
                     std::int32_t world_height,
                     std::int32_t cell_size,
                     std::int32_t default_view_radius)
    : world_width_(world_width)
    , world_height_(world_height)
    , cell_size_(cell_size)
    , default_view_radius_(default_view_radius)
    , grid_(world_width, world_height, cell_size) {
}

void AoiSystem::run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) {
    (void)ctx;
    auto* simple_world = dynamic_cast<v2::ecs::SimpleWorld*>(&world);
    if (simple_world == nullptr) return;

    grid_.clear();

    // Populate grid with all entities that have positions
    simple_world->for_each<v2::battle::PositionComponent>(
        [&](v2::ecs::EntityHandle handle, v2::battle::PositionComponent& pos) {
            grid_.add(handle.id, pos.x, pos.y);
        });

    // Query visible neighbors for each online participant
    simple_world->for_each<v2::battle::BattleParticipantComponent>(
        [&](v2::ecs::EntityHandle handle, v2::battle::BattleParticipantComponent& participant) {
            if (!participant.online) return;

            auto* pos = simple_world->get_component<v2::battle::PositionComponent>(handle);
            if (pos == nullptr) return;

            auto visible = grid_.query(pos->x, pos->y, default_view_radius_);

            auto* view = simple_world->get_component<AoiViewComponent>(handle);
            if (view == nullptr) {
                view = &simple_world->add_component<AoiViewComponent>(handle);
            }

            view->visible_entities = std::move(visible);
            view->visible_count = static_cast<std::uint32_t>(view->visible_entities.size());
        });
}

}  // namespace v2::aoi
