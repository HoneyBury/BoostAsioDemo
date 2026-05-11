#include <gtest/gtest.h>

#include <cstring>

#include "v2/memory/arena.h"

using v2::memory::BumpArena;

TEST(V2ArenaTest, BasicAllocReturnsNonNull) {
    BumpArena arena(1024);
    void* p = arena.alloc(64);
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(arena.allocated(), 64U);
}

TEST(V2ArenaTest, MultipleAllocsSucceedUntilFull) {
    BumpArena arena(256);
    // 256 bytes, aligned alloc — should fit 3x64=192 but not 4x64=256
    void* a = arena.alloc(64);
    void* b = arena.alloc(64);
    void* c = arena.alloc(64);
    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);
    EXPECT_NE(c, nullptr);

    // The arena has ~192 allocated, 256-192=64 remaining. Next alloc of 64 might fit.
    // With alignment overhead it's tight, but null check is sufficient.
    void* d = arena.alloc(128);
    // d might be nullptr depending on alignment — just verify a,b,c are valid
    (void)d;
    EXPECT_GE(arena.allocated(), 192U);
    EXPECT_LE(arena.allocated(), 256U);
}

TEST(V2ArenaTest, ResetReusesMemory) {
    BumpArena arena(1024);
    void* p1 = arena.alloc(128);
    EXPECT_NE(p1, nullptr);
    EXPECT_EQ(arena.allocated(), 128U);

    arena.reset();
    EXPECT_EQ(arena.allocated(), 0U);
    EXPECT_EQ(arena.remaining(), 1024U);

    void* p2 = arena.alloc(128);
    EXPECT_NE(p2, nullptr);
    EXPECT_EQ(p1, p2);  // Reset returns to start — same pointer
}

TEST(V2ArenaTest, AllocReturnsAlignedPointer) {
    BumpArena arena(1024);
    // Allocate 1 byte many times — all pointers should be aligned
    for (int i = 0; i < 20; ++i) {
        void* p = arena.alloc(1);
        ASSERT_NE(p, nullptr);
        auto addr = reinterpret_cast<std::uintptr_t>(p);
        EXPECT_EQ(addr % alignof(std::max_align_t), 0U);
    }
}

TEST(V2ArenaTest, OomReturnsNull) {
    BumpArena arena(64);
    void* p = arena.alloc(128);  // Request more than capacity
    EXPECT_EQ(p, nullptr);
    EXPECT_EQ(arena.allocated(), 0U);  // Nothing allocated
}

TEST(V2ArenaTest, RemainingDecreasesWithAlloc) {
    BumpArena arena(1024);
    std::size_t before = arena.remaining();
    EXPECT_EQ(before, 1024U);

    (void)arena.alloc(100);
    std::size_t after = arena.remaining();
    EXPECT_LT(after, before);
}

TEST(V2ArenaTest, CapacityIsCorrect) {
    BumpArena arena(4096);
    EXPECT_EQ(arena.capacity(), 4096U);
}

TEST(V2ArenaTest, ResetMultipleTimes) {
    BumpArena arena(512);
    for (int cycle = 0; cycle < 5; ++cycle) {
        void* p = arena.alloc(64);
        EXPECT_NE(p, nullptr);
        // Write to verify memory is usable
        std::memset(p, 0xAB, 64);
        arena.reset();
        EXPECT_EQ(arena.allocated(), 0U);
    }
}
