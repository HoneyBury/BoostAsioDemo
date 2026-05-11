#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace v2::aoi {

using EntityId = std::uint32_t;

class SpatialGrid {
public:
    explicit SpatialGrid(std::int32_t world_width,
                         std::int32_t world_height,
                         std::int32_t cell_size);

    void add(EntityId entity, std::int32_t x, std::int32_t y);
    void remove(EntityId entity);
    void move(EntityId entity, std::int32_t new_x, std::int32_t new_y);

    [[nodiscard]] std::vector<EntityId> query(std::int32_t x,
                                               std::int32_t y,
                                               std::int32_t radius) const;

    void clear();
    [[nodiscard]] std::size_t size() const noexcept;

private:
    [[nodiscard]] std::size_t cell_index(std::int32_t cell_x, std::int32_t cell_y) const noexcept;
    [[nodiscard]] std::int32_t cell_coord_x(std::int32_t x) const noexcept;
    [[nodiscard]] std::int32_t cell_coord_y(std::int32_t y) const noexcept;

    std::int32_t world_width_;
    std::int32_t world_height_;
    std::int32_t cell_size_;
    std::int32_t grid_width_cells_;
    std::int32_t grid_height_cells_;

    // cells_ indexed by cell_y * grid_width_cells_ + cell_x
    std::vector<std::vector<EntityId>> cells_;

    // entity_id -> (cell_x, cell_y)
    std::unordered_map<EntityId, std::pair<std::int32_t, std::int32_t>> entity_positions_;
};

}  // namespace v2::aoi
