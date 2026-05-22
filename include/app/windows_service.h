#pragma once

#include <cstdint>
#include <functional>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

// Windows Service wrapper for Boost Gateway backend processes.
// Provides the ability to register, unregister, start, stop and run
// backend processes as native Windows Services.
// On non-Windows platforms all methods are no-ops.

namespace app {

class WindowsService {
public:
    // Signature for the actual backend main function that will be wrapped.
    using ServiceMainFn = std::function<int(int argc, char* argv[])>;

#ifdef _WIN32
    // Register this binary as a Windows Service with the SCM.
    // Returns true on success.
    static bool register_service(const std::string& name,
                                  const std::string& display_name,
                                  const std::string& binary_path);

    // Remove a previously registered service from the SCM.
    static bool unregister_service(const std::string& name);

    // Start a registered service (requires appropriate privileges).
    static bool start_service(const std::string& name);

    // Stop a running service gracefully.
    static bool stop_service(const std::string& name);

    // Enter the service dispatch loop. The provided fn will be wrapped
    // in SERVICE_MAIN and called when the SCM starts this service.
    // This function does not return until the service stops.
    static int run_as_service(ServiceMainFn fn);

    // Query whether the current process was launched by the SCM
    // (i.e. is running as a Windows Service).
    static bool is_running_as_service();
#else
    // Non-Windows: stub implementations that return false / 0.
    static bool register_service(const std::string&, const std::string&, const std::string&) {
        return false; }
    static bool unregister_service(const std::string&) { return false; }
    static bool start_service(const std::string&) { return false; }
    static bool stop_service(const std::string&) { return false; }
    static int run_as_service(ServiceMainFn fn) {
        // On non-Windows, just run the function directly.
        return fn(0, nullptr);
    }
    static bool is_running_as_service() { return false; }
#endif

    WindowsService() = delete;

private:
#ifdef _WIN32
    // Internal SERVICE_MAIN entry point (called by the SCM).
    static void WINAPI service_main_entry(DWORD argc, char* argv[]);

    // Handler for control requests from the SCM.
    static DWORD WINAPI service_ctrl_handler_ex(
        DWORD control, DWORD event_type, void* event_data, void* context);

    // Current service status handle (set by RegisterServiceCtrlHandlerEx).
    static SERVICE_STATUS_HANDLE g_status_handle;

    // The user-provided main function.
    static ServiceMainFn g_user_main;

    // Current service status.
    static SERVICE_STATUS g_status;

    // Whether the service is requested to stop.
    static bool g_service_stopping;

    // Update service status with the SCM.
    static bool report_status(DWORD current_state,
                               DWORD exit_code = NO_ERROR,
                               DWORD wait_hint = 0);
#endif
};

}  // namespace app
