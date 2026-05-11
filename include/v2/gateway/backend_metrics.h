#pragma once

#include "v2/service/service_id.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace v2::gateway {

struct BackendMetricsSnapshot {
    std::uint64_t total_requests = 0;
    std::uint64_t total_successes = 0;
    std::uint64_t total_timeouts = 0;
    std::uint64_t total_unavailable = 0;
    std::uint64_t total_errors = 0;
};

class BackendMetrics {
public:
    void record_request(v2::service::ServiceId service) {
        std::scoped_lock lock(mutex_);
        counters_[service].total_requests++;
    }

    void record_success(v2::service::ServiceId service) {
        std::scoped_lock lock(mutex_);
        counters_[service].total_successes++;
    }

    void record_timeout(v2::service::ServiceId service) {
        std::scoped_lock lock(mutex_);
        counters_[service].total_timeouts++;
    }

    void record_unavailable(v2::service::ServiceId service) {
        std::scoped_lock lock(mutex_);
        counters_[service].total_unavailable++;
    }

    void record_error(v2::service::ServiceId service) {
        std::scoped_lock lock(mutex_);
        counters_[service].total_errors++;
    }

    [[nodiscard]] BackendMetricsSnapshot snapshot(
        v2::service::ServiceId service) const {
        std::scoped_lock lock(mutex_);
        auto it = counters_.find(service);
        if (it != counters_.end()) return it->second;
        return {};
    }

    [[nodiscard]] std::unordered_map<v2::service::ServiceId, BackendMetricsSnapshot>
    all_snapshots() const {
        std::scoped_lock lock(mutex_);
        return counters_;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<v2::service::ServiceId, BackendMetricsSnapshot> counters_;
};

inline const char* service_id_to_key(v2::service::ServiceId id) {
    switch (id) {
        case v2::service::ServiceId::kLogin:  return "login";
        case v2::service::ServiceId::kRoom:   return "room";
        case v2::service::ServiceId::kBattle: return "battle";
        default: return "unknown";
    }
}

}  // namespace v2::gateway
