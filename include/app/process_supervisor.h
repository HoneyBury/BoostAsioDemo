#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// ProcessSupervisor — manages child process lifecycle on Windows.
//
// Starts child processes via CreateProcess (or fork+exec on POSIX),
// monitors them, and automatically restarts crashed processes up to
// a configurable limit. Supports health checks via TCP port ping,
// graceful shutdown, and crash event callbacks.
//
// On non-Windows platforms, the class compiles as a no-op stub.

namespace app {

class ProcessSupervisor {
public:
    struct ProcessConfig {
        std::string name;                     // Human-readable process name
        std::string executable;               // Path to the executable
        std::vector<std::string> args;        // Command-line arguments
        int restart_delay_ms = 3000;          // Delay before restart after crash
        int max_restarts = 5;                 // Max consecutive restarts (0 = infinite)
        std::chrono::seconds health_check_timeout{10};  // TCP connect timeout for health
        std::uint16_t health_check_port = 0;  // TCP port to ping (0 = skip health check)
    };

    using CrashCallback = std::function<void(const std::string& name, int exit_code)>;

    ProcessSupervisor();
    ~ProcessSupervisor();

    // Non-copyable, non-movable.
    ProcessSupervisor(const ProcessSupervisor&) = delete;
    ProcessSupervisor& operator=(const ProcessSupervisor&) = delete;

    // Register a process configuration. Must be called before start_all().
    void add_process(const ProcessConfig& config);

    // Start all registered processes. Returns true if all started successfully.
    bool start_all();

    // Stop all managed processes gracefully (SIGTERM/Ctrl+C equivalent),
    // then forcefully terminate after a timeout.
    void stop_all();

    // Block until all managed processes have exited (normal or crash).
    void wait();

    // Stop and restart a specific process by name.
    bool restart_process(const std::string& name);

    // Returns true if the named process is currently running.
    bool is_running(const std::string& name) const;

    // Set a callback invoked when a managed process crashes.
    void set_crash_callback(CrashCallback cb);

    // Get the number of restart attempts for a given process.
    int restart_count(const std::string& name) const;

private:
    // Internal per-process state.
    struct ProcessState {
        ProcessConfig config;
#ifdef _WIN32
        void* process_handle = nullptr;   // HANDLE (kept opaque to avoid windows.h leak)
        void* thread_handle   = nullptr;
        DWORD pid = 0;
#else
        pid_t pid = -1;
#endif
        std::atomic<bool> running{false};
        std::atomic<bool> stopping{false};
        int restart_attempts = 0;
        std::thread monitor_thread;
    };

    // Internal health check: try TCP connect to the configured port.
    static bool health_check(const ProcessConfig& config);

    // Monitor loop for a single process (runs in its own thread).
    void monitor_routine(std::shared_ptr<ProcessState> state);

#ifdef _WIN32
    // Start a process on Windows via CreateProcess.
    bool spawn_windows(const ProcessConfig& config, ProcessState& state);
#else
    // Start a process on POSIX via fork+exec.
    bool spawn_posix(const ProcessConfig& config, ProcessState& state);
#endif

    // Request a process to stop gracefully, then force kill after timeout.
    void terminate_process(const ProcessConfig& config, ProcessState& state);

    std::vector<std::shared_ptr<ProcessState>> processes_;
    CrashCallback crash_callback_;
    std::atomic<bool> supervisor_stopping_{false};
};

}  // namespace app
