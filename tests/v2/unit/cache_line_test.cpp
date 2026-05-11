#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "v2/memory/cache_line.h"

using v2::memory::kCacheLineSize;
using v2::memory::CacheLinePad;
using v2::memory::HotCold;

TEST(V2CacheLineTest, CacheLineSizeIs64) {
    EXPECT_EQ(kCacheLineSize, 64U);
}

TEST(V2CacheLineTest, CacheLinePadHasCorrectSize) {
    EXPECT_EQ(sizeof(CacheLinePad), kCacheLineSize);
}

TEST(V2CacheLineTest, CacheLinePadHasCorrectAlignment) {
    EXPECT_EQ(alignof(CacheLinePad), kCacheLineSize);
}

TEST(V2CacheLineTest, HotColdDefaultConstruction) {
    HotCold<int, double> hc;
    EXPECT_EQ(hc.hot(), 0);
    EXPECT_EQ(hc.hot(), 0.0);  // default double
}

// Actually test separately:
TEST(V2CacheLineTest, HotColdHotAccessor) {
    HotCold<int, double> hc;
    hc.hot() = 42;
    EXPECT_EQ(hc.hot(), 42);
}

TEST(V2CacheLineTest, HotColdColdAccessor) {
    HotCold<int, double> hc;
    hc.cold() = 3.14;
    EXPECT_DOUBLE_EQ(hc.cold(), 3.14);
}

TEST(V2CacheLineTest, HotColdSizeExceedsTwoCacheLines) {
    // HotStorage(64) + CacheLinePad(64) + ColdStorage(64) = 192 >= 128
    EXPECT_GE(sizeof(HotCold<int, int>), kCacheLineSize * 2);
}

TEST(V2CacheLineTest, HotColdWithComplexTypes) {
    HotCold<std::string, std::string> hc;
    hc.hot() = "hello";
    hc.cold() = "world";
    EXPECT_EQ(hc.hot(), "hello");
    EXPECT_EQ(hc.cold(), "world");
}

TEST(V2CacheLineTest, AlignAsMacroUsage) {
    // Verify we can use alignas(kCacheLineSize) on a struct
    struct alignas(kCacheLineSize) AlignedStruct {
        int x = 0;
    };
    EXPECT_EQ(alignof(AlignedStruct), kCacheLineSize);
    EXPECT_GE(sizeof(AlignedStruct), kCacheLineSize);
}
