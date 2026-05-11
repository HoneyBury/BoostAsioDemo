#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace v2::io {

// SPSC lock-free ring buffer.
// Single producer thread, single consumer thread — no mutex needed.
template <typename T>
class SpscQueue {
    static constexpr std::size_t kDefaultCapacity = 1024;

    static constexpr std::size_t next_power_of_two(std::size_t n) noexcept {
        if (n <= 1) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

public:
    explicit SpscQueue(std::size_t capacity = kDefaultCapacity)
        : capacity_(next_power_of_two(capacity))
        , mask_(capacity_ - 1)
        , buffer_(capacity_) {}

    bool try_enqueue(T item) {
        const auto write = write_idx_.load(std::memory_order_relaxed);
        const auto read = read_idx_.load(std::memory_order_acquire);
        if (write - read >= capacity_) {
            return false;
        }
        buffer_[write & mask_] = std::move(item);
        write_idx_.store(write + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> try_dequeue() {
        const auto read = read_idx_.load(std::memory_order_relaxed);
        const auto write = write_idx_.load(std::memory_order_acquire);
        if (read >= write) {
            return std::nullopt;
        }
        T item = std::move(buffer_[read & mask_]);
        read_idx_.store(read + 1, std::memory_order_release);
        return item;
    }

    std::vector<T> drain() {
        std::vector<T> items;
        while (auto item = try_dequeue()) {
            items.push_back(std::move(*item));
        }
        return items;
    }

    [[nodiscard]] bool empty() const noexcept {
        return read_idx_.load(std::memory_order_acquire) >=
               write_idx_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t size() const noexcept {
        const auto w = write_idx_.load(std::memory_order_acquire);
        const auto r = read_idx_.load(std::memory_order_acquire);
        return w - r;
    }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<T> buffer_;

    // Separate cache lines to avoid false sharing between producer and consumer.
    alignas(64) std::atomic<std::size_t> write_idx_{0};
    alignas(64) std::atomic<std::size_t> read_idx_{0};
};

}  // namespace v2::io
