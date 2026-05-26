#define OBLIVION
#define OBLIVION_VERSION_1_2_416 0x010201A0

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <winternl.h>
#include <d3d9.h>
#include <psapi.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <atomic>
#include <mutex>
#include <string>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "winmm.lib")

typedef NTSTATUS(NTAPI* NtSetTimerResolution_t)(
    ULONG DesiredResolution,
    BOOLEAN SetResolution,
    PULONG CurrentResolution
    );

typedef NTSTATUS(NTAPI* NtQueryTimerResolution_t)(
    PULONG MinimumResolution,
    PULONG MaximumResolution,
    PULONG CurrentResolution
    );

enum RuntimeStack
{
    LEGACY_STACK,
    MODERN_STACK,
    FULL_MODERN_STACK,
    DXVK_STACK,
    ENB_STACK,
    LOW_END_STACK,
    STANDALONE_STACK
};

struct RuntimeCaps
{
    bool hasDXVK = false;
    bool hasENB = false;
    bool hasReShade = false;
    bool hasVulkan = false;
    bool lowEndCPU = false;
    bool lowMemory = false;
    bool standaloneMode = false;
};

struct RuntimePolicies
{
    bool enableLFH = true;
    bool enableTimerResolution = true;
    bool enableDynamicTimer = true;
    bool enableHeapCompaction = true;
    bool enableWorkingSetTrim = true;
    bool enablePriorityBoost = true;
    bool enableResetHook = true;
    bool enableMaintenanceThread = true;
    bool enableCrashHandler = true;
    bool enableWinMMFallback = true;
};

static RuntimeCaps gCaps;
static RuntimePolicies gPolicies;

static RuntimeStack gStack = FULL_MODERN_STACK;

static HMODULE gModule = nullptr;

static HANDLE gMaintenanceThread = nullptr;

static std::atomic<bool> gRunning = false;

static FILE* gLog = nullptr;

static std::mutex gLogMutex;

static NtSetTimerResolution_t pNtSetTimerResolution = nullptr;
static NtQueryTimerResolution_t pNtQueryTimerResolution = nullptr;

static ULONG gCurrentTimerResolution = 0;
static ULONG gMinimumResolution = 0;
static ULONG gMaximumResolution = 0;

static IDirect3D9* gD3D = nullptr;

bool IsModuleLoaded(const wchar_t* module)
{
    return GetModuleHandleW(module) != nullptr;
}

void Log(const char* text)
{
    std::lock_guard<std::mutex> lock(gLogMutex);

    if (!gLog)
        return;

    SYSTEMTIME st{};

    GetLocalTime(&st);

    fprintf(
        gLog,
        "[%02d:%02d:%02d] %s\n",
        st.wHour,
        st.wMinute,
        st.wSecond,
        text
    );

    fflush(gLog);
}

void DetectEnvironment()
{
    gCaps.hasDXVK =
        IsModuleLoaded(L"dxgi.dll") ||
        IsModuleLoaded(L"d3d11.dll") ||
        IsModuleLoaded(L"vulkan-1.dll");

    gCaps.hasENB =
        IsModuleLoaded(L"enbseries.dll") ||
        IsModuleLoaded(L"enbhelper.dll");

    gCaps.hasReShade =
        IsModuleLoaded(L"reshade.dll") ||
        IsModuleLoaded(L"ReShade64.dll");

    gCaps.hasVulkan =
        IsModuleLoaded(L"vulkan-1.dll");

    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);

    GlobalMemoryStatusEx(&mem);

    gCaps.lowEndCPU =
        si.dwNumberOfProcessors <= 4;

    gCaps.lowMemory =
        (mem.ullTotalPhys / (1024ull * 1024ull * 1024ull)) <= 8;

    wchar_t path[MAX_PATH]{};

    GetModuleFileNameW(
        nullptr,
        path,
        MAX_PATH
    );

    std::wstring exe(path);

    if (exe.find(L"Oblivion") != std::wstring::npos)
    {
        gCaps.standaloneMode = true;
    }
}

void SelectRuntimeStack()
{
    if (gCaps.hasENB)
    {
        gStack = ENB_STACK;
        return;
    }

    if (gCaps.hasDXVK)
    {
        gStack = DXVK_STACK;
        return;
    }

    if (gCaps.lowEndCPU || gCaps.lowMemory)
    {
        gStack = LOW_END_STACK;
        return;
    }

    if (gCaps.standaloneMode)
    {
        gStack = STANDALONE_STACK;
        return;
    }

    gStack = FULL_MODERN_STACK;
}

void EnableLFH()
{
    HANDLE heap = GetProcessHeap();

    if (!heap)
        return;

    ULONG heapInfo = 2;

    HeapSetInformation(
        heap,
        HeapCompatibilityInformation,
        &heapInfo,
        sizeof(heapInfo)
    );
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS*)
{
    Log("[CRASH] UNHANDLED_EXCEPTION");
    return EXCEPTION_EXECUTE_HANDLER;
}

void InstallCrashHandler()
{
    SetUnhandledExceptionFilter(CrashHandler);
}

