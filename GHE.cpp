#define WIN32_LEAN_AND_MEAN
#define STRICT

#include <windows.h>
#include <tlhelp32.h>

#include <fstream>
#include <iomanip>

static bool g_IsFirstLogCall = true;

void LogEventSilently(const char* message) noexcept
{
    try
    {
        std::ofstream logFile;

        if (g_IsFirstLogCall)
        {
            logFile.open(
                "monitor_log.txt",
                std::ios::out | std::ios::trunc
            );

            g_IsFirstLogCall = false;
        }
        else
        {
            logFile.open(
                "monitor_log.txt",
                std::ios::out | std::ios::app
            );
        }

        if (logFile.is_open())
        {
            SYSTEMTIME st;

            ::GetLocalTime(&st);

            logFile
                << "["
                << std::setw(4) << std::setfill('0') << st.wYear
                << "-"
                << std::setw(2) << std::setfill('0') << st.wMonth
                << "-"
                << std::setw(2) << std::setfill('0') << st.wDay
                << " "
                << std::setw(2) << std::setfill('0') << st.wHour
                << ":"
                << std::setw(2) << std::setfill('0') << st.wMinute
                << ":"
                << std::setw(2) << std::setfill('0') << st.wSecond
                << "] "
                << message
                << "\n";
        }
    }
    catch (...)
    {
    }
}

class CriticalResourceGuard
{
private:

    HANDLE m_Handle;

public:

    explicit CriticalResourceGuard(
        HANDLE handle) noexcept
        :
        m_Handle(handle)
    {
    }

    ~CriticalResourceGuard() noexcept
    {
        if (
            m_Handle &&
            m_Handle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(m_Handle);
        }
    }

    HANDLE Get() const noexcept
    {
        return m_Handle;
    }

    bool IsValid() const noexcept
    {
        return (
            m_Handle &&
            m_Handle != INVALID_HANDLE_VALUE
            );
    }

    CriticalResourceGuard(
        const CriticalResourceGuard&) = delete;

    CriticalResourceGuard& operator=(
        const CriticalResourceGuard&) = delete;
};

bool IsSystemElevatedSecure() noexcept
{
    BOOL elevated = FALSE;

    HANDLE token = nullptr;

    if (::OpenProcessToken(
        ::GetCurrentProcess(),
        TOKEN_QUERY,
        &token))
    {
        CriticalResourceGuard guard(token);

        TOKEN_ELEVATION elevation;

        DWORD size =
            sizeof(elevation);

        if (::GetTokenInformation(
            guard.Get(),
            TokenElevation,
            &elevation,
            sizeof(elevation),
            &size))
        {
            elevated =
                elevation.TokenIsElevated;
        }
    }

    return elevated != FALSE;
}

DWORD ScanActiveProcessesPerpetual(
    const wchar_t* targetName) noexcept
{
    HANDLE snapshot =
        ::CreateToolhelp32Snapshot(
            TH32CS_SNAPPROCESS,
            0
        );

    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    CriticalResourceGuard guard(snapshot);

    PROCESSENTRY32W entry;

    entry.dwSize =
        sizeof(entry);

    if (!::Process32FirstW(
        guard.Get(),
        &entry))
    {
        return 0;
    }

    do
    {
        if (_wcsicmp(
            entry.szExeFile,
            targetName) == 0)
        {
            if (
                entry.th32ProcessID &&
                entry.cntThreads)
            {
                return entry.th32ProcessID;
            }
        }

    } while (::Process32NextW(
        guard.Get(),
        &entry));

    return 0;
}

bool ApplyCoexistentOptimization(
    DWORD processId,
    DWORD priority) noexcept
{
    HANDLE process =
        ::OpenProcess(
            PROCESS_SET_INFORMATION |
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId
        );

    if (!process)
        return false;

    CriticalResourceGuard guard(process);

    DWORD exitCode = 0;

    if (
        !::GetExitCodeProcess(
            guard.Get(),
            &exitCode) ||
        exitCode != STILL_ACTIVE)
    {
        return false;
    }

    if (!::SetPriorityClass(
        guard.Get(),
        priority))
    {
        return false;
    }

    ::SetProcessPriorityBoost(
        guard.Get(),
        FALSE
    );

    return true;
}

void InternalProtectedExecutionLoopRaw(
    bool& isObseOptimized,
    DWORD& lastActivePid) noexcept
{
    __try
    {
        DWORD oblivionPid =
            ScanActiveProcessesPerpetual(
                L"Oblivion.exe"
            );

        if (!oblivionPid)
        {
            if (lastActivePid)
            {
                LogEventSilently(
                    "[INFO] Oblivion.exe terminated."
                );
            }

            lastActivePid = 0;
            isObseOptimized = false;

            DWORD obsePid =
                ScanActiveProcessesPerpetual(
                    L"obse_loader.exe"
                );

            if (
                obsePid &&
                !isObseOptimized)
            {
                if (ApplyCoexistentOptimization(
                    obsePid,
                    ABOVE_NORMAL_PRIORITY_CLASS))
                {
                    LogEventSilently(
                        "[SUCCESS] obse_loader.exe optimized."
                    );

                    isObseOptimized = true;
                }
            }

            ::Sleep(4000);
        }
        else
        {
            if (oblivionPid != lastActivePid)
            {
                lastActivePid =
                    oblivionPid;

                LogEventSilently(
                    "[INFO] Oblivion.exe detected."
                );

                ::Sleep(8000);

                ApplyCoexistentOptimization(
                    oblivionPid,
                    ABOVE_NORMAL_PRIORITY_CLASS
                );

                LogEventSilently(
                    "[SUCCESS] Oblivion.exe optimized."
                );
            }
            else
            {
                ::Sleep(15000);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LogEventSilently(
            "[ERROR] Runtime exception intercepted."
        );

        ::Sleep(5000);
    }
}

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    LPWSTR,
    int)
{
    ::SetPriorityClass(
        ::GetCurrentProcess(),
        IDLE_PRIORITY_CLASS
    );

    LogEventSilently(
        "[INIT] External optimization runtime active."
    );

    if (!IsSystemElevatedSecure())
    {
        LogEventSilently(
            "[CRITICAL_ERROR] Administrator privileges required."
        );

        return 0;
    }

    bool isObseOptimized = false;

    DWORD lastActivePid = 0;

    while (true)
    {
        InternalProtectedExecutionLoopRaw(
            isObseOptimized,
            lastActivePid
        );
    }

    return 0;
}
