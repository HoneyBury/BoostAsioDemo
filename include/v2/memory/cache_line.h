#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace v2::memory {

// Standard cache line size on x86-64 and ARM64 (Apple Silicon, Graviton, etc.)
inline constexpr std::size_t kCacheLineSize = 64;

// Empty struct aligned to one cache line. Place between fields in a struct
// to force the next field onto a separate cache line, preventing false sharing.
struct alignas(kCacheLineSize) CacheLinePad {};

// ─── HotCold split ─────────────────────────────────────────────

// HotCold<Hot, Cold> stores Hot and Cold fields on separate cache lines so that
// frequent writes to one do not invalidate the other's cache line on other cores.
template <typename Hot, typename Cold>
class HotCold {
public:
    // Piecewise construction from forwarded argument tuples.
    template <typename... HotArgs, typename... ColdArgs>
    HotCold(std::piecewise_construct_t,
            std::tuple<HotArgs...> hot_args,
            std::tuple<ColdArgs...> cold_args)
        : hot_(std::make_from_tuple<Hot>(std::move(hot_args)))
        , cold_(std::make_from_tuple<Cold>(std::move(cold_args))) {}

    // Default construction requires both types to be default-constructible.
    HotCold()
        requires std::is_default_constructible_v<Hot> &&
                 std::is_default_constructible_v<Cold>
        : hot_(), cold_() {}

    [[nodiscard]] Hot& hot() noexcept { return hot_.value; }
    [[nodiscard]] const Hot& hot() const noexcept { return hot_.value; }
    [[nodiscard]] Cold& cold() noexcept { return cold_.value; }
    [[nodiscard]] const Cold& cold() const noexcept { return cold_.value; }

private:
    // Each field is wrapped in a cache-line-aligned struct. The padding between
    // them guarantees separation even when the structs are adjacent in memory.
    struct alignas(kCacheLineSize) HotStorage { Hot value; };
    struct alignas(kCacheLineSize) ColdStorage { Cold value; };

    HotStorage hot_;
    CacheLinePad pad_;
    ColdStorage cold_;
};

// ─── Compile-time verification ─────────────────────────────────

static_assert(sizeof(CacheLinePad) == kCacheLineSize,
              "CacheLinePad must be exactly one cache line");
static_assert(alignof(CacheLinePad) == kCacheLineSize,
              "CacheLinePad must align to cache line boundary");

// Verify that HotCold places fields on different cache lines.
// Two cache-line-aligned structs + a padding pad give at least 3 cache lines.
static_assert(sizeof(HotCold<int, int>) >= kCacheLineSize * 2,
              "Hot and Cold fields must be on separate cache lines");

}  // namespace v2::memory
