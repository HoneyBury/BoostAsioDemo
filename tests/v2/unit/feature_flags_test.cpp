#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "v2/config/feature_flags.h"

// ─── Unregistered Flag Returns False ─────────────────────────────

TEST(V2FeatureFlagsTest, UnregisteredFlagReturnsFalse) {
    v2::config::FeatureFlags flags;

    // 未注册的 flag 应当返回 false，无论 user_id 如何
    EXPECT_FALSE(flags.is_enabled("non_existent", "user_1"));
    EXPECT_FALSE(flags.is_enabled("non_existent", std::uint64_t{42}));
    EXPECT_FALSE(flags.is_enabled("never_registered", "any_user"));
}

// ─── Disabled Flag Returns False ─────────────────────────────────

TEST(V2FeatureFlagsTest, DisabledFlagReturnsFalse) {
    v2::config::FeatureFlags flags;

    // 注册一个 enabled=false 的 flag，即使 100% 放量也不该启用
    flags.register_flag("test_feature", 100, false);
    EXPECT_FALSE(flags.is_enabled("test_feature", "user_a"));
    EXPECT_FALSE(flags.is_enabled("test_feature", std::uint64_t{123}));

    // 即使关闭后启用，再关闭也应返回 false
    flags.set_enabled("test_feature", true);
    EXPECT_TRUE(flags.is_enabled("test_feature", "user_a"));

    flags.set_enabled("test_feature", false);
    EXPECT_FALSE(flags.is_enabled("test_feature", "user_a"));
}

// ─── Zero Percent Returns False ──────────────────────────────────

TEST(V2FeatureFlagsTest, ZeroPercentReturnsFalse) {
    v2::config::FeatureFlags flags;

    // rollout_percentage=0 即使 enabled=true 也返回 false
    flags.register_flag("zero_rollout", 0, true);
    EXPECT_FALSE(flags.is_enabled("zero_rollout", "user_1"));
    EXPECT_FALSE(flags.is_enabled("zero_rollout", "user_999"));
    EXPECT_FALSE(flags.is_enabled("zero_rollout", std::uint64_t{0}));
    EXPECT_FALSE(flags.is_enabled("zero_rollout", std::uint64_t{999999}));
}

// ─── Full Rollout Returns True ───────────────────────────────────

TEST(V2FeatureFlagsTest, FullRolloutReturnsTrue) {
    v2::config::FeatureFlags flags;

    // rollout_percentage=100 且 enabled=true 时始终返回 true
    flags.register_flag("full_rollout", 100, true);
    EXPECT_TRUE(flags.is_enabled("full_rollout", "any_user"));
    EXPECT_TRUE(flags.is_enabled("full_rollout", "another_user"));
    EXPECT_TRUE(flags.is_enabled("full_rollout", std::uint64_t{1}));
    EXPECT_TRUE(flags.is_enabled("full_rollout", std::uint64_t{999999}));
}

// ─── Consistent Hash For Same User ───────────────────────────────

TEST(V2FeatureFlagsTest, ConsistentHashForSameUser) {
    v2::config::FeatureFlags flags;

    // 同一 user_id 多次调用应返回相同结果（确定性哈希）
    flags.register_flag("consistent", 50, true);

    bool result1 = flags.is_enabled("consistent", "deterministic_user");
    bool result2 = flags.is_enabled("consistent", "deterministic_user");
    EXPECT_EQ(result1, result2);

    // 对整数 user_id 同样应具有确定性
    bool result3 = flags.is_enabled("consistent", std::uint64_t{12345});
    bool result4 = flags.is_enabled("consistent", std::uint64_t{12345});
    EXPECT_EQ(result3, result4);

    // 多次重复验证
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(
            flags.is_enabled("consistent", "stable_user"),
            flags.is_enabled("consistent", "stable_user")
        );
    }
}

// ─── Percentage Distribution ─────────────────────────────────────

