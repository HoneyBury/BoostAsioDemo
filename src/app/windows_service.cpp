#include "app/windows_service.h"

#include "app/logging.h"

#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace app {

#ifdef _WIN32

// ─── Static members ─────────────────────────────────────────────────

SERVICE_STATUS_HANDLE WindowsService::g_status_handle = nullptr;
WindowsService::ServiceMainFn WindowsService::g_user_main = nullptr;
SERVICE_STATUS WindowsService::g_status{};
bool WindowsService::g_service_stopping = false;

// ─── Helpers ────────────────────────────────────────────────────────

bool WindowsService::report_status(DWORD current_state,
                                    DWORD exit_code,
                                    DWORD wait_hint) {
    if (g_status_handle == nullptr) {
        return false;
    }

    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = current_state;
    g_status.dwControlsAccepted = 0;
    g_status.dwWin32ExitCode = exit_code;
    g_status.dwServiceSpecificExitCode = 0;
    g_status.dwCheckPoint = 0;
    g_status.dwWaitHint = 0;

    if (current_state == SERVICE_START_PENDING) {
        g_status.dwControlsAccepted = 0;
    } else if (current_state == SERVICE_RUNNING) {
        g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP |
                                      SERVICE_ACCEPT_SHUTDOWN |
                                      SERVICE_ACCEPT_PRESHUTDOWN;
    } else if (current_state == SERVICE_STOP_PENDING) {
        g_status.dwControlsAccepted = 0;
        g_status.dwWaitHint = wait_hint;
    } else if (current_state == SERVICE_STOPPED) {
        g_status.dwControlsAccepted = 0;
        g_status.dwCheckPoint = 0;
        g_status.dwWaitHint = 0;
    }

    // Set checkpoint for pending states to avoid SCM timeout.
    static DWORD s_checkpoint = 1;
    if (current_state == SERVICE_START_PENDING ||
        current_state == SERVICE_STOP_PENDING) {
        g_status.dwCheckPoint = s_checkpoint++;
        g_status.dwWaitHint = wait_hint > 0 ? wait_hint : 3000;
    }

    return SetServiceStatus(g_status_handle, &g_status) != FALSE;
}

// ─── Service control handler ───────────────────────────────────────

