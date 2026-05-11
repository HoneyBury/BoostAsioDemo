#include "v2/aoi/spatial_grid.h"

#include <algorithm>

namespace v2::aoi {

SpatialGrid::SpatialGrid(std::int32_t world_width,
                         std::int32_t world_height,
                         std::int32_t cell_size)
    : world_width_(world_width)
    , world_height_(world_height)
    , cell_size_(cell_size)
    , grid_width_cells_(std::max(1, (world_width + cell_size - 1) / cell_size))
    , grid_height_cells_(std::max(1, (world_height + cell_size - 1) / cell_size))
    , cells_(static_cast<std::size_t>(grid_width_cells_) * grid_height_cells_)
{
}

std::int32_t SpatialGrid::cell_coord_x(std::int32_t x) const noexcept {
    auto cx = x / cell_size_;
    return std::clamp(cx, static_cast<std::int32_t>(0), grid_width_cells_ - 1);
}

std::int32_t SpatialGrid::cell_coord_y(std::int32_t y) const noexcept {
    auto cy = y / cell_size_;
    return std::clamp(cy, static_cast<std::int32_t>(0), grid_height_cells_ - 1);
}

std::size_t SpatialGrid::cell_index(std::int32_t cell_x, std::int32_t cell_y) const noexcept {
    return static_cast<std::size_t>(cell_y) * grid_width_cells_ + cell_x;
}

void SpatialGrid::add(EntityId entity, std::int32_t x, std::int32_t y) {
    if (entity_positions_.count(entity)) return;

    auto cx = cell_coord_x(x);
    auto cy = cell_coord_y(y);
    auto idx = cell_index(cx, cy);

    cells_[idx].push_back(entity);
    entity_positions_[entity] = {cx, cy};
}

void SpatialGrid::remove(EntityId entity) {
    auto it = entity_positions_.find(entity);
    if (it == entity_positions_.end()) return;

    auto [cx, cy] = it->second;
    auto idx = cell_index(cx, cy);

    auto& cell = cells_[idx];
    auto vec_it = std::find(cell.begin(), cell.end(), entity);
    if (vec_it != cell.end()) {
        *vec_it = cell.back();
        cell.pop_back();
    }

    entity_positions_.erase(it);
}

void SpatialGrid::move(EntityId entity, std::int32_t new_x, std::int32_t new_y) {
    auto it = entity_positions_.find(entity);
    if (it == entity_positions_.end()) return;

    auto new_cx = cell_coord_x(new_x);
    auto new_cy = cell_coord_y(new_y);
    auto [old_cx, old_cy] = it->second;

    if (old_cx == new_cx && old_cy == new_cy) {
        it->second = {new_cx, new_cy};
        return;
    }

    // Remove from old cell
    auto old_idx = cell_index(old_cx, old_cy);
    auto& old_cell = cells_[old_idx];
    auto vec_it = std::find(old_cell.begin(), old_cell.end(), entity);
    if (vec_it != old_cell.end()) {
        *vec_it = old_cell.back();
        old_cell.pop_back();
    }

    // Add to new cell
    auto new_idx = cell_index(new_cx, new_cy);
    cells_[new_idx].push_back(entity);

    it->second = {new_cx, new_cy};
}

std::vector<EntityId> SpatialGrid::query(std::int32_t x,
                                          std::int32_t y,
                                          std::int32_t radius) const {
    std::vector<EntityId> result;

    if (radius < 0) return result;

    auto min_cx = cell_coord_x(x - radius);
    auto max_cx = cell_coord_x(x + radius);
    auto min_cy = cell_coord_y(y - radius);
    auto max_cy = cell_coord_y(y + radius);

    for (auto cy = min_cy; cy <= max_cy; ++cy) {
        for (auto cx = min_cx; cx <= max_cx; ++cx) {
            auto idx = cell_index(cx, cy);
            const auto& cell = cells_[idx];
            result.insert(result.end(), cell.begin(), cell.end());
        }
    }

    return result;
}

void SpatialGrid::clear() {
    for (auto& cell : cells_) {
        cell.clear();
    }
    entity_positions_.clear();
}

std::size_t SpatialGrid::size() const noexcept {
    return entity_positions_.size();
}

}  // namespace v2::aoi
