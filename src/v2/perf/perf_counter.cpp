#include "v2/perf/hot_path.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace v2::perf {

// ============================================================================
// Global registry (thread-safe)
// ============================================================================

namespace {

class CounterRegistry {
public:
    void register_counter(PerfCounter* counter) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find(counters_.begin(), counters_.end(), counter);
        if (it == counters_.end()) {
            counters_.push_back(counter);
        }
    }

    void unregister_counter(PerfCounter* counter) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find(counters_.begin(), counters_.end(), counter);
        if (it != counters_.end()) {
            counters_.erase(it);
        }
    }

    std::size_t dump_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* counter : counters_) {
            auto snap = counter->snapshot();
            if (snap.count > 0) {
                SPDLOG_INFO("[PERF] {}: count={} min={:.1f}us max={:.1f}us "
                            "avg={:.1f}us p50={:.1f}us p99={:.1f}us",
                            counter->name(),
                            snap.count,
                            snap.min_us,
                            snap.max_us,
                            snap.avg_us,
                            snap.p50_us,
                            snap.p99_us);
            }
            counter->reset();
        }
        return counters_.size();
    }

private:
    std::mutex mutex_;
    std::vector<PerfCounter*> counters_;
};

CounterRegistry& global_registry() {
    static CounterRegistry registry;
    return registry;
}

}  // anonymous namespace

// ============================================================================
// PerfCounter
// ============================================================================

PerfCounter::PerfCounter(const char* name) noexcept
    : name_(name) {
    storage_.samples.reserve(kMaxSamples);
    register_perf_counter(this);
}

std::uint64_t PerfCounter::record() noexcept {
#if defined(_MSC_VER)
    return __rdtsc();
#else
    return static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

double PerfCounter::latency_us(std::uint64_t start) const noexcept {
#if defined(_MSC_VER)
    // TSC-based timing: convert ticks to microseconds.
    // We use a simple calibration: assume ~2-3 GHz typical, but for accurate
    // results we query the actual frequency.
    static const double ticks_per_us = []() -> double {
        LARGE_INTEGER freq;
        if (QueryPerformanceFrequency(&freq)) {
            // TSC is typically invariant on modern Windows, but we fall back
            // to QPC for calibration. We measure the TSC delta over a known
            // QPC interval.
            auto qpc_start = std::chrono::steady_clock::now();
            auto tsc_start = __rdtsc();
            // Busy-wait ~1ms
            volatile std::uint64_t dummy = 0;
            auto qpc_now = std::chrono::steady_clock::now();
            while (std::chrono::duration_cast<std::chrono::microseconds>(
                       qpc_now - qpc_start)
                       .count() < 1000) {
                dummy += __rdtsc();
                qpc_now = std::chrono::steady_clock::now();
            }
            auto tsc_end = __rdtsc();
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                  qpc_now - qpc_start)
                                  .count();
            if (elapsed_us > 0) {
                return static_cast<double>(tsc_end - tsc_start) /
                       static_cast<double>(elapsed_us);
            }
        }
        return 2500.0;  // fallback: assume 2.5 GHz
    }();
    auto delta = __rdtsc() - start;
    return static_cast<double>(delta) / ticks_per_us;
#else
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto delta = now - static_cast<std::int64_t>(start);
    return static_cast<double>(delta) / 1000.0;
#endif
}

void PerfCounter::sample(double elapsed_us) noexcept {
    auto& s = storage_;
    if (s.sample_count < kMaxSamples) {
        s.samples.push_back(elapsed_us);
    }
    s.sample_count++;
    s.running_min = std::min(s.running_min, elapsed_us);
    s.running_max = std::max(s.running_max, elapsed_us);
    s.running_sum += elapsed_us;
}

PerfSnapshot PerfCounter::snapshot() const noexcept {
    PerfSnapshot snap;
    const auto& s = storage_;

    snap.count = s.sample_count;
    if (snap.count == 0) {
        snap.min_us = 0.0;
        snap.max_us = 0.0;
        snap.avg_us = 0.0;
        snap.p50_us = 0.0;
        snap.p99_us = 0.0;
        return snap;
    }

    snap.min_us = s.running_min;
    snap.max_us = s.running_max;
    snap.avg_us = s.running_sum / static_cast<double>(snap.count);

    // Compute percentiles from stored samples.
    if (s.samples.size() >= 2) {
        auto sorted = s.samples;
        std::sort(sorted.begin(), sorted.end());

        auto p50_idx = static_cast<std::size_t>(sorted.size() * 0.50);
        auto p99_idx = static_cast<std::size_t>(sorted.size() * 0.99);
        if (p50_idx >= sorted.size()) p50_idx = sorted.size() - 1;
        if (p99_idx >= sorted.size()) p99_idx = sorted.size() - 1;

        snap.p50_us = sorted[p50_idx];
        snap.p99_us = sorted[p99_idx];
    } else {
        snap.p50_us = s.running_min;
        snap.p99_us = s.running_max;
    }

    return snap;
}

void PerfCounter::reset() noexcept {
    auto& s = storage_;
    s.samples.clear();
    s.sample_count = 0;
    s.running_min = std::numeric_limits<double>::max();
    s.running_max = std::numeric_limits<double>::min();
    s.running_sum = 0.0;
}

// ============================================================================
// Global registry API
// ============================================================================

void register_perf_counter(PerfCounter* counter) noexcept {
    global_registry().register_counter(counter);
}

void unregister_perf_counter(PerfCounter* counter) noexcept {
    global_registry().unregister_counter(counter);
}

std::size_t dump_all_counters() noexcept {
    return global_registry().dump_all();
}

void periodic_perf_report() noexcept {
    static auto last_report = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_report);
    if (elapsed.count() >= 60) {
        SPDLOG_INFO("[PERF] === Periodic performance report ===");
        dump_all_counters();
        last_report = now;
    }
}

}  // namespace v2::perf
