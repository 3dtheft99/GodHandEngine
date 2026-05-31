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

typedef LONG(WINAPI* NtSetTimerResolutionFn)(
    ULONG,
    BOOLEAN,
    PULONG
    );

typedef LONG(WINAPI* NtQueryTimerResolutionFn)(
    PULONG,
    PULONG,
    PULONG
    );

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

constexpr ULONG kDesiredTimerResolution100ns =
5000;

constexpr size_t kMaxLogQueue =
1024;

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

static std::atomic_bool gShutdownStarted =
false;

static bool gEnableLFH =
true;

static bool gEnableTimerResolution =
true;

static bool gEnablePriorityBoost =
false;

static NtSetTimerResolutionFn gNtSetTimerResolution =
nullptr;

static NtQueryTimerResolutionFn gNtQueryTimerResolution =
nullptr;

static bool gNtTimerActive =
false;

static ULONG gAppliedTimerResolution =
0;

static UINT gWinMMPeriod =
0;

static HANDLE gMaintenanceTimer =
nullptr;

static bool gEnableDynamicSleepGranularity =
true;

static bool gEnableBackgroundMode =
true;

static DWORD gLastMaintenanceTick =
0;

static std::deque<std::string> gLogBuffer;

static std::mutex gLogMutex;

void Log(
    const char* fmt,
    ...
);

bool IsModuleLoaded(
    const char* moduleName);

void DetectLateRuntimeModules()
;

const char* RuntimeModeToString();

void OpenLog();

void PushLogLine(
    const char* text);

bool IsModuleLoaded(
    const char* moduleName)
{
    return (
        GetModuleHandleA(moduleName)
        != nullptr
        );
}

void DetectLateRuntimeModules()
{
    bool lateDXVK =
        (
            IsModuleLoaded("dxgi.dll") ||
            IsModuleLoaded("d3d11.dll") ||
            IsModuleLoaded("dxvk.dll") ||
            IsModuleLoaded("d3d9_dxvk.dll") ||
            IsModuleLoaded("d3d11_dxvk.dll")
            );

    if (
        lateDXVK &&
        !gCaps.hasDXVK)
    {
        gCaps.hasDXVK =
            true;

        Log(
            "DXVK late-detected"
        );
    }

    bool lateMoreHeap =
        (
            IsModuleLoaded("MoreHeap.dll") ||
            IsModuleLoaded("moreheap.dll")
            );

    if (
        lateMoreHeap &&
        !gCaps.hasMoreHeap)
    {
        gCaps.hasMoreHeap =
            true;

        Log(
            "MoreHeap late-detected"
        );
    }
}

