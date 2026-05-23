#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace v2::platform {

/// RAII helper that boosts the Windows timer resolution to 1 ms for the
/// lifetime of the object.  The default system timer tick is ~15.6 ms;
/// without this, std::this_thread::sleep_for(1ms) actually sleeps for
/// ~15.6 ms, which adds ~30 ms to every gateway-to-backend round-trip.
///
/// Use at the top of main() in every gateway and backend process.
class HighResTimer {
public:
    HighResTimer() noexcept {
#ifdef _WIN32
        timeBeginPeriod(1);
#endif
    }

    HighResTimer(const HighResTimer&) = delete;
    HighResTimer& operator=(const HighResTimer&) = delete;

    ~HighResTimer() noexcept {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }
};

}  // namespace v2::platform
