#include <gtest/gtest.h>

#include "v2/data/lru_cache.h"

// ─── Basic Put and Get ────────────────────────────────────────────

TEST(V2LruCacheTest, LruCacheBasicPutAndGet) {
    v2::data::LruCache<int, int> cache;

    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);

    auto v1 = cache.get(1);
    ASSERT_NE(v1, nullptr);
    EXPECT_EQ(*v1, 100);

    auto v2 = cache.get(2);
    ASSERT_NE(v2, nullptr);
    EXPECT_EQ(*v2, 200);

    auto v3 = cache.get(3);
    ASSERT_NE(v3, nullptr);
    EXPECT_EQ(*v3, 300);
}

// ─── Get Missing Returns Nullptr ──────────────────────────────────

TEST(V2LruCacheTest, LruCacheGetMissingReturnsNullptr) {
    v2::data::LruCache<int, int> cache;
    cache.put(1, 100);

    auto result = cache.get(999);
    EXPECT_EQ(result, nullptr);
}

// ─── Evicts Oldest When At Capacity ───────────────────────────────

TEST(V2LruCacheTest, LruCacheEvictsOldestWhenAtCapacity) {
    v2::data::LruCache<int, int> cache(3);

    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);

    // Cache is at capacity. Inserting a 4th item should evict key 1.
    cache.put(4, 400);

    EXPECT_EQ(cache.size(), 3U);
    EXPECT_EQ(cache.get(1), nullptr);   // evicted
    ASSERT_NE(cache.get(4), nullptr);
    EXPECT_EQ(*cache.get(4), 400);

    // Remaining items are accessible
    ASSERT_NE(cache.get(2), nullptr);
    EXPECT_EQ(*cache.get(2), 200);
    ASSERT_NE(cache.get(3), nullptr);
    EXPECT_EQ(*cache.get(3), 300);
}

// ─── Get Updates LRU Order ────────────────────────────────────────

TEST(V2LruCacheTest, LruCacheGetUpdatesLruOrder) {
    v2::data::LruCache<int, int> cache(3);

    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);

    // Access the middle item (key 1), making it most recently used.
    // Order should now be: 1, 3, 2.
    // So key 2 is now the LRU and should be evicted first.
    (void)cache.get(1);

    cache.put(4, 400);  // Should evict key 2 (the LRU)

    EXPECT_EQ(cache.get(2), nullptr);  // evicted
    ASSERT_NE(cache.get(1), nullptr);
    EXPECT_EQ(*cache.get(1), 100);
    ASSERT_NE(cache.get(3), nullptr);
    EXPECT_EQ(*cache.get(3), 300);
    ASSERT_NE(cache.get(4), nullptr);
    EXPECT_EQ(*cache.get(4), 400);
}

// ─── Put Duplicate Updates Value ──────────────────────────────────

TEST(V2LruCacheTest, LruCachePutDuplicateUpdatesValue) {
    v2::data::LruCache<int, int> cache(3);

    cache.put(1, 100);
    cache.put(1, 999);  // update value

    auto v = cache.get(1);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 999);
    EXPECT_EQ(cache.size(), 1U);
}

// ─── Remove Deletes Item ──────────────────────────────────────────

TEST(V2LruCacheTest, LruCacheRemoveDeletesItem) {
    v2::data::LruCache<int, int> cache;

    cache.put(1, 100);
    cache.put(2, 200);
    EXPECT_EQ(cache.size(), 2U);

    cache.remove(1);
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_EQ(cache.get(1), nullptr);
    ASSERT_NE(cache.get(2), nullptr);
    EXPECT_EQ(*cache.get(2), 200);
}

// ─── Clear Empties All ────────────────────────────────────────────

TEST(V2LruCacheTest, LruCacheClearEmptiesAll) {
    v2::data::LruCache<int, int> cache;

    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);
    EXPECT_EQ(cache.size(), 3U);

    cache.clear();
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.get(1), nullptr);
    EXPECT_EQ(cache.get(2), nullptr);
    EXPECT_EQ(cache.get(3), nullptr);
}

// ─── Size And MaxSize ─────────────────────────────────────────────

TEST(V2LruCacheTest, LruCacheSizeAndMaxSize) {
    v2::data::LruCache<int, int> cache(5);

    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.max_size(), 5U);

    cache.put(1, 100);
    EXPECT_EQ(cache.size(), 1U);
    cache.put(2, 200);
    EXPECT_EQ(cache.size(), 2U);

    // Default max_size
    v2::data::LruCache<int, int> default_cache;
    EXPECT_EQ(default_cache.max_size(), 10000U);
}

// ─── Contains Returns Correctly ───────────────────────────────────

TEST(V2LruCacheTest, LruCacheContainsReturnsCorrectly) {
    v2::data::LruCache<int, int> cache;

    cache.put(1, 100);
    cache.put(2, 200);

    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
    EXPECT_FALSE(cache.contains(3));

    cache.remove(1);
    EXPECT_FALSE(cache.contains(1));
}