const char* RuntimeModeToString()
{
    switch (gRuntimeMode)
    {
    case MODE_FULL_MODERN_STACK:
        return "FULL_MODERN_STACK";

    case MODE_DISPLAY_COOPERATIVE:
        return "DISPLAY_COOPERATIVE";

    case MODE_ENGINE_FIX_COOPERATIVE:
        return "ENGINE_FIX_COOPERATIVE";

    default:
        return "LEGACY_STANDALONE";
    }
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

void PushLogLine(
    const char* text)
{
    std::lock_guard<std::mutex> lock(
        gLogMutex
    );

    if (gLogBuffer.size() >= kMaxLogQueue)
    {
        gLogBuffer.pop_front();
    }

    gLogBuffer.push_back(text);

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

            localCopy.swap(gLogBuffer);

            ResetEvent(gLogEvent);
        }

        if (
            gLogFile ==
            INVALID_HANDLE_VALUE)
        {
            continue;
        }

        SetFilePointer(
            gLogFile,
            0,
            nullptr,
            FILE_END
        );

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

void InitializeNTTimerAPI()
{
    HMODULE ntdll =
        GetModuleHandleA(
            "ntdll.dll"
        );

    if (!ntdll)
        return;

    gNtSetTimerResolution =
        reinterpret_cast<NtSetTimerResolutionFn>(
            GetProcAddress(
                ntdll,
                "NtSetTimerResolution"
            )
            );

    gNtQueryTimerResolution =
        reinterpret_cast<NtQueryTimerResolutionFn>(
            GetProcAddress(
                ntdll,
                "NtQueryTimerResolution"
            )
            );
}

void ApplyTimerResolution()
{
    if (!gEnableTimerResolution)
        return;

    if (gNtTimerActive || gWinMMPeriod)
        return;

    InitializeNTTimerAPI();

    if (
        gCaps.hasDXVK ||
        gCaps.hasDisplayTweaks)
    {
        Log(
            "Modern frame pacing detected"
        );

        return;
    }

    if (
        gNtSetTimerResolution &&
        gNtQueryTimerResolution)
    {
        ULONG minimum = 0;
        ULONG maximum = 0;
        ULONG current = 0;

        if (
            gNtQueryTimerResolution(
                &minimum,
                &maximum,
                &current
            ) >= 0)
        {
            ULONG desired =
                kDesiredTimerResolution100ns;

            if (desired < minimum)
            {
                desired = minimum;
            }

            if (desired > maximum)
            {
                desired = maximum;
            }

            if (
                gNtSetTimerResolution(
                    desired,
                    TRUE,
                    &gAppliedTimerResolution
                ) >= 0)
            {
                gNtTimerActive = true;

                Log(
                    "NT timer resolution applied"
                );

                return;
            }
        }
    }

    TIMECAPS tc{};

    if (
        timeGetDevCaps(
            &tc,
            sizeof(tc)
        ) == TIMERR_NOERROR)
    {
        UINT desired =
            min(
                max(
                    tc.wPeriodMin,
                    1u
                ),
                tc.wPeriodMax
            );

        if (
            timeBeginPeriod(
                desired
            ) == TIMERR_NOERROR)
        {
            gWinMMPeriod =
                desired;

            Log(
                "WinMM timer resolution applied"
            );
        }
    }
}

void RestoreTimerResolution()
{
    if (
        gNtTimerActive &&
        gNtSetTimerResolution)
    {
        ULONG current = 0;

        gNtSetTimerResolution(
            gAppliedTimerResolution,
            FALSE,
            &current
        );

        gNtTimerActive =
            false;
    }

    if (gWinMMPeriod)
    {
        timeEndPeriod(
            gWinMMPeriod
        );

        gWinMMPeriod =
            0;
    }
}

void OptimizeScheduler()
{
    HANDLE process =
        GetCurrentProcess();

    SetPriorityClass(
        process,
        ABOVE_NORMAL_PRIORITY_CLASS
    );

    if (gEnablePriorityBoost)
    {
        SetProcessPriorityBoost(
            process,
            FALSE
        );
    }

    Log(
        "Scheduler optimized"
    );
}

void EnableLFH()
{
    if (!gEnableLFH)
        return;

    ULONG heapInfo = 2;

    HANDLE heaps[128]{};

    DWORD count =
        GetProcessHeaps(
            128,
            heaps
        );

    for (DWORD i = 0; i < count; ++i)
    {
        HeapSetInformation(
            heaps[i],
            HeapCompatibilityInformation,
            &heapInfo,
            sizeof(heapInfo)
        );
    }

    Log(
        "LFH enabled"
    );
}

void DetectPluginStack(
    OBSEInterface* obse)
{
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
            "BlueEngineFixes"
        );

    gCaps.hasDisplayTweaks =       
            obse->GetPluginLoaded(
                "oblivion_display_tweaks"
        );

    gCaps.hasDXVK =
        (
            IsModuleLoaded("dxgi.dll") ||
            IsModuleLoaded("d3d11.dll") ||
            IsModuleLoaded("dxvk.dll") ||
            IsModuleLoaded("d3d9_dxvk.dll") ||
            IsModuleLoaded("d3d11_dxvk.dll")       
            );

    gCaps.hasMoreHeap =
        (
            obse->GetPluginLoaded(
                "MoreHeap"
            ) ||
            IsModuleLoaded(
                "MoreHeap.dll"
            ) ||
            IsModuleLoaded(
                "moreheap.dll"
            )
            );

    gCaps.hasModernD3DHooking =
        (
            gCaps.hasDXVK ||
            gCaps.hasDisplayTweaks
            );

    gCaps.hasModernFramePacing =
        (
            gCaps.hasDXVK ||
            gCaps.hasDisplayTweaks
            );

    gCaps.hasModernCrashFixes =
        (
            gCaps.hasAveSithis ||
            gCaps.hasEngineBugFixes ||
            gCaps.hasBlueEngineFixes
            );

    gCaps.hasModernHeapManagement =
        (
            gCaps.hasMoreHeap ||
            gCaps.hasEngineBugFixes
            );

    if (
        gCaps.hasModernFramePacing &&
        gCaps.hasModernCrashFixes)
    {
        gRuntimeMode =
            MODE_FULL_MODERN_STACK;
    }
    else if (
        gCaps.hasDisplayTweaks)
    {
        gRuntimeMode =
            MODE_DISPLAY_COOPERATIVE;
    }
    else if (
        gCaps.hasEngineBugFixes)
    {
        gRuntimeMode =
            MODE_ENGINE_FIX_COOPERATIVE;
    }
    else
    {
        gRuntimeMode =
            MODE_LEGACY_STANDALONE;
    }

    Log(
        "Runtime mode: %s",
        RuntimeModeToString()
    );
}


unsigned __stdcall MaintenanceThread(
    void*)
{
    OptimizeScheduler();

    Sleep(3000);

    DetectLateRuntimeModules();

    while (gRunning)
    {
        DWORD now =
            GetTickCount();

        if (
            now - gLastMaintenanceTick
            >= kMaintenanceIntervalMS)
        {
            gLastMaintenanceTick =
                now;

            if (
                gEnableBackgroundMode)
            {
                SetThreadPriority(
                    GetCurrentThread(),
                    THREAD_PRIORITY_ABOVE_NORMAL
                );
            }

            if (
                !gCaps.hasModernHeapManagement)
            {
                EnableLFH();
            }

            if (
                !gCaps.hasModernFramePacing)
            {
                ApplyTimerResolution();
            }
        }

        if (
            WaitForSingleObject(
                gStopEvent,
                1000
            ) == WAIT_OBJECT_0)
        {
            break;
        }
    }

    return 0;
}

void ShutdownRuntime()
{
    if (
        gShutdownStarted.exchange(
            true
        ))
    {
        return;
    }

    gRunning =
        false;

    if (gStopEvent)
    {
        SetEvent(
            gStopEvent
        );
    }

    if (gWorkerThread)
    {
        WaitForSingleObject(
            gWorkerThread,
            5000
        );

        CloseHandle(
            gWorkerThread
        );

        gWorkerThread =
            nullptr;
    }

    if (gLoggerThread)
    {
        WaitForSingleObject(
            gLoggerThread,
            5000
        );

        CloseHandle(
            gLoggerThread
        );

        gLoggerThread =
            nullptr;
    }

    {
        std::deque<std::string> remaining;

        {
            std::lock_guard<std::mutex> lock(
                gLogMutex
            );

            remaining.swap(
                gLogBuffer
            );
        }

        if (
            gLogFile !=
            INVALID_HANDLE_VALUE)
        {
            for (const auto& line : remaining)
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

            FlushFileBuffers(
                gLogFile
            );
        }
    }

    RestoreTimerResolution();

    if (
        gLogFile !=
        INVALID_HANDLE_VALUE)
    {
        CloseHandle(
            gLogFile
        );

        gLogFile =
            INVALID_HANDLE_VALUE;
    }

    if (gStopEvent)
    {
        CloseHandle(
            gStopEvent
        );

        gStopEvent =
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

extern "C"
{

    __declspec(dllexport)
        bool OBSEPlugin_Query(
            const OBSEInterface* obse,
            PluginInfo* info)
    {
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

        return true;
    }

    __declspec(dllexport)
        bool OBSEPlugin_Load(
            OBSEInterface* obse)
    {
        OpenLog();

        Log(
            "Loading %s",
            MASTER_PLUGIN_NAME
        );

        DetectPluginStack(
            obse
        );

        gStopEvent =
            CreateEventA(
                nullptr,
                TRUE,
                FALSE,
                nullptr
            );

        StartLogger();

        gRunning =
            true;

        gWorkerThread =
            (HANDLE)_beginthreadex(
                nullptr,
                0,
                MaintenanceThread,
                nullptr,
                0,
                nullptr
            );

        Log(
            "Plugin loaded successfully"
        );

        return true;
    }

    BOOL APIENTRY DllMain(
        HMODULE hModule,
        DWORD reason,
        LPVOID)
    {
        if (
            reason ==
            DLL_PROCESS_ATTACH)
        {
            DisableThreadLibraryCalls(
                hModule
            );
        }

        return TRUE;
    }

}
