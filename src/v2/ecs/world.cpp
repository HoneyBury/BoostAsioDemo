#include "v2/ecs/world.h"

#include <utility>

namespace v2::ecs {

EntityHandle SimpleWorld::create_entity() {
    const auto entity_id = next_entity_id_++;
    auto& generation = generations_[entity_id];
    if (generation == 0) {
        generation = 1;
    }
    return EntityHandle{
        .id = entity_id,
        .generation = generation,
    };
}

void SimpleWorld::destroy_entity(EntityHandle entity) {
    if (!exists(entity)) {
        return;
    }
    for (auto& [type_id, store] : component_stores_) {
        (void)type_id;
        store.components.erase(entity.id);
    }
    ++generations_[entity.id];
}

bool SimpleWorld::exists(EntityHandle entity) const {
    if (!entity.valid()) {
        return false;
    }
    const auto it = generations_.find(entity.id);
    return it != generations_.end() && it->second == entity.generation;
}

void SimpleWorld::tick(const FrameContext& ctx) {
    for (auto& system : systems_) {
        system->run(*this, ctx);
    }
}

void SimpleWorld::add_system(std::unique_ptr<System> system) {
    if (!system) {
        return;
    }
    systems_.push_back(std::move(system));
}

Component* SimpleWorld::add_component_erased(EntityHandle entity,
                                             ComponentTypeId type_id,
                                             std::unique_ptr<Component> component) {
    if (!exists(entity) || component == nullptr) {
        return nullptr;
    }
    auto& slot = component_stores_[type_id].components[entity.id];
    slot = std::move(component);
    return slot.get();
}

Component* SimpleWorld::get_component_erased(EntityHandle entity,
                                             ComponentTypeId type_id) {
    if (!exists(entity)) {
        return nullptr;
    }
    auto* store = find_store(type_id);
    if (store == nullptr) {
        return nullptr;
    }
    auto it = store->components.find(entity.id);
    return it == store->components.end() ? nullptr : it->second.get();
}

bool SimpleWorld::remove_component_erased(EntityHandle entity, ComponentTypeId type_id) {
    if (!exists(entity)) {
        return false;
    }
    auto* store = find_store(type_id);
    if (store == nullptr) {
        return false;
    }
    return store->components.erase(entity.id) > 0;
}

SimpleWorld::ComponentStore* SimpleWorld::find_store(ComponentTypeId type_id) {
    auto it = component_stores_.find(type_id);
    return it == component_stores_.end() ? nullptr : &it->second;
}

const SimpleWorld::ComponentStore* SimpleWorld::find_store(ComponentTypeId type_id) const {
    auto it = component_stores_.find(type_id);
    return it == component_stores_.end() ? nullptr : &it->second;
}

}  // namespace v2::ecs
