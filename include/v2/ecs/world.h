#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "v2/ecs/entity.h"
#include "v2/ecs/component.h"
#include "v2/ecs/system.h"

namespace v2::ecs {

using FrameNumber = std::uint32_t;

struct FrameContext {
    std::string battle_id;
    std::string room_id;
    FrameNumber frame_number = 0;
    std::string trigger;
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
    T& add_component(EntityHandle entity, Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>,
                      "T must derive from v2::ecs::Component");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        auto* stored = add_component_erased(entity, component_type_id<T>(), std::move(component));
        return static_cast<T&>(*stored);
    }

    template <typename T>
    [[nodiscard]] T* get_component(EntityHandle entity) {
        static_assert(std::is_base_of_v<Component, T>,
                      "T must derive from v2::ecs::Component");
        return static_cast<T*>(get_component_erased(entity, component_type_id<T>()));
    }

    template <typename T>
    bool remove_component(EntityHandle entity) {
        static_assert(std::is_base_of_v<Component, T>,
                      "T must derive from v2::ecs::Component");
        return remove_component_erased(entity, component_type_id<T>());
    }

    virtual void tick(const FrameContext& ctx) = 0;

protected:
    virtual Component* add_component_erased(EntityHandle entity,
                                            ComponentTypeId type_id,
                                            std::unique_ptr<Component> component) = 0;
    [[nodiscard]] virtual Component* get_component_erased(EntityHandle entity,
                                                          ComponentTypeId type_id) = 0;
    virtual bool remove_component_erased(EntityHandle entity, ComponentTypeId type_id) = 0;
};

class SimpleWorld final : public World {
public:
    SimpleWorld() = default;
    ~SimpleWorld() override = default;

    EntityHandle create_entity() override;
    void destroy_entity(EntityHandle entity) override;
    [[nodiscard]] bool exists(EntityHandle entity) const override;
    void tick(const FrameContext& ctx) override;

    void add_system(std::unique_ptr<System> system);

    template <typename T, typename Fn>
    void for_each(Fn&& fn) {
        static_assert(std::is_base_of_v<Component, T>,
                      "T must derive from v2::ecs::Component");
        auto* store = find_store(component_type_id<T>());
        if (store == nullptr) {
            return;
        }
        for (auto& [entity_id, component] : store->components) {
            const EntityHandle handle{
                .id = entity_id,
                .generation = generations_[entity_id],
            };
            fn(handle, static_cast<T&>(*component));
        }
    }

private:
    struct ComponentStore {
        std::unordered_map<EntityId, std::unique_ptr<Component>> components;
    };

    Component* add_component_erased(EntityHandle entity,
                                    ComponentTypeId type_id,
                                    std::unique_ptr<Component> component) override;
    [[nodiscard]] Component* get_component_erased(EntityHandle entity,
                                                  ComponentTypeId type_id) override;
    bool remove_component_erased(EntityHandle entity, ComponentTypeId type_id) override;

    [[nodiscard]] ComponentStore* find_store(ComponentTypeId type_id);
    [[nodiscard]] const ComponentStore* find_store(ComponentTypeId type_id) const;

    std::unordered_map<ComponentTypeId, ComponentStore> component_stores_;
    std::unordered_map<EntityId, std::uint32_t> generations_;
    std::vector<std::unique_ptr<System>> systems_;
    EntityId next_entity_id_ = 1;
};

}  // namespace v2::ecs