TEST(V2FeatureFlagsTest, PercentageDistribution) {
    v2::config::FeatureFlags flags;

    // 1000 个不同 user_id 在 30% 放量下，启用比例应接近 30%
    constexpr int kUserCount = 1000;
    constexpr std::uint32_t kRolloutPct = 30;

    flags.register_flag("pct_dist", kRolloutPct, true);

    int enabled_count = 0;
    for (int i = 0; i < kUserCount; ++i) {
        std::string uid = "dist_user_" + std::to_string(i);
        if (flags.is_enabled("pct_dist", uid)) {
            ++enabled_count;
        }
    }

    // 允许 +/-5% 的波动范围：期望 300 人，允许 250-350
    // （1,000 * 30% = 300）
    EXPECT_GE(enabled_count, 250);
    EXPECT_LE(enabled_count, 350);
}

// ─── Set Rollout Percentage Takes Effect ─────────────────────────

TEST(V2FeatureFlagsTest, SetRolloutPercentageTakesEffect) {
    v2::config::FeatureFlags flags;

    // 注册一个 0% 的 flag，应全部返回 false
    flags.register_flag("dynamic_pct", 0, true);
    EXPECT_FALSE(flags.is_enabled("dynamic_pct", "any_user"));

    // 动态修改为 100%，应全部返回 true
    flags.set_rollout_percentage("dynamic_pct", 100);
    EXPECT_TRUE(flags.is_enabled("dynamic_pct", "any_user"));

    // 动态修改为 50%，同一用户应仍返回相同值（确定性）
    flags.set_rollout_percentage("dynamic_pct", 50);
    bool r1 = flags.is_enabled("dynamic_pct", "bucketed_user");
    bool r2 = flags.is_enabled("dynamic_pct", "bucketed_user");
    EXPECT_EQ(r1, r2);

    // 动态修改为 0%，全部 return false
    flags.set_rollout_percentage("dynamic_pct", 0);
    EXPECT_FALSE(flags.is_enabled("dynamic_pct", "bucketed_user"));
}

// ─── Thread Safety Smoke ─────────────────────────────────────────

TEST(V2FeatureFlagsTest, ThreadSafetySmoke) {
    v2::config::FeatureFlags flags;

    // 注册多个 flag
    flags.register_flag("thread_flag_a", 50, true);
    flags.register_flag("thread_flag_b", 30, true);
    flags.register_flag("thread_flag_c", 100, true);
    flags.register_flag("thread_flag_d", 0, true);

    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;

    constexpr int kReaderCount = 8;
    constexpr int kWriterCount = 2;

    // 读线程：持续调 is_enabled
    for (int t = 0; t < kReaderCount; ++t) {
        threads.emplace_back([&flags, &stop, t]() {
            while (!stop.load(std::memory_order_relaxed)) {
                std::string uid = "thread_user_" + std::to_string(t);
                // 调不同 flag 和不同 user_id 组合
                (void)flags.is_enabled("thread_flag_a", uid);
                (void)flags.is_enabled("thread_flag_b", uid);
                (void)flags.is_enabled("thread_flag_c",
                                       static_cast<std::uint64_t>(t));
                (void)flags.is_enabled("thread_flag_d",
                                       static_cast<std::uint64_t>(t + 1000));
                // 读不存在的 flag
                (void)flags.is_enabled("nonexistent_" + std::to_string(t),
                                       uid);
            }
        });
    }

    // 写线程：持续调 register_flag / set_rollout_percentage
    for (int t = 0; t < kWriterCount; ++t) {
        threads.emplace_back([&flags, &stop, t]() {
            int iter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                std::string name = "dynamic_flag_" + std::to_string(t) + "_" + std::to_string(iter);
                flags.register_flag(name, (iter * 10) % 100, true);
                flags.set_rollout_percentage("thread_flag_a", (iter * 7) % 101);
                flags.set_enabled("thread_flag_b", (iter % 2) == 0);
                flags.unregister_flag("thread_flag_c");
                flags.register_flag("thread_flag_c", 100, true);
                ++iter;
                if (iter > 100) break;
            }
        });
    }

    // 让线程运行一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop.store(true, std::memory_order_relaxed);

    // 等待所有线程结束
    for (auto& th : threads) {
        th.join();
    }

    // 运行后状态应仍一致
    EXPECT_TRUE(flags.is_enabled("thread_flag_c", "final_check"));
    EXPECT_FALSE(flags.is_enabled("thread_flag_d", "final_check"));
}
