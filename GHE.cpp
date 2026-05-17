#define WIN32_LEAN_AND_MEAN
#define STRICT
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>

static bool g_IsFirstLogCall = true;

void LogEventSilently(const char* message) noexcept {
    try {
        std::ofstream logFile;
        if (g_IsFirstLogCall) {
            logFile.open("monitor_log.txt", std::ios::out | std::ios::trunc);
            g_IsFirstLogCall = false;
        }
        else {
            logFile.open("monitor_log.txt", std::ios::out | std::ios::app);
        }

        if (logFile.is_open()) {
            SYSTEMTIME st;
            ::GetLocalTime(&st);
            logFile << "[" << std::setw(4) << std::setfill('0') << st.wYear << "-"
                << std::setw(2) << std::setfill('0') << st.wMonth << "-"
                << std::setw(2) << std::setfill('0') << st.wDay << " "
                << std::setw(2) << std::setfill('0') << st.wHour << ":"
                << std::setw(2) << std::setfill('0') << st.wMinute << ":"
                << std::setw(2) << std::setfill('0') << st.wSecond << "] "
                << message << "\n";
        }
    }
    catch (...) {}
}

class CriticalResourceGuard {
private:
    HANDLE m_handle;
public:
    explicit CriticalResourceGuard(HANDLE h) noexcept : m_handle(h) {}
    ~CriticalResourceGuard() noexcept {
        if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_handle);
        }
    }
    [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }
    [[nodiscard]] bool IsValid() const noexcept { return m_handle && m_handle != INVALID_HANDLE_VALUE; }
    CriticalResourceGuard(const CriticalResourceGuard&) = delete;
    CriticalResourceGuard& operator=(const CriticalResourceGuard&) = delete;
};

bool IsSystemElevatedSecure() noexcept {
    BOOL bIsElevated = FALSE;
    HANDLE hToken = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        CriticalResourceGuard tokenGuard(hToken);
        TOKEN_ELEVATION elevation;
        DWORD dwSize = sizeof(elevation);
        if (::GetTokenInformation(tokenGuard.Get(), TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            bIsElevated = elevation.TokenIsElevated;
        }
    }
    return bIsElevated != 0;
}

[[nodiscard]] DWORD ScanActiveProcessesPerpetual(const wchar_t* targetName) noexcept {
    DWORD foundPid = 0;
    HANDLE snapshotRaw = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshotRaw == INVALID_HANDLE_VALUE) return 0;

    CriticalResourceGuard snapshotGuard(snapshotRaw);
    PROCESSENTRY32W processEntry;
    processEntry.dwSize = sizeof(processEntry);

    if (::Process32FirstW(snapshotGuard.Get(), &processEntry)) {
        do {
            if (_wcsicmp(processEntry.szExeFile, targetName) == 0) {
                if (processEntry.th32ProcessID != 0 && processEntry.cntThreads > 0) {
                    foundPid = processEntry.th32ProcessID;
                    break;
                }
            }
        } while (::Process32NextW(snapshotGuard.Get(), &processEntry));
    }
    return foundPid;
}

void ApplyMemorySafetyShield(DWORD processId) noexcept {
    HANDLE processRaw = ::OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, processId);
    if (!processRaw) return;
    CriticalResourceGuard processGuard(processRaw);

    HANDLE tokenRaw = nullptr;
    if (::OpenProcessToken(processGuard.Get(), TOKEN_WRITE, &tokenRaw)) {
        CriticalResourceGuard tokenGuard(tokenRaw);
        DWORD virtualizationFlags = 0;
        ::SetTokenInformation(tokenGuard.Get(), TokenVirtualizationAllowed, &virtualizationFlags, sizeof(virtualizationFlags));
    }

    LogEventSilently("[SUCCESS] Virtualization shield deployed.");
}

static bool InternalExecutePriorityAssignment(HANDLE processHandle, DWORD targetPriority) noexcept {
    __try {
        DWORD exitCode = 0;
        if (!::GetExitCodeProcess(processHandle, &exitCode) || exitCode != STILL_ACTIVE) {
            return false;
        }

        if (!::SetPriorityClass(processHandle, targetPriority)) {
            return false;
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ApplyCoexistentOptimization(DWORD processId, DWORD targetPriority) noexcept {
    HANDLE processRaw = ::OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processRaw) return false;

    CriticalResourceGuard processGuard(processRaw);
    return InternalExecutePriorityAssignment(processGuard.Get(), targetPriority);
}

static void InternalProtectedExecutionLoopRaw(bool& isObseOptimized, DWORD& lastActivePid) noexcept {
    __try {
        DWORD pidOblivion = ScanActiveProcessesPerpetual(L"Oblivion.exe");

        if (pidOblivion == 0) {
            if (lastActivePid != 0) {
                LogEventSilently("[INFO] Oblivion.exe terminated. Standby mode active.");
            }
            isObseOptimized = false;
            lastActivePid = 0;

            DWORD pidObse = ScanActiveProcessesPerpetual(L"obse_loader.exe");
            if (pidObse != 0 && !isObseOptimized) {
                if (ApplyCoexistentOptimization(pidObse, ABOVE_NORMAL_PRIORITY_CLASS)) {
                    ApplyMemorySafetyShield(pidObse);
                    LogEventSilently("[SUCCESS] Boot priority coupled to obse_loader.exe.");
                    isObseOptimized = true;
                }
            }
            ::Sleep(2000);
        }
        else {
            if (pidOblivion != lastActivePid) {
                LogEventSilently("[INFO] Oblivion.exe detected. Tracking active.");
                lastActivePid = pidOblivion;

                ::Sleep(12000);
                ApplyCoexistentOptimization(pidOblivion, ABOVE_NORMAL_PRIORITY_CLASS);
                ApplyMemorySafetyShield(pidOblivion);
            }
            else {
                ::Sleep(10000);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        LogEventSilently("[ERROR] Loop exception intercepted. Recovery engaged.");
        ::Sleep(5000);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    ::SetPriorityClass(::GetCurrentProcess(), IDLE_PRIORITY_CLASS);

    LogEventSilently("[INIT] External optimization shield active.");

    if (!IsSystemElevatedSecure()) {
        LogEventSilently("[CRITICAL_ERROR] Administrative privileges required. Terminating.");
        return 0;
    }

    bool isObseOptimized = false;
    DWORD lastActivePid = 0;

    while (true) {
        InternalProtectedExecutionLoopRaw(isObseOptimized, lastActivePid);
    }

    return 0;
}