void InitializeTimerResolution()
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");

    if (!ntdll)
        return;

    pNtSetTimerResolution =
        (NtSetTimerResolution_t)
        GetProcAddress(
            ntdll,
            "NtSetTimerResolution"
        );

    pNtQueryTimerResolution =
        (NtQueryTimerResolution_t)
        GetProcAddress(
            ntdll,
            "NtQueryTimerResolution"
        );

    if (!pNtSetTimerResolution ||
        !pNtQueryTimerResolution)
    {
        return;
    }

    pNtQueryTimerResolution(
        &gMinimumResolution,
        &gMaximumResolution,
        &gCurrentTimerResolution
    );

    ULONG targetResolution =
        gMaximumResolution;

    if (gStack == DXVK_STACK ||
        gStack == ENB_STACK ||
        gStack == LOW_END_STACK)
    {
        targetResolution = 10000;
    }

    pNtSetTimerResolution(
        targetResolution,
        TRUE,
        &gCurrentTimerResolution
    );

    if (gPolicies.enableWinMMFallback)
    {
        timeBeginPeriod(1);
    }

    Log("[NTTIMER] API_READY");
    Log("[NTTIMER] SKIPPED_MODERN_FRAMEPACER");
}

void RestoreTimerResolution()
{
    if (pNtSetTimerResolution)
    {
        pNtSetTimerResolution(
            gCurrentTimerResolution,
            FALSE,
            &gCurrentTimerResolution
        );
    }

    if (gPolicies.enableWinMMFallback)
    {
        timeEndPeriod(1);
    }
}

void ApplyPriorityPolicy()
{
    if (gPolicies.enablePriorityBoost)
    {
        SetPriorityClass(
            GetCurrentProcess(),
            ABOVE_NORMAL_PRIORITY_CLASS
        );

        Log("[PRIORITY] ABOVE_NORMAL");
    }
    else
    {
        SetPriorityClass(
            GetCurrentProcess(),
            NORMAL_PRIORITY_CLASS
        );

        Log("[PRIORITY] NORMAL");
    }
}

void InitializeHooks()
{
    gD3D = Direct3DCreate9(
        D3D_SDK_VERSION
    );

    if (!gD3D)
    {
        Log("[D3D9] FAILED");
        return;
    }

    Log("[D3D9] INITIALIZED");
}

DWORD WINAPI MaintenanceThread(LPVOID)
{
    SetThreadPriority(
        GetCurrentThread(),
        THREAD_PRIORITY_BELOW_NORMAL
    );

    while (gRunning)
    {
        Sleep(300000);

        if (gPolicies.enableHeapCompaction)
        {
            HeapCompact(
                GetProcessHeap(),
                0
            );
        }

        if (gPolicies.enableWorkingSetTrim)
        {
            SetProcessWorkingSetSize(
                GetCurrentProcess(),
                (SIZE_T)-1,
                (SIZE_T)-1
            );
        }
    }

    return 0;
}

DWORD WINAPI RuntimeThread(LPVOID)
{
    fopen_s(
        &gLog,
        "GodHandEngine.log",
        "w"
    );

    DetectEnvironment();

    SelectRuntimeStack();

    if (gPolicies.enableLFH)
    {
        EnableLFH();
    }

    if (gPolicies.enableCrashHandler)
    {
        InstallCrashHandler();
    }

    if (gPolicies.enableTimerResolution)
    {
        InitializeTimerResolution();
    }

    ApplyPriorityPolicy();

    if (gPolicies.enableHeapCompaction)
    {
        HeapCompact(
            GetProcessHeap(),
            0
        );

        Log("[HEAP] COMPACTED");
    }

    if (gPolicies.enableWorkingSetTrim)
    {
        SetProcessWorkingSetSize(
            GetCurrentProcess(),
            (SIZE_T)-1,
            (SIZE_T)-1
        );

        Log("[MEMORY] WORKING_SET_TRIM");
    }

    InitializeHooks();

    gRunning = true;

    if (gPolicies.enableMaintenanceThread)
    {
        gMaintenanceThread = CreateThread(
            nullptr,
            0,
            MaintenanceThread,
            nullptr,
            0,
            nullptr
        );
    }

    Log("[RUNTIME] INITIALIZED");

    return 0;
}

void ShutdownRuntime()
{
    gRunning = false;

    if (gMaintenanceThread)
    {
        WaitForSingleObject(
            gMaintenanceThread,
            5000
        );

        CloseHandle(
            gMaintenanceThread
        );

        gMaintenanceThread = nullptr;
    }

    if (gPolicies.enableTimerResolution)
    {
        RestoreTimerResolution();
    }

    if (gD3D)
    {
        gD3D->Release();
        gD3D = nullptr;
    }

    if (gLog)
    {
        fclose(gLog);
        gLog = nullptr;
    }
}

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID
)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        gModule = hModule;

        DisableThreadLibraryCalls(
            hModule
        );

        CreateThread(
            nullptr,
            0,
            RuntimeThread,
            nullptr,
            0,
            nullptr
        );

        break;
    }

    case DLL_PROCESS_DETACH:
    {
        ShutdownRuntime();
        break;
    }
    }

    return TRUE;
}

extern "C"
{

    __declspec(dllexport)
        bool __stdcall OBSEPlugin_Query(
            const void*,
            void*
        )
    {
        return true;
    }

    __declspec(dllexport)
        bool __stdcall OBSEPlugin_Load(
            const void*
        )
    {
        return true;
    }

}