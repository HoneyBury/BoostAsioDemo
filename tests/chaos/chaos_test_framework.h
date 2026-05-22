// Boost Gateway — 混沌测试框架
//
// ChaosSimulator 类提供故障注入的编排能力，用于协议级别的混沌工程测试。
// 设计原则：
//   - 线程安全：多个 actor 可同时查询和注入故障
//   - 非侵入式：通过故障规则描述，不修改被测系统代码
//   - 可观测：记录故障注入和恢复事件，供测试断言
//
// 使用方式：
//   ChaosSimulator chaos;
//   chaos.add_rule({ChaosFailureType::MessageDrop, 0.05, "gateway", 0});
//   chaos.add_rule({ChaosFailureType::MessageDelay, 0.10, "backend", 2000});
//
//   if (chaos.should_inject("backend")) {
//       chaos.inject_delay("backend");
//       // 发送延迟后的消息
//   }

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace boost::gateway::chaos {

/// 故障类型枚举
enum class ChaosFailureType {
    NetworkPartition,     ///< 网络分区（连接阻断）
    ProcessKill,         ///< 进程杀死
    MessageDelay,        ///< 消息延迟
    MessageDrop,         ///< 消息丢弃
    MessageCorrupt,      ///< 消息篡改
    ResourceExhaustion,  ///< 资源耗尽
};

/// 将故障类型转换为可读字符串
inline const char* to_string(ChaosFailureType type) {
    switch (type) {
        case ChaosFailureType::NetworkPartition:    return "NetworkPartition";
        case ChaosFailureType::ProcessKill:         return "ProcessKill";
        case ChaosFailureType::MessageDelay:        return "MessageDelay";
        case ChaosFailureType::MessageDrop:         return "MessageDrop";
        case ChaosFailureType::MessageCorrupt:      return "MessageCorrupt";
        case ChaosFailureType::ResourceExhaustion:  return "ResourceExhaustion";
    }
    return "Unknown";
}

/// 混沌规则：描述一种故障注入行为
struct ChaosRule {
    ChaosFailureType failure_type;  ///< 故障类型
    double probability;             ///< 触发概率 [0.0, 1.0]
    std::string target_service;     ///< 目标服务名（"gateway", "backend", "login", "all"）
    std::uint32_t duration_ms;      ///< 故障持续时间（毫秒），0 表示瞬发

    /// 消息延迟专用：最大延迟毫秒数
    std::uint32_t max_delay_ms = 0;

    /// 消息丢弃专用：丢弃比例（与 probability 联合使用）
    double drop_ratio = 0.0;
};

/// 故障注入事件记录
struct ChaosEvent {
    ChaosFailureType type;
    std::string target_service;
    std::chrono::steady_clock::time_point injected_at;
    std::chrono::steady_clock::time_point recovered_at;
    bool recovered = false;
};

/// 混沌模拟器（线程安全）
class ChaosSimulator {
public:
    ChaosSimulator()
        : rng_(std::random_device{}()) {}

    /// 添加混沌规则
    void add_rule(const ChaosRule& rule) {
        std::scoped_lock lock(mutex_);
        rules_.push_back(rule);
        SPDLOG_INFO("[ChaosSimulator] added rule: type={}, probability={}, target={}, duration_ms={}",
                    to_string(rule.failure_type), rule.probability, rule.target_service, rule.duration_ms);
    }

    /// 清空所有规则
    void clear_rules() {
        std::scoped_lock lock(mutex_);
        rules_.clear();
        active_failures_.clear();
    }

    /// 设置全局启用/禁用
    void set_enabled(bool enabled) {
        enabled_.store(enabled, std::memory_order_relaxed);
    }

    /// 是否全局启用
    bool is_enabled() const {
        return enabled_.load(std::memory_order_relaxed);
    }

    /// 对给定的服务，判断是否应该注入故障
    bool should_inject(const std::string& service_name) {
        if (!enabled_.load(std::memory_order_relaxed)) {
            return false;
        }

        std::scoped_lock lock(mutex_);
        for (const auto& rule : rules_) {
            if (!matches_target(rule.target_service, service_name)) {
                continue;
            }

            // 检查是否已经在活跃故障中
            if (is_already_active(rule.failure_type, service_name)) {
                continue;
            }

            if (should_trigger(rule.probability)) {
                activate_failure(rule);
                return true;
            }
        }
        return false;
    }

    /// 注入消息延迟
    /// 返回延迟的毫秒数，0 表示不延迟
    std::uint32_t inject_delay(const std::string& service_name) {
        if (!enabled_.load(std::memory_order_relaxed)) {
            return 0;
        }

        std::scoped_lock lock(mutex_);
        for (const auto& rule : rules_) {
            if (rule.failure_type != ChaosFailureType::MessageDelay) {
                continue;
            }
            if (!matches_target(rule.target_service, service_name)) {
                continue;
            }
            if (should_trigger(rule.probability)) {
                const auto delay = std::uniform_int_distribution<std::uint32_t>(
                    100, std::max<std::uint32_t>(100, rule.max_delay_ms))(rng_);
                SPDLOG_INFO("[ChaosSimulator] injecting MessageDelay on {}: {}ms", service_name, delay);
                return delay;
            }
        }
        return 0;
    }

    /// 判断消息是否应该被丢弃
    bool should_drop(const std::string& service_name) {
        if (!enabled_.load(std::memory_order_relaxed)) {
            return false;
        }

        std::scoped_lock lock(mutex_);
        for (const auto& rule : rules_) {
            if (rule.failure_type != ChaosFailureType::MessageDrop) {
                continue;
            }
            if (!matches_target(rule.target_service, service_name)) {
                continue;
            }
            if (should_trigger(rule.probability)) {
                SPDLOG_INFO("[ChaosSimulator] dropping message for {}", service_name);
                return true;
            }
        }
        return false;
    }

    /// 判断消息是否应该被篡改
    bool should_corrupt(const std::string& service_name) {
        if (!enabled_.load(std::memory_order_relaxed)) {
            return false;
        }

        std::scoped_lock lock(mutex_);
        for (const auto& rule : rules_) {
            if (rule.failure_type != ChaosFailureType::MessageCorrupt) {
                continue;
            }
            if (!matches_target(rule.target_service, service_name)) {
                continue;
            }
            if (should_trigger(rule.probability)) {
                SPDLOG_INFO("[ChaosSimulator] corrupting message for {}", service_name);
                return true;
            }
        }
        return false;
    }

    /// 检查是否应该触发 NetworkPartition
    bool is_network_partitioned(const std::string& service_name) {
        if (!enabled_.load(std::memory_order_relaxed)) {
            return false;
        }

        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();

        for (const auto& [key, event] : active_failures_) {
            if (event.type == ChaosFailureType::NetworkPartition &&
                matches_target(event.target_service, service_name)) {
                if (event.recovered) {
                    continue;
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - event.injected_at);
                // 从规则中查找对应的 duration_ms
                for (const auto& rule : rules_) {
                    if (rule.failure_type == ChaosFailureType::NetworkPartition &&
                        matches_target(rule.target_service, service_name)) {
                        if (elapsed.count() < static_cast<std::int64_t>(rule.duration_ms)) {
                            return true;
                        }
                        // 超时自动恢复
                        const_cast<ChaosEvent&>(event).recovered = true;
                        const_cast<ChaosEvent&>(event).recovered_at = now;
                        SPDLOG_INFO("[ChaosSimulator] NetworkPartition auto-recovered for {} after {}ms",
                                    service_name, elapsed.count());
                        break;
                    }
                }
            }
        }
        return false;
    }

    /// 手动恢复指定服务的所有故障
    void recover(const std::string& service_name) {
        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto& [key, event] : active_failures_) {
            if (matches_target(event.target_service, service_name) && !event.recovered) {
                event.recovered = true;
                event.recovered_at = now;
                SPDLOG_INFO("[ChaosSimulator] manually recovered {} from {}",
                            service_name, to_string(event.type));
            }
        }
    }

    /// 恢复所有故障
    void recover_all() {
        std::scoped_lock lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto& [key, event] : active_failures_) {
            if (!event.recovered) {
                event.recovered = true;
                event.recovered_at = now;
            }
        }
        SPDLOG_INFO("[ChaosSimulator] recovered all failures");
    }

    /// 获取活跃故障列表
    std::vector<ChaosEvent> active_failures() const {
        std::scoped_lock lock(mutex_);
        std::vector<ChaosEvent> result;
        for (const auto& [key, event] : active_failures_) {
            result.push_back(event);
        }
        return result;
    }

    /// 获取事件总数
    std::size_t failure_count() const {
        std::scoped_lock lock(mutex_);
        return active_failures_.size();
    }

    /// 获取已恢复的事件数
    std::size_t recovered_count() const {
        std::scoped_lock lock(mutex_);
        return std::count_if(active_failures_.begin(), active_failures_.end(),
                             [](const auto& pair) { return pair.second.recovered; });
    }

    /// 获取未恢复的事件数
    std::size_t active_count() const {
        std::scoped_lock lock(mutex_);
        return std::count_if(active_failures_.begin(), active_failures_.end(),
                             [](const auto& pair) { return !pair.second.recovered; });
    }

private:
    /// 判断 target 是否匹配 service_name
    static bool matches_target(const std::string& target, const std::string& service_name) {
        return target == "all" || target == service_name;
    }

    /// 按概率触发
    bool should_trigger(double probability) {
        return std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < probability;
    }

    /// 检查故障是否已经活跃
    bool is_already_active(ChaosFailureType type, const std::string& service_name) {
        const auto key = make_key(type, service_name);
        return active_failures_.find(key) != active_failures_.end() &&
               !active_failures_[key].recovered;
    }

    /// 激活一个新的故障
    void activate_failure(const ChaosRule& rule) {
        const auto key = make_key(rule.failure_type, rule.target_service);
        const auto now = std::chrono::steady_clock::now();
        active_failures_[key] = ChaosEvent{
            rule.failure_type,
            rule.target_service,
            now,
            now,
            false,
        };
        SPDLOG_INFO("[ChaosSimulator] activated failure: type={}, target={}, duration_ms={}",
                    to_string(rule.failure_type), rule.target_service, rule.duration_ms);
    }

    static std::string make_key(ChaosFailureType type, const std::string& service) {
        return std::string(to_string(type)) + ":" + service;
    }

    mutable std::mutex mutex_;
    std::mt19937 rng_;
    std::atomic<bool> enabled_{true};
    std::vector<ChaosRule> rules_;
    std::unordered_map<std::string, ChaosEvent> active_failures_;
};

}  // namespace boost::gateway::chaos
