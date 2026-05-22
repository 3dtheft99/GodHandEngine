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

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <atomic>

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
120000;

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

    bool hasMoreHeap = false;
    bool hasHeapManager = false;

    bool hasModernFramePacing = false;
    bool hasModernCrashFixes = false;
    bool hasModernHeapManagement = false;
    bool hasModernD3DHooking = false;
};

static RuntimeCapabilities gCaps;

static RuntimeMode gRuntimeMode =
MODE_LEGACY_STANDALONE;

static HANDLE gLogFile =
INVALID_HANDLE_VALUE;

static HANDLE gWorkerThread =
nullptr;

static HANDLE gStopEvent =
nullptr;

static std::atomic_bool gRunning =
false;

static bool gEnableLFH =
true;

static bool gEnableTimerResolution =
false;

static bool gEnablePriorityBoost =
false;

static bool gEnableIdealProcessor =
false;

static bool gEnablePowerThrottlingPatch =
true;

static bool gEnableExceptionFilter =
true;

static bool gEnableTelemetry =
true;

static bool gEnableDiagnostics =
true;

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

    DWORD written = 0;

    WriteFile(
        gLogFile,
        finalBuffer,
        (DWORD)strlen(finalBuffer),
        &written,
        nullptr
    );

    FlushFileBuffers(gLogFile);
}

void OpenLog()
{
    gLogFile = CreateFileA(
        MASTER_LOG_PATH,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

void CloseLog()
{
    if (gLogFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(gLogFile);

        gLogFile =
            INVALID_HANDLE_VALUE;
    }
}

bool IsModuleLoaded(
    const char* moduleName)
{
    return (
        GetModuleHandleA(moduleName)
        != nullptr
        );
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

    gCaps.hasModernD3DHooking =
        (
            gCaps.hasDisplayTweaks
            );

    gCaps.hasHeapManager =
        (
            gCaps.hasMoreHeap ||
            gCaps.hasModernHeapManagement
            );

    Log(
        "[STACK] AveSithis=%d EngineBugFixes=%d BA_EngineFixes=%d DisplayTweaks=%d DXVK=%d MoreHeap=%d",
        gCaps.hasAveSithis,
        gCaps.hasEngineBugFixes,
        gCaps.hasBlueEngineFixes,
        gCaps.hasDisplayTweaks,
        gCaps.hasDXVK,
        gCaps.hasMoreHeap
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
        gEnableIdealProcessor = false;
        gEnablePowerThrottlingPatch = true;
        gEnableExceptionFilter = true;

        Log(
            "[MODE] LEGACY_STANDALONE"
        );

        break;
    }

    case MODE_ENGINE_FIX_COOPERATIVE:
    {
        gEnableLFH = true;
        gEnableTimerResolution = false;
        gEnablePriorityBoost = false;
        gEnableIdealProcessor = false;
        gEnablePowerThrottlingPatch = true;
        gEnableExceptionFilter = false;

        Log(
            "[MODE] ENGINE_FIX_COOPERATIVE"
        );

        break;
    }

    case MODE_DISPLAY_COOPERATIVE:
    {
        gEnableLFH = true;
        gEnableTimerResolution = false;
        gEnablePriorityBoost = false;
        gEnableIdealProcessor = false;
        gEnablePowerThrottlingPatch = true;
        gEnableExceptionFilter = true;

        Log(
            "[MODE] DISPLAY_COOPERATIVE"
        );

        break;
    }

    case MODE_FULL_MODERN_STACK:
    {
        gEnableLFH = false;
        gEnableTimerResolution = false;
        gEnablePriorityBoost = false;
        gEnableIdealProcessor = false;
        gEnablePowerThrottlingPatch = false;
        gEnableExceptionFilter = false;

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
            "[COMPAT] External heap manager detected - LFH disabled"
        );
    }
}

void EnableLFH()
{
    if (!gEnableLFH)
    {
        Log(
            "[LFH] DISABLED_BY_POLICY"
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
    {
        Log(
            "[TIMER] DISABLED_BY_POLICY"
        );

        return;
    }

    timeBeginPeriod(1);

    Log(
        "[TIMER] 1MS_ENABLED"
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

typedef BOOL(WINAPI* SetProcessInformationFn)(
    HANDLE,
    PROCESS_INFORMATION_CLASS,
    LPVOID,
    DWORD
    );

void DisablePowerThrottling()
{
    if (!gEnablePowerThrottlingPatch)
    {
        Log(
            "[POWER] POLICY_DISABLED"
        );

        return;
    }

    auto kernel =
        GetModuleHandleA(
            "kernel32.dll"
        );

    if (!kernel)
        return;

    auto setProcessInformation =
        reinterpret_cast<
        SetProcessInformationFn>(
            GetProcAddress(
                kernel,
                "SetProcessInformation"
            )
            );

    if (!setProcessInformation)
        return;

    PROCESS_POWER_THROTTLING_STATE state{};

    state.Version =
        PROCESS_POWER_THROTTLING_CURRENT_VERSION;

    state.ControlMask =
        PROCESS_POWER_THROTTLING_EXECUTION_SPEED;

    state.StateMask = 0;

    setProcessInformation(
        GetCurrentProcess(),
        ProcessPowerThrottling,
        &state,
        sizeof(state)
    );

    Log(
        "[POWER] THROTTLING_DISABLED"
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

    if (privateMB > 1700)
    {
        Log(
            "[WARNING] HIGH_MEMORY_PRESSURE"
        );
    }
}

void RunDiagnostics()
{
    if (!gEnableDiagnostics)
        return;

    if (
        gCaps.hasDisplayTweaks &&
        gCaps.hasDXVK)
    {
        Log(
            "[DIAGNOSTIC] DisplayTweaks + DXVK detected"
        );
    }

    if (
        gCaps.hasHeapManager)
    {
        Log(
            "[DIAGNOSTIC] External heap manager active"
        );
    }

    if (
        gCaps.hasModernFramePacing &&
        gEnableTimerResolution)
    {
        Log(
            "[DIAGNOSTIC_WARNING] Legacy timer active with modern frame pacing"
        );
    }

    if (
        gCaps.hasModernCrashFixes &&
        gEnableExceptionFilter)
    {
        Log(
            "[DIAGNOSTIC_WARNING] Exception filter active with engine fixes"
        );
    }

    if (
        gCaps.hasModernD3DHooking)
    {
        Log(
            "[DIAGNOSTIC] Modern D3D hook layer active"
        );
    }
}

void CollectTelemetry()
{
    if (!gEnableTelemetry)
        return;

    SYSTEM_INFO si{};

    GetSystemInfo(&si);

    Log(
        "[TELEMETRY] CPU_CORES=%u",
        si.dwNumberOfProcessors
    );

    MEMORYSTATUSEX mem{};

    mem.dwLength =
        sizeof(mem);

    if (
        GlobalMemoryStatusEx(
            &mem))
    {
        DWORDLONG totalMB =
            mem.ullTotalPhys /
            (1024ull * 1024ull);

        DWORDLONG availMB =
            mem.ullAvailPhys /
            (1024ull * 1024ull);

        Log(
            "[TELEMETRY] RAM_TOTAL=%lluMB RAM_FREE=%lluMB",
            totalMB,
            availMB
        );
    }
}

LONG WINAPI RuntimeExceptionHandler(
    EXCEPTION_POINTERS* info)
{
    if (!info)
        return EXCEPTION_CONTINUE_SEARCH;

    Log(
        "[EXCEPTION] CODE=0x%08X",
        info->ExceptionRecord->ExceptionCode
    );

    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI MaintenanceThread(
    LPVOID)
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

        LogMemoryStatus();

        CollectTelemetry();

        RunDiagnostics();

        Log(
            "[MAINTENANCE] COMPLETE"
        );
    }

    Log(
        "[THREAD] MAINTENANCE_STOPPED"
    );

    return 0;
}

void StartMaintenance()
{
    gStopEvent =
        CreateEventA(
            nullptr,
            TRUE,
            FALSE,
            nullptr
        );

    if (!gStopEvent)
        return;

    gRunning = true;

    gWorkerThread =
        CreateThread(
            nullptr,
            0,
            MaintenanceThread,
            nullptr,
            0,
            nullptr
        );
}

void StopMaintenance()
{
    gRunning = false;

    if (gStopEvent)
    {
        SetEvent(gStopEvent);
    }

    if (gWorkerThread)
    {
        WaitForSingleObject(
            gWorkerThread,
            2000
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

    if (gEnableExceptionFilter)
    {
        SetUnhandledExceptionFilter(
            RuntimeExceptionHandler
        );

        Log(
            "[RUNTIME] EXCEPTION_FILTER_ENABLED"
        );
    }

    EnableLFH();

    ApplyTimerResolution();

    DisablePowerThrottling();

    LogMemoryStatus();

    CollectTelemetry();

    RunDiagnostics();

    StartMaintenance();

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

    StopMaintenance();

    RestoreTimerResolution();

    Log(
        "[RUNTIME] SHUTDOWN_END"
    );
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
    HMODULE,
    DWORD reason,
    LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_DETACH:
    {
        ShutdownRuntime();

        CloseLog();

        break;
    }
    }

    return TRUE;
}