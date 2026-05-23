#define OBLIVION
#define OBLIVION_VERSION_1_2_416 0x010201A0

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <mmsystem.h>
#include <psapi.h>
#include <process.h>

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <atomic>
#include <deque>
#include <string>
#include <mutex>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")

using UInt32 = uint32_t;

struct CommandInfo;

typedef UInt32 PluginHandle;

struct OBSEInterface
{
    UInt32 obseVersion;
    UInt32 oblivionVersion;
    UInt32 editorVersion;
    UInt32 isEditor;

    bool (*RegisterCommand)(CommandInfo* info);
    void (*SetOpcodeBase)(UInt32 opcode);
    void* (*QueryInterface)(UInt32 id);
    PluginHandle(*GetPluginHandle)(void);

    bool (*RegisterTypedCommand)(
        CommandInfo* info,
        UInt32 returnType);

    const char* (*GetOblivionDirectory)();

    bool (*GetPluginLoaded)(
        const char* pluginName);

    UInt32(*GetPluginVersion)(
        const char* pluginName);
};

struct PluginInfo
{
    enum
    {
        kInfoVersion = 3
    };

    UInt32 infoVersion;
    const char* name;
    UInt32 version;
};

const char* const MASTER_PLUGIN_NAME =
"Z_GodHandEngine";

const UInt32 MASTER_PLUGIN_VERSION = 21;

const char* const MASTER_LOG_PATH =
"Data\\OBSE\\Plugins\\Z_GodHandEngine.log";

constexpr DWORD kMaintenanceIntervalMS =
300000;

constexpr DWORD kLogFlushIntervalMS =
3000;

constexpr size_t kMaxLogLines =
21;

enum RuntimeMode
{
    MODE_LEGACY_STANDALONE = 0,
    MODE_ENGINE_FIX_COOPERATIVE,
    MODE_DISPLAY_COOPERATIVE,
    MODE_FULL_MODERN_STACK
};

struct RuntimeCapabilities
{
    bool hasAveSithis = false;
    bool hasEngineBugFixes = false;
    bool hasBlueEngineFixes = false;
    bool hasDisplayTweaks = false;

    bool hasDXVK = false;
    bool hasModernD3DHooking = false;

    bool hasMoreHeap = false;
    bool hasHeapManager = false;

    bool hasModernFramePacing = false;
    bool hasModernCrashFixes = false;
    bool hasModernHeapManagement = false;
};

static RuntimeCapabilities gCaps;

static RuntimeMode gRuntimeMode =
MODE_LEGACY_STANDALONE;

static HANDLE gLogFile =
INVALID_HANDLE_VALUE;

static HANDLE gWorkerThread =
nullptr;

static HANDLE gLoggerThread =
nullptr;

static HANDLE gStopEvent =
nullptr;

static HANDLE gLogEvent =
nullptr;

static std::atomic_bool gRunning =
false;

static bool gEnableLFH =
true;

static bool gEnableTimerResolution =
false;

static bool gEnablePriorityBoost =
false;

static bool gEnableWorkingSetTrim =
false;

static bool gEnableHeapCompaction =
false;

static bool gEnableTelemetry =
true;

static bool gEnableDiagnostics =
true;

static std::deque<std::string> gLogBuffer;

static std::mutex gLogMutex;

bool IsModuleLoaded(
    const char* moduleName)
{
    return (
        GetModuleHandleA(moduleName)
        != nullptr
        );
}

void OpenLog()
{
    gLogFile = CreateFileA(
        MASTER_LOG_PATH,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (gLogFile != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(
            gLogFile,
            0,
            nullptr,
            FILE_END
        );
    }
}

void CloseLog()
{
    if (gLogFile != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(gLogFile);

        CloseHandle(gLogFile);

        gLogFile =
            INVALID_HANDLE_VALUE;
    }
}

void PushLogLine(
    const char* text)
{
    std::lock_guard<std::mutex> lock(
        gLogMutex
    );

    gLogBuffer.push_back(text);

    while (
        gLogBuffer.size() >
        kMaxLogLines)
    {
        gLogBuffer.pop_front();
    }

    if (gLogEvent)
    {
        SetEvent(gLogEvent);
    }
}

void Log(
    const char* fmt,
    ...)
{
    if (gLogFile == INVALID_HANDLE_VALUE)
        return;

    char message[2048]{};

    va_list args;

    va_start(args, fmt);

    vsnprintf(
        message,
        sizeof(message),
        fmt,
        args
    );

    va_end(args);

    if (
        strstr(message, "DXVK") != nullptr
        )
    {
        return;
    }

    SYSTEMTIME st{};

    GetLocalTime(&st);

    char finalBuffer[2400]{};

    snprintf(
        finalBuffer,
        sizeof(finalBuffer),
        "[%02u:%02u:%02u] %s\r\n",
        st.wHour,
        st.wMinute,
        st.wSecond,
        message
    );

    PushLogLine(finalBuffer);
}

unsigned __stdcall LoggerThread(
    void*)
{
    HANDLE handles[2]
    {
        gStopEvent,
        gLogEvent
    };

    while (gRunning)
    {
        DWORD result =
            WaitForMultipleObjects(
                2,
                handles,
                FALSE,
                kLogFlushIntervalMS
            );

        if (result == WAIT_OBJECT_0)
            break;

        std::deque<std::string> localCopy;

        {
            std::lock_guard<std::mutex> lock(
                gLogMutex
            );

            localCopy = gLogBuffer;

            ResetEvent(gLogEvent);
        }

        if (gLogFile == INVALID_HANDLE_VALUE)
            continue;

        SetFilePointer(
            gLogFile,
            0,
            nullptr,
            FILE_BEGIN
        );

        SetEndOfFile(gLogFile);

        for (const auto& line : localCopy)
        {
            DWORD written = 0;

            WriteFile(
                gLogFile,
                line.c_str(),
                (DWORD)line.size(),
                &written,
                nullptr
            );
        }

        FlushFileBuffers(gLogFile);
    }

    return 0;
}

void StartLogger()
{
    gLogEvent =
        CreateEventA(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

    gLoggerThread =
        (HANDLE)_beginthreadex(
            nullptr,
            0,
            LoggerThread,
            nullptr,
            0,
            nullptr
        );
}

void StopLogger()
{
    if (gLogEvent)
    {
        SetEvent(gLogEvent);
    }

    if (gLoggerThread)
    {
        WaitForSingleObject(
            gLoggerThread,
            3000
        );

        CloseHandle(
            gLoggerThread
        );

        gLoggerThread =
            nullptr;
    }

    if (gLogEvent)
    {
        CloseHandle(
            gLogEvent
        );

        gLogEvent =
            nullptr;
    }
}

void RefreshGraphicsHooks()
{
    bool currentD3D9 =
        IsModuleLoaded("d3d9.dll");

    bool currentDXGI =
        IsModuleLoaded("dxgi.dll");

    if (
        currentD3D9 &&
        !gCaps.hasModernD3DHooking)
    {
        gCaps.hasModernD3DHooking = true;

        Log(
            "[GRAPHICS] D3D9_WRAPPER_DETECTED"
        );
    }

    if (
        currentDXGI &&
        !gCaps.hasDXVK)
    {
        gCaps.hasDXVK = true;
    }
}

void DetectPluginStack(
    const OBSEInterface* obse)
{
    if (!obse)
        return;

    gCaps.hasAveSithis =
        obse->GetPluginLoaded(
            "AveSithisEngineFixes"
        );

    gCaps.hasEngineBugFixes =
        obse->GetPluginLoaded(
            "EngineBugFixes"
        );

    gCaps.hasBlueEngineFixes =
        obse->GetPluginLoaded(
            "BA_EngineFixes"
        );

    gCaps.hasDisplayTweaks =
        obse->GetPluginLoaded(
            "oblivion_display_tweaks"
        );

    gCaps.hasMoreHeap =
        (
            obse->GetPluginLoaded(
                "MoreHeap"
            ) ||
            IsModuleLoaded(
                "MoreHeap.dll"
            )
            );

    gCaps.hasDXVK =
        (
            IsModuleLoaded("dxgi.dll") ||
            IsModuleLoaded("d3d11.dll")
            );

    gCaps.hasModernD3DHooking =
        (
            IsModuleLoaded("d3d9.dll") ||
            gCaps.hasDisplayTweaks
            );

    gCaps.hasModernCrashFixes =
        (
            gCaps.hasAveSithis ||
            gCaps.hasEngineBugFixes
            );

    gCaps.hasModernFramePacing =
        (
            gCaps.hasDisplayTweaks ||
            gCaps.hasDXVK
            );

    gCaps.hasModernHeapManagement =
        (
            gCaps.hasEngineBugFixes ||
            gCaps.hasMoreHeap
            );

    gCaps.hasHeapManager =
        (
            gCaps.hasMoreHeap ||
            gCaps.hasModernHeapManagement
            );

    Log(
        "[STACK] D3DHOOK=%d DISPLAY=%d",
        gCaps.hasModernD3DHooking,
        gCaps.hasDisplayTweaks
    );
}

void ApplyAdaptivePolicies()
{
    if (
        gCaps.hasModernCrashFixes &&
        gCaps.hasModernFramePacing)
    {
        gRuntimeMode =
            MODE_FULL_MODERN_STACK;
    }
    else if (
        gCaps.hasModernFramePacing)
    {
        gRuntimeMode =
            MODE_DISPLAY_COOPERATIVE;
    }
    else if (
        gCaps.hasModernCrashFixes)
    {
        gRuntimeMode =
            MODE_ENGINE_FIX_COOPERATIVE;
    }
    else
    {
        gRuntimeMode =
            MODE_LEGACY_STANDALONE;
    }

    switch (gRuntimeMode)
    {
    case MODE_LEGACY_STANDALONE:
    {
        gEnableLFH = true;
        gEnableTimerResolution = true;

        gEnablePriorityBoost = false;
        gEnableWorkingSetTrim = false;
        gEnableHeapCompaction = false;

        Log(
            "[MODE] LEGACY_STANDALONE"
        );

        break;
    }

    case MODE_ENGINE_FIX_COOPERATIVE:
    {
        gEnableLFH = true;
        gEnableTimerResolution = true;

        gEnablePriorityBoost = true;
        gEnableWorkingSetTrim = false;
        gEnableHeapCompaction = true;

        Log(
            "[MODE] ENGINE_FIX_COOPERATIVE"
        );

        break;
    }

    case MODE_DISPLAY_COOPERATIVE:
    {
        gEnableLFH = true;
        gEnableTimerResolution = true;

        gEnablePriorityBoost = true;
        gEnableWorkingSetTrim = true;
        gEnableHeapCompaction = true;

        Log(
            "[MODE] DISPLAY_COOPERATIVE"
        );

        break;
    }

    case MODE_FULL_MODERN_STACK:
    {
        gEnableLFH = true;
        gEnableTimerResolution = true;

        gEnablePriorityBoost = true;
        gEnableWorkingSetTrim = true;
        gEnableHeapCompaction = true;

        Log(
            "[MODE] FULL_MODERN_STACK"
        );

        break;
    }
    }

    if (gCaps.hasHeapManager)
    {
        gEnableLFH = false;

        Log(
            "[COMPAT] EXTERNAL_HEAP_MANAGER"
        );
    }
}

void EnableLFH()
{
    if (!gEnableLFH)
    {
        Log(
            "[LFH] DISABLED"
        );

        return;
    }

    DWORD heapCount =
        GetProcessHeaps(
            0,
            nullptr
        );

    if (!heapCount)
        return;

    std::vector<HANDLE> heaps(
        heapCount
    );

    heapCount =
        GetProcessHeaps(
            heapCount,
            heaps.data()
        );

    ULONG mode = 2;

    for (DWORD i = 0; i < heapCount; i++)
    {
        HeapSetInformation(
            heaps[i],
            HeapCompatibilityInformation,
            &mode,
            sizeof(mode)
        );
    }

    Log(
        "[LFH] ENABLED"
    );
}

void ApplyTimerResolution()
{
    if (!gEnableTimerResolution)
        return;

    timeBeginPeriod(1);

    Log(
        "[TIMER] 1MS"
    );
}

void RestoreTimerResolution()
{
    if (!gEnableTimerResolution)
        return;

    timeEndPeriod(1);

    Log(
        "[TIMER] RESTORED"
    );
}

void ApplyPriorityBoost()
{
    if (!gEnablePriorityBoost)
        return;

    SetPriorityClass(
        GetCurrentProcess(),
        ABOVE_NORMAL_PRIORITY_CLASS
    );

    Log(
        "[PRIORITY] ABOVE_NORMAL"
    );
}

void CompactHeaps()
{
    if (!gEnableHeapCompaction)
        return;

    DWORD heapCount =
        GetProcessHeaps(
            0,
            nullptr
        );

    if (!heapCount)
        return;

    std::vector<HANDLE> heaps(
        heapCount
    );

    heapCount =
        GetProcessHeaps(
            heapCount,
            heaps.data()
        );

    for (DWORD i = 0; i < heapCount; i++)
    {
        HeapCompact(
            heaps[i],
            0
        );
    }

    Log(
        "[HEAP] COMPACTED"
    );
}

void TrimWorkingSet()
{
    if (!gEnableWorkingSetTrim)
        return;

    SetProcessWorkingSetSize(
        GetCurrentProcess(),
        (SIZE_T)-1,
        (SIZE_T)-1
    );

    Log(
        "[MEMORY] WORKING_SET_TRIM"
    );
}

void LogMemoryStatus()
{
    PROCESS_MEMORY_COUNTERS pmc{};

    if (!GetProcessMemoryInfo(
        GetCurrentProcess(),
        &pmc,
        sizeof(pmc)))
    {
        return;
    }

    SIZE_T workingSetMB =
        pmc.WorkingSetSize /
        (1024 * 1024);

    SIZE_T privateMB =
        pmc.PagefileUsage /
        (1024 * 1024);

    Log(
        "[MEMORY] WS=%zuMB PRIVATE=%zuMB",
        workingSetMB,
        privateMB
    );
}

void CollectTelemetry()
{
    if (!gEnableTelemetry)
        return;

    SYSTEM_INFO si{};

    GetSystemInfo(&si);

    Log(
        "[CPU] CORES=%u",
        si.dwNumberOfProcessors
    );
}

unsigned __stdcall MaintenanceThread(
    void*)
{
    SetThreadPriority(
        GetCurrentThread(),
        THREAD_PRIORITY_BELOW_NORMAL
    );

    Log(
        "[THREAD] MAINTENANCE_STARTED"
    );

    while (gRunning)
    {
        DWORD result =
            WaitForSingleObject(
                gStopEvent,
                kMaintenanceIntervalMS
            );

        if (result != WAIT_TIMEOUT)
            break;

        RefreshGraphicsHooks();

        CompactHeaps();

        TrimWorkingSet();

        LogMemoryStatus();

        CollectTelemetry();

        Log(
            "[MAINTENANCE] COMPLETE"
        );
    }

    Log(
        "[THREAD] MAINTENANCE_STOPPED"
    );

    return 0;
}

void StartThreads()
{
    gStopEvent =
        CreateEventA(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

    gRunning = true;

    StartLogger();

    gWorkerThread =
        (HANDLE)_beginthreadex(
            nullptr,
            0,
            MaintenanceThread,
            nullptr,
            0,
            nullptr
        );
}

void StopThreads()
{
    gRunning = false;

    if (gStopEvent)
    {
        SetEvent(gStopEvent);
    }

    StopLogger();

    if (gWorkerThread)
    {
        WaitForSingleObject(
            gWorkerThread,
            3000
        );

        CloseHandle(
            gWorkerThread
        );

        gWorkerThread =
            nullptr;
    }

    if (gStopEvent)
    {
        CloseHandle(
            gStopEvent
        );

        gStopEvent =
            nullptr;
    }
}

bool InitializeRuntime(
    const OBSEInterface* obse)
{
    Log(
        "[RUNTIME] INITIALIZING"
    );

    DetectPluginStack(obse);

    ApplyAdaptivePolicies();

    EnableLFH();

    ApplyTimerResolution();

    ApplyPriorityBoost();

    CompactHeaps();

    TrimWorkingSet();

    LogMemoryStatus();

    CollectTelemetry();

    StartThreads();

    Log(
        "[RUNTIME] INITIALIZED"
    );

    return true;
}

void ShutdownRuntime()
{
    Log(
        "[RUNTIME] SHUTDOWN_BEGIN"
    );

    StopThreads();

    RestoreTimerResolution();

    Log(
        "[RUNTIME] SHUTDOWN_END"
    );

    CloseLog();
}

extern "C"
{

    __declspec(dllexport)
        bool OBSEPlugin_Query(
            const OBSEInterface* obse,
            PluginInfo* info)
    {
        OpenLog();

        if (!obse || !info)
        {
            return false;
        }

        info->infoVersion =
            PluginInfo::kInfoVersion;

        info->name =
            MASTER_PLUGIN_NAME;

        info->version =
            MASTER_PLUGIN_VERSION;

        if (obse->isEditor)
        {
            return false;
        }

        if (
            obse->oblivionVersion !=
            OBLIVION_VERSION_1_2_416)
        {
            return false;
        }

        Log(
            "[QUERY] SUCCESS"
        );

        return true;
    }

    __declspec(dllexport)
        bool OBSEPlugin_Load(
            const OBSEInterface* obse)
    {
        if (!obse)
        {
            return false;
        }

        InitializeRuntime(obse);

        Log(
            "[LOAD] SUCCESS"
        );

        return true;
    }

}

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(
            hModule
        );

        break;
    }

    case DLL_PROCESS_DETACH:
    {
        gRunning = false;

        ShutdownRuntime();

        break;
    }
    }

    return TRUE;
}