#include <gtest/gtest.h>

#include "v2/ecs/world.h"

namespace {

struct PositionComponent final : v2::ecs::Component {
    int x = 0;
};

class AdvancePositionSystem final : public v2::ecs::System {
public:
    void run(v2::ecs::World& world, const v2::ecs::FrameContext& ctx) override {
        auto* simple_world = dynamic_cast<v2::ecs::SimpleWorld*>(&world);
        ASSERT_NE(simple_world, nullptr);
        simple_world->for_each<PositionComponent>(
            [&](v2::ecs::EntityHandle, PositionComponent& position) {
                position.x += static_cast<int>(ctx.frame_number);
            });
    }
};

struct HealthComponent final : v2::ecs::Component {
    int hp = 100;
};

}  // namespace

TEST(V2EcsWorldTest, CreatesEntitiesAndManagesComponents) {
    v2::ecs::SimpleWorld world;

    const auto entity = world.create_entity();
    ASSERT_TRUE(world.exists(entity));

    auto& position = world.add_component<PositionComponent>(entity);
    position.x = 7;

    auto* stored = world.get_component<PositionComponent>(entity);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->x, 7);

    EXPECT_TRUE(world.remove_component<PositionComponent>(entity));
    EXPECT_EQ(world.get_component<PositionComponent>(entity), nullptr);

    world.destroy_entity(entity);
    EXPECT_FALSE(world.exists(entity));
}

TEST(V2EcsWorldTest, TicksRegisteredSystems) {
    v2::ecs::SimpleWorld world;
    world.add_system(std::make_unique<AdvancePositionSystem>());

    const auto entity = world.create_entity();
    world.add_component<PositionComponent>(entity).x = 1;

    world.tick(v2::ecs::FrameContext{
        .battle_id = "battle_0001",
        .room_id = "room_alpha",
        .frame_number = 3,
        .trigger = "test",
    });

    auto* stored = world.get_component<PositionComponent>(entity);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->x, 4);
}

TEST(V2EcsWorldTest, DestroyEntityRemovesComponents) {
    v2::ecs::SimpleWorld world;
    const auto entity = world.create_entity();
    world.add_component<PositionComponent>(entity);
    world.destroy_entity(entity);
    EXPECT_EQ(world.get_component<PositionComponent>(entity), nullptr);
}

TEST(V2EcsWorldTest, EntityExistsReturnsFalseAfterDestroy) {
    v2::ecs::SimpleWorld world;
    const auto entity = world.create_entity();
    EXPECT_TRUE(world.exists(entity));
    world.destroy_entity(entity);
    EXPECT_FALSE(world.exists(entity));
}

TEST(V2EcsWorldTest, MultipleEntitiesHaveIndependentComponents) {
    v2::ecs::SimpleWorld world;
    const auto e1 = world.create_entity();
    const auto e2 = world.create_entity();

    world.add_component<PositionComponent>(e1).x = 10;
    world.add_component<PositionComponent>(e2).x = 20;
    world.add_component<HealthComponent>(e2).hp = 50;

    EXPECT_EQ(world.get_component<PositionComponent>(e1)->x, 10);
    EXPECT_EQ(world.get_component<PositionComponent>(e2)->x, 20);
    EXPECT_EQ(world.get_component<HealthComponent>(e2)->hp, 50);
    EXPECT_EQ(world.get_component<HealthComponent>(e1), nullptr);
}

TEST(V2EcsWorldTest, RemoveComponentReturnsNullOnGet) {
    v2::ecs::SimpleWorld world;
    const auto entity = world.create_entity();
    world.add_component<PositionComponent>(entity);
    EXPECT_TRUE(world.remove_component<PositionComponent>(entity));
    EXPECT_EQ(world.get_component<PositionComponent>(entity), nullptr);
}

TEST(V2EcsWorldTest, AddComponentOverwritesExisting) {
    v2::ecs::SimpleWorld world;
    const auto entity = world.create_entity();
    world.add_component<PositionComponent>(entity).x = 5;
    world.add_component<PositionComponent>(entity).x = 15;
    const auto* comp = world.get_component<PositionComponent>(entity);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->x, 15);
}

TEST(V2EcsWorldTest, WorldWithNoSystemsRunsWithoutCrash) {
    v2::ecs::SimpleWorld world;
    const auto entity = world.create_entity();
    world.add_component<PositionComponent>(entity);
    EXPECT_NO_THROW(world.tick(v2::ecs::FrameContext{
        .battle_id = "test",
        .room_id = "test",
        .frame_number = 1,
        .trigger = "test",
    }));
}

TEST(V2EcsWorldTest, DestroyInvalidEntityIsNoOp) {
    v2::ecs::SimpleWorld world;
    const v2::ecs::EntityHandle invalid{};
    EXPECT_NO_THROW(world.destroy_entity(invalid));
    EXPECT_FALSE(world.exists(invalid));
}
