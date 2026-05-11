#include <gtest/gtest.h>

#include <algorithm>

#include "v2/aoi/spatial_grid.h"

using v2::aoi::EntityId;
using v2::aoi::SpatialGrid;

// ─── Basic operations ─────────────────────────────────────────────

TEST(V2AoiSpatialGridTest, AddAndSize) {
    SpatialGrid grid(1000, 1000, 100);
    EXPECT_EQ(grid.size(), 0U);

    grid.add(1, 50, 50);
    EXPECT_EQ(grid.size(), 1U);

    grid.add(2, 150, 250);
    EXPECT_EQ(grid.size(), 2U);
}

TEST(V2AoiSpatialGridTest, AddDuplicateIsNoOp) {
    SpatialGrid grid(1000, 1000, 100);
    grid.add(1, 100, 200);
    EXPECT_EQ(grid.size(), 1U);
    grid.add(1, 999, 999);  // different position, same entity — ignored
    EXPECT_EQ(grid.size(), 1U);
}

TEST(V2AoiSpatialGridTest, RemoveExisting) {
    SpatialGrid grid(1000, 1000, 100);
    grid.add(1, 10, 20);
    grid.add(2, 30, 40);
    EXPECT_EQ(grid.size(), 2U);

    grid.remove(1);
    EXPECT_EQ(grid.size(), 1U);

    grid.remove(2);
    EXPECT_EQ(grid.size(), 0U);
}

TEST(V2AoiSpatialGridTest, RemoveMissingIsNoOp) {
    SpatialGrid grid(1000, 1000, 100);
    grid.remove(999);  // not present
    EXPECT_EQ(grid.size(), 0U);
}

TEST(V2AoiSpatialGridTest, ClearEmptiesAll) {
    SpatialGrid grid(1000, 1000, 100);
    for (int i = 1; i <= 50; ++i) {
        grid.add(i, (i * 10) % 1000, (i * 20) % 1000);
    }
    EXPECT_EQ(grid.size(), 50U);
    grid.clear();
    EXPECT_EQ(grid.size(), 0U);
}

// ─── Query ────────────────────────────────────────────────────────

TEST(V2AoiSpatialGridTest, QueryReturnsEntitiesInRadius) {
    SpatialGrid grid(1000, 1000, 100);
    grid.add(1, 100, 100);   // center
    grid.add(2, 120, 100);   // 20 units away → visible
    grid.add(3, 100, 120);   // 20 units away → visible
    grid.add(4, 300, 300);   // 200 units away → not visible with radius=50

    auto result = grid.query(100, 100, 50);
    // Should contain 1, 2, 3 but not 4
    ASSERT_EQ(result.size(), 3U);
    EXPECT_NE(std::find(result.begin(), result.end(), 1U), result.end());
    EXPECT_NE(std::find(result.begin(), result.end(), 2U), result.end());
    EXPECT_NE(std::find(result.begin(), result.end(), 3U), result.end());
}

TEST(V2AoiSpatialGridTest, QueryNegativeRadiusReturnsEmpty) {
    SpatialGrid grid(1000, 1000, 100);
    grid.add(1, 50, 50);
    auto result = grid.query(50, 50, -1);
    EXPECT_TRUE(result.empty());
}

TEST(V2AoiSpatialGridTest, QueryAtWorldBoundary) {
    SpatialGrid grid(1000, 1000, 100);
    grid.add(1, 10, 10);        // near origin
    grid.add(2, 990, 990);      // near far corner

    // Query from origin — should see entity 1
    auto result = grid.query(0, 0, 50);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], 1U);

    // Query from far corner — should see entity 2
    result = grid.query(1000, 1000, 50);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], 2U);
}

// ─── Move ─────────────────────────────────────────────────────────

TEST(V2AoiSpatialGridTest, MoveUpdatesPosition) {
    SpatialGrid grid(1000, 1000, 100);
    grid.add(1, 50, 50);

    // Move entity far away
    grid.move(1, 500, 500);

    // Old position should be empty
    auto result = grid.query(50, 50, 10);
    EXPECT_TRUE(result.empty());

    // New position should have it
    result = grid.query(500, 500, 10);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], 1U);
}

TEST(V2AoiSpatialGridTest, MoveMissingIsNoOp) {
    SpatialGrid grid(1000, 1000, 100);
    grid.move(999, 500, 500);  // not present
    EXPECT_EQ(grid.size(), 0U);
}

// ─── Cell-conservative query (returns all in bounding cells) ──────

TEST(V2AoiSpatialGridTest, QueryReturnsAllInBoundingCells) {
    SpatialGrid grid(1000, 1000, 100);
    grid.add(1, 100, 100);    // cell (1,1)
    grid.add(2, 160, 100);    // cell (1,1) — dx=60 but same cell
    grid.add(3, 100, 160);    // cell (1,1) — dy=60 but same cell
    grid.add(4, 500, 500);    // cell (5,5) — far away, different cell

    auto result = grid.query(100, 100, 50);
    // Grid query is cell-conservative: returns 1, 2, 3 from cell (1,1)
    // but NOT 4 from a distant cell
    ASSERT_EQ(result.size(), 3U);
    EXPECT_NE(std::find(result.begin(), result.end(), 1U), result.end());
    EXPECT_NE(std::find(result.begin(), result.end(), 2U), result.end());
    EXPECT_NE(std::find(result.begin(), result.end(), 3U), result.end());
}

// ─── Multiple entities in same cell ───────────────────────────────

TEST(V2AoiSpatialGridTest, MultipleEntitiesInSameCell) {
    SpatialGrid grid(1000, 1000, 100);
    for (int i = 1; i <= 20; ++i) {
        grid.add(i, 10 + i, 10 + i);  // all in cell (0,0) or (0,0)/(1,1) area
    }
    EXPECT_EQ(grid.size(), 20U);

    // Query with small radius from center
    auto result = grid.query(20, 20, 50);
    EXPECT_EQ(result.size(), 20U);
}