DWORD WINAPI WindowsService::service_ctrl_handler_ex(
    DWORD control, DWORD /*event_type*/, void* /*event_data*/, void* /*context*/) {

    switch (control) {
    case SERVICE_CONTROL_STOP:
        LOG_INFO("WindowsService: received SERVICE_CONTROL_STOP");
        g_service_stopping = true;
        report_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        if (g_user_main) {
            // Signal the running service to stop.
            // The service main loop should check g_service_stopping.
        }
        return NO_ERROR;

    case SERVICE_CONTROL_SHUTDOWN:
        LOG_INFO("WindowsService: received SERVICE_CONTROL_SHUTDOWN");
        g_service_stopping = true;
        report_status(SERVICE_STOPPED, NO_ERROR);
        return NO_ERROR;

    case SERVICE_CONTROL_PRESHUTDOWN:
        LOG_INFO("WindowsService: received SERVICE_CONTROL_PRESHUTDOWN");
        g_service_stopping = true;
        report_status(SERVICE_STOPPED, NO_ERROR);
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_status_handle, &g_status);
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

// ─── Service main entry point ──────────────────────────────────────

void WINAPI WindowsService::service_main_entry(DWORD /*argc*/, char* /*argv*/[]) {
    // Register the control handler immediately.
    g_status_handle = RegisterServiceCtrlHandlerExW(
        L"",  // empty — matched by the service name from CreateService
        service_ctrl_handler_ex,
        nullptr);

    if (g_status_handle == nullptr) {
        LOG_ERROR("WindowsService: RegisterServiceCtrlHandlerEx failed (error {})",
                  GetLastError());
        return;
    }

    // Report start pending.
    if (!report_status(SERVICE_START_PENDING, NO_ERROR, 3000)) {
        LOG_ERROR("WindowsService: SetServiceStatus (START_PENDING) failed");
        return;
    }

    // Report running.
    if (!report_status(SERVICE_RUNNING)) {
        LOG_ERROR("WindowsService: SetServiceStatus (RUNNING) failed");
        return;
    }

    LOG_INFO("WindowsService: service is now running, starting user main...");

    // Run the user-provided main function.
    // We construct default argc/argv since the SCM may pass its own.
    int fake_argc = 1;
    char* fake_argv[] = {const_cast<char*>("service"), nullptr};
    int exit_code = 0;
    if (g_user_main) {
        exit_code = g_user_main(fake_argc, fake_argv);
    }

    LOG_INFO("WindowsService: user main exited with code {}", exit_code);

    // Report final stopped status.
    report_status(SERVICE_STOPPED, exit_code == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR,
                  exit_code);
}

// ─── Public API ─────────────────────────────────────────────────────

bool WindowsService::register_service(const std::string& name,
                                       const std::string& display_name,
                                       const std::string& binary_path) {
    SC_HANDLE scm = OpenSCManager(
        nullptr,                    // local machine
        nullptr,                    // default database
        SC_MANAGER_CREATE_SERVICE); // required for CreateService

    if (scm == nullptr) {
        LOG_ERROR("WindowsService: OpenSCManager failed (error {})", GetLastError());
        return false;
    }

    // Build command line: binary path with optional --service flag
    std::string cmd_line = binary_path;
    if (cmd_line.find(' ') != std::string::npos) {
        cmd_line = "\"" + cmd_line + "\"";
    }
    cmd_line += " --service";

    std::wstring wname(name.begin(), name.end());
    std::wstring wdisplay_name(display_name.begin(), display_name.end());
    std::wstring wcmd_line(cmd_line.begin(), cmd_line.end());

    SC_HANDLE service = CreateServiceW(
        scm,                            // SCM handle
        wname.c_str(),                  // service name
        wdisplay_name.c_str(),          // display name
        SERVICE_ALL_ACCESS,             // desired access
        SERVICE_WIN32_OWN_PROCESS,      // service type
        SERVICE_AUTO_START,             // start type
        SERVICE_ERROR_NORMAL,           // error control
        wcmd_line.c_str(),              // binary path
        nullptr,                        // no load order group
        nullptr,                        // no tag identifier
        nullptr,                        // no dependencies
        nullptr,                        // no local system account
        nullptr);                       // no password

    DWORD last_error = GetLastError();

    CloseServiceHandle(scm);

    if (service == nullptr) {
        LOG_ERROR("WindowsService: CreateService failed (error {})", last_error);
        return false;
    }

    // Set failure actions: restart on crash with delays.
    SERVICE_FAILURE_ACTIONSW failure_actions{};
    SC_ACTION actions[3];
    actions[0].Type = SC_ACTION_RESTART;
    actions[0].Delay = 3000;
    actions[1].Type = SC_ACTION_RESTART;
    actions[1].Delay = 10000;
    actions[2].Type = SC_ACTION_NONE;
    actions[2].Delay = 0;

    failure_actions.dwResetPeriod = 86400; // 1 day
    failure_actions.lpCommand = nullptr;
    failure_actions.lpRebootMsg = nullptr;
    failure_actions.cActions = 3;
    failure_actions.lpsaActions = actions;

    ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure_actions);

    CloseServiceHandle(service);

    LOG_INFO("WindowsService: registered service '{}' ('{}') at {}",
             name, display_name, binary_path);
    return true;
}

bool WindowsService::unregister_service(const std::string& name) {
    SC_HANDLE scm = OpenSCManager(
        nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        LOG_ERROR("WindowsService: OpenSCManager failed (error {})", GetLastError());
        return false;
    }

    std::wstring wname(name.begin(), name.end());
    SC_HANDLE service = OpenServiceW(
        scm, wname.c_str(),
        SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);

    if (service == nullptr) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            LOG_WARN("WindowsService: service '{}' does not exist", name);
            return false;
        }
        LOG_ERROR("WindowsService: OpenService failed (error {})", err);
        return false;
    }

    // Try to stop the service first.
    SERVICE_STATUS stop_status{};
    ControlService(service, SERVICE_CONTROL_STOP, &stop_status);

    bool deleted = DeleteService(service) != FALSE;
    DWORD last_error = GetLastError();

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!deleted) {
        LOG_ERROR("WindowsService: DeleteService failed (error {})", last_error);
        return false;
    }

    LOG_INFO("WindowsService: unregistered service '{}'", name);
    return true;
}

bool WindowsService::start_service(const std::string& name) {
    SC_HANDLE scm = OpenSCManager(
        nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        LOG_ERROR("WindowsService: OpenSCManager failed (error {})", GetLastError());
        return false;
    }

    std::wstring wname(name.begin(), name.end());
    SC_HANDLE service = OpenServiceW(
        scm, wname.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        LOG_ERROR("WindowsService: OpenService failed (error {})", err);
        return false;
    }

    // Check if already running.
    SERVICE_STATUS current_status{};
    if (QueryServiceStatus(service, &current_status)) {
        if (current_status.dwCurrentState == SERVICE_RUNNING) {
            LOG_WARN("WindowsService: service '{}' is already running", name);
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return true;
        }
    }

    bool started = StartServiceW(service, 0, nullptr) != FALSE;
    DWORD last_error = GetLastError();

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!started) {
        LOG_ERROR("WindowsService: StartService failed (error {})", last_error);
        return false;
    }

    LOG_INFO("WindowsService: started service '{}'", name);
    return true;
}

bool WindowsService::stop_service(const std::string& name) {
    SC_HANDLE scm = OpenSCManager(
        nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        LOG_ERROR("WindowsService: OpenSCManager failed (error {})", GetLastError());
        return false;
    }

    std::wstring wname(name.begin(), name.end());
    SC_HANDLE service = OpenServiceW(
        scm, wname.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (service == nullptr) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        LOG_ERROR("WindowsService: OpenService failed (error {})", err);
        return false;
    }

    SERVICE_STATUS stop_status{};
    bool stopped = ControlService(service, SERVICE_CONTROL_STOP, &stop_status) != FALSE;
    DWORD last_error = GetLastError();

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (!stopped) {
        LOG_ERROR("WindowsService: ControlService(STOP) failed (error {})", last_error);
        return false;
    }

    LOG_INFO("WindowsService: stopping service '{}'", name);
    return true;
}

int WindowsService::run_as_service(ServiceMainFn fn) {
    if (!fn) {
        LOG_ERROR("WindowsService: run_as_service called with null function");
        return 1;
    }

    g_user_main = std::move(fn);

    // Build the service name from the executable name.
    // The service main table requires at least one entry.
    SERVICE_TABLE_ENTRYW service_table[] = {
        {const_cast<wchar_t*>(L""), service_main_entry},
        {nullptr, nullptr}  // terminator
    };

    LOG_INFO("WindowsService: entering dispatch loop...");
    bool dispatched = StartServiceCtrlDispatcherW(service_table) != FALSE;

    if (!dispatched) {
        DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            LOG_WARN("WindowsService: not launched by SCM, running in console mode");
            // If we are not launched by the SCM, run the function directly.
            int fake_argc = 1;
            char* fake_argv[] = {const_cast<char*>("service"), nullptr};
            int result = g_user_main ? g_user_main(fake_argc, fake_argv) : 0;
            g_user_main = nullptr;
            return result;
        }
        LOG_ERROR("WindowsService: StartServiceCtrlDispatcher failed (error {})", err);
        return 1;
    }

    g_user_main = nullptr;
    return 0;
}

bool WindowsService::is_running_as_service() {
    // Check if the parent process is the SCM (services.exe).
    // A reliable heuristic: check if StartServiceCtrlDispatcher would succeed
    // (it fails with ERROR_FAILED_SERVICE_CONTROLLER_CONNECT if not in a service).
    // However, we can't call it here without side effects. Instead, check
    // if the process was started with the --service flag or has no console.
    // A more robust approach: check the parent process name.
    static bool cached = false;
    static bool result = false;

    if (cached) return result;

    // Check the command line for --service flag.
    LPWSTR cmd_line = GetCommandLineW();
    if (cmd_line != nullptr && wcsstr(cmd_line, L"--service") != nullptr) {
        result = true;
    } else {
        // Alternative: check if no console is attached (typical for services).
        result = (GetConsoleWindow() == nullptr);
    }

    cached = true;
    return result;
}

#else  // _WIN32

// Non-Windows stub: already provided inline in the header.

#endif

}  // namespace app
