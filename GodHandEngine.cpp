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
#include <float.h>
#include <xmmintrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

#pragma comment(lib, "winmm.lib")

using UInt8 = uint8_t;
using UInt16 = uint16_t;
using UInt32 = uint32_t;
using UInt64 = uint64_t;

using SInt8 = int8_t;
using SInt16 = int16_t;
using SInt32 = int32_t;
using SInt64 = int64_t;

const char* const MASTER_PLUGIN_NAME = "Z_GodHandEngine";
const UInt32 MASTER_PLUGIN_VERSION = 10;
const char* const MASTER_LOG_PATH = "Data\\OBSE\\Plugins\\Z_GodHandEngine.log";

struct CommandInfo;
struct ParamInfo;
class TESObjectREFR;
class Script;
class TESForm;
struct ScriptEventList;
struct ArrayKey;

typedef UInt32 PluginHandle;

enum
{
    kPluginHandle_Invalid = 0xFFFFFFFF
};

enum
{
    kInterface_Console = 0,
    kInterface_Serialization,
    kInterface_StringVar,
    kInterface_IO,
    kInterface_Messaging,
    kInterface_ArrayVar,
    kInterface_CommandTable,
    kInterface_Script,
    kInterface_Tasks,
    kInterface_Input,
    kInterface_EventManager,
    kInterface_Tasks2,
    kInterface_Max
};

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

    bool (*RegisterTypedCommand)(CommandInfo* info, UInt32 returnType);

    const char* (*GetOblivionDirectory)();
    bool (*GetPluginLoaded)(const char* pluginName);
    UInt32(*GetPluginVersion)(const char* pluginName);
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

alignas(64) static std::atomic<bool> g_RuntimeActive(false);
alignas(64) static std::atomic<bool> g_FPUException(false);
alignas(64) static std::atomic<bool> g_VMException(false);

static PluginHandle g_PluginHandle = kPluginHandle_Invalid;

static HANDLE g_RuntimeThread = nullptr;
static HANDLE g_TimerQueue = nullptr;
static HANDLE g_TimerHandle = nullptr;

class EngineLogger
{
private:

    FILE* m_File;
    SRWLOCK m_Lock;
    bool m_Ready;

public:

    EngineLogger()
        :
        m_File(nullptr),
        m_Ready(false)
    {
        InitializeSRWLock(&m_Lock);
    }

    ~EngineLogger()
    {
        Shutdown();
    }

    void Initialize()
    {
        AcquireSRWLockExclusive(&m_Lock);

        if (!m_Ready)
        {
            fopen_s(
                &m_File,
                MASTER_LOG_PATH,
                "wb"
            );

            if (m_File)
            {
                setvbuf(
                    m_File,
                    nullptr,
                    _IOFBF,
                    16384
                );

                m_Ready = true;
            }
        }

        ReleaseSRWLockExclusive(&m_Lock);
    }

    __forceinline void Log(
        const char* fmt,
        ...)
    {
        if (!m_Ready)
            return;

        char buffer[512];

        va_list args;

        va_start(args, fmt);

        vsnprintf(
            buffer,
            sizeof(buffer),
            fmt,
            args
        );

        va_end(args);

        AcquireSRWLockExclusive(&m_Lock);

        fwrite(
            buffer,
            1,
            strlen(buffer),
            m_File
        );

        fwrite(
            "\n",
            1,
            1,
            m_File
        );

        fflush(m_File);

        ReleaseSRWLockExclusive(&m_Lock);
    }

    void Shutdown()
    {
        AcquireSRWLockExclusive(&m_Lock);

        if (m_File)
        {
            fflush(m_File);

            fclose(m_File);

            m_File = nullptr;
        }

        m_Ready = false;

        ReleaseSRWLockExclusive(&m_Lock);
    }
};

static EngineLogger gLog;

namespace Memory
{
    __forceinline bool Protect(
        UInt32 addr,
        UInt32 len,
        DWORD protect,
        DWORD* oldProtect)
    {
        return VirtualProtect(
            reinterpret_cast<void*>(addr),
            len,
            protect,
            oldProtect
        ) != FALSE;
    }

    __forceinline bool SafeWrite(
        UInt32 addr,
        const void* data,
        UInt32 len)
    {
        DWORD oldProtect = 0;

        if (!Protect(
            addr,
            len,
            PAGE_EXECUTE_READWRITE,
            &oldProtect))
        {
            return false;
        }

        memcpy(
            reinterpret_cast<void*>(addr),
            data,
            len
        );

        FlushInstructionCache(
            GetCurrentProcess(),
            reinterpret_cast<void*>(addr),
            len
        );

        DWORD temp = 0;

        Protect(
            addr,
            len,
            oldProtect,
            &temp
        );

        return true;
    }

    __forceinline bool WriteRelCall(
        UInt32 src,
        UInt32 dst)
    {
        UInt8 buffer[5];

        buffer[0] = 0xE8;

        *reinterpret_cast<UInt32*>(buffer + 1) =
            dst - src - 5;

        return SafeWrite(
            src,
            buffer,
            5
        );
    }

    __forceinline bool WriteNOP(
        UInt32 addr,
        UInt32 size)
    {
        DWORD oldProtect = 0;

        if (!Protect(
            addr,
            size,
            PAGE_EXECUTE_READWRITE,
            &oldProtect))
        {
            return false;
        }

        memset(
            reinterpret_cast<void*>(addr),
            0x90,
            size
        );

        DWORD temp = 0;

        Protect(
            addr,
            size,
            oldProtect,
            &temp
        );

        return true;
    }
}

namespace Runtime
{
    VOID CALLBACK RuntimeTimerCallback(
        PVOID,
        BOOLEAN)
    {
        _mm_prefetch(
            reinterpret_cast<const char*>(0x440368),
            _MM_HINT_T0
        );

        _mm_prefetch(
            reinterpret_cast<const char*>(0x4FBF43),
            _MM_HINT_T0
        );
    }

    void ConfigureHeap()
    {
        ULONG mode = 2;

        HeapSetInformation(
            GetProcessHeap(),
            HeapCompatibilityInformation,
            &mode,
            sizeof(mode)
        );
    }

    void ConfigureTiming()
    {
        timeBeginPeriod(1);
    }

    void ConfigurePriority()
    {
        HANDLE process =
            GetCurrentProcess();

        SetPriorityClass(
            process,
            ABOVE_NORMAL_PRIORITY_CLASS
        );

        SetProcessPriorityBoost(
            process,
            FALSE
        );
    }

    void ConfigureAffinity()
    {
        HANDLE process =
            GetCurrentProcess();

        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;

        if (GetProcessAffinityMask(
            process,
            &processMask,
            &systemMask))
        {
            if (systemMask != 0)
            {
                SetProcessAffinityMask(
                    process,
                    systemMask
                );
            }
        }
    }

    DWORD WINAPI RuntimeThread(
        LPVOID)
    {
        HANDLE thread =
            GetCurrentThread();

        SetThreadPriority(
            thread,
            THREAD_PRIORITY_ABOVE_NORMAL
        );

        SetThreadIdealProcessor(
            thread,
            0
        );

        while (g_RuntimeActive.load(
            std::memory_order_relaxed))
        {
            SleepEx(
                INFINITE,
                TRUE
            );
        }

        return 0;
    }

    bool Initialize()
    {
        ConfigureHeap();

        ConfigureTiming();

        ConfigurePriority();

        ConfigureAffinity();

        g_RuntimeActive.store(
            true,
            std::memory_order_release
        );

        g_RuntimeThread =
            CreateThread(
                nullptr,
                0,
                RuntimeThread,
                nullptr,
                0,
                nullptr
            );

        if (!g_RuntimeThread)
        {
            gLog.Log("[RUNTIME_THREAD_FAILED]");
            return false;
        }

        g_TimerQueue =
            CreateTimerQueue();

        if (!g_TimerQueue)
        {
            gLog.Log("[TIMER_QUEUE_FAILED]");
            return false;
        }

        if (!CreateTimerQueueTimer(
            &g_TimerHandle,
            g_TimerQueue,
            RuntimeTimerCallback,
            nullptr,
            1000,
            1000,
            WT_EXECUTEINTIMERTHREAD))
        {
            gLog.Log("[TIMER_CREATE_FAILED]");
            return false;
        }

        return true;
    }

    void Shutdown()
    {
        g_RuntimeActive.store(
            false,
            std::memory_order_release
        );

        if (g_TimerHandle)
        {
            if (!DeleteTimerQueueTimer(
                g_TimerQueue,
                g_TimerHandle,
                INVALID_HANDLE_VALUE))
            {
                DWORD err = GetLastError();

                if (err != ERROR_IO_PENDING)
                {
                    gLog.Log("[TIMER_DELETE_FAILED]");
                }
            }

            g_TimerHandle = nullptr;
        }

        if (g_TimerQueue)
        {
            if (!DeleteTimerQueueEx(
                g_TimerQueue,
                INVALID_HANDLE_VALUE))
            {
                gLog.Log("[TIMER_QUEUE_DELETE_FAILED]");
            }

            g_TimerQueue = nullptr;
        }

        if (g_RuntimeThread)
        {
            QueueUserAPC(
                [](ULONG_PTR) {},
                g_RuntimeThread,
                0
            );

            WaitForSingleObject(
                g_RuntimeThread,
                3000
            );

            CloseHandle(
                g_RuntimeThread
            );

            g_RuntimeThread = nullptr;
        }

        timeEndPeriod(1);
    }
}

namespace Engine
{
    typedef void(__cdecl* PFN_RUNSCRIPTS)(
        void*,
        double,
        double,
        double
        );

    typedef UInt32(__fastcall* PFN_SCRIPTRUN)(
        void*,
        void*,
        void*,
        UInt32,
        UInt32,
        UInt32
        );

    static PFN_RUNSCRIPTS OriginalRunScripts = nullptr;
    static PFN_SCRIPTRUN OriginalScriptRun = nullptr;

    __declspec(noinline)
        void __cdecl HookedRunScripts(
            void* refr,
            double a4,
            double a5,
            double a3)
    {
        if (!refr)
            return;

        _clearfp();

        __try
        {
            OriginalRunScripts(
                refr,
                a4,
                a5,
                a3
            );
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (!g_FPUException.exchange(
                true,
                std::memory_order_relaxed))
            {
                gLog.Log("[FPU_EXCEPTION]");
            }
        }
    }

    __declspec(noinline)
        UInt32 __fastcall HookedScriptRun(
            void* scriptThis,
            void* edx,
            void* refr,
            UInt32 arg2,
            UInt32 arg3,
            UInt32 arg4)
    {
        if (!scriptThis)
            return 0;

        _clearfp();

        __try
        {
            return OriginalScriptRun(
                scriptThis,
                edx,
                refr,
                arg2,
                arg3,
                arg4
            );
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *(UInt8*)(0x0B361AC) = 0;
            *(UInt8*)(0x0B33A5C) = 0;

            if (!g_VMException.exchange(
                true,
                std::memory_order_relaxed))
            {
                gLog.Log("[VM_EXCEPTION]");
            }
        }

        return 0;
    }

    bool InstallHooks()
    {
        if (*(UInt8*)0x440368 != 0xE8)
        {
            gLog.Log("[HOOK_RUNSCRIPTS_INVALID]");
            return false;
        }

        {
            UInt32 rel =
                *(UInt32*)(0x440368 + 1);

            OriginalRunScripts =
                reinterpret_cast<PFN_RUNSCRIPTS>(
                    0x440368 + 5 + rel
                    );

            if (!Memory::WriteRelCall(
                0x440368,
                reinterpret_cast<UInt32>(
                    HookedRunScripts)))
            {
                gLog.Log("[HOOK_RUNSCRIPTS_FAILED]");
                return false;
            }
        }

        if (*(UInt8*)0x4FBF43 != 0xE8)
        {
            gLog.Log("[HOOK_SCRIPTRUN_INVALID]");
            return false;
        }

        {
            UInt32 rel =
                *(UInt32*)(0x4FBF43 + 1);

            OriginalScriptRun =
                reinterpret_cast<PFN_SCRIPTRUN>(
                    0x4FBF43 + 5 + rel
                    );

            if (!Memory::WriteRelCall(
                0x4FBF43,
                reinterpret_cast<UInt32>(
                    HookedScriptRun)))
            {
                gLog.Log("[HOOK_SCRIPTRUN_FAILED]");
                return false;
            }
        }

        if (!Memory::WriteNOP(
            0x440379,
            7))
        {
            gLog.Log("[HOOK_NOP_FAILED]");
            return false;
        }

        return true;
    }
}

extern "C"
{

    __declspec(dllexport)
        bool OBSEPlugin_Query(
            const OBSEInterface* obse,
            PluginInfo* info)
    {
        gLog.Initialize();

        if (!obse || !info)
        {
            gLog.Log("[QUERY_INVALID_INTERFACE]");
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
            gLog.Log("[EDITOR_NOT_SUPPORTED]");
            return false;
        }

        if (obse->oblivionVersion !=
            OBLIVION_VERSION_1_2_416)
        {
            gLog.Log("[INVALID_OBLIVION_VERSION]");
            return false;
        }

        gLog.Log("[QUERY_SUCCESS]");

        return true;
    }

    __declspec(dllexport)
        bool OBSEPlugin_Load(
            const OBSEInterface* obse)
    {
        if (!obse)
        {
            gLog.Log("[LOAD_INVALID_INTERFACE]");
            return false;
        }

        __try
        {
            g_PluginHandle =
                obse->GetPluginHandle();

            if (!Runtime::Initialize())
            {
                gLog.Log("[RUNTIME_INITIALIZE_FAILED]");
                return false;
            }

            if (!Engine::InstallHooks())
            {
                gLog.Log("[HOOK_INSTALL_FAILED]");
                return false;
            }

            gLog.Log("[ENGINE_INITIALIZED]");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gLog.Log("[LOAD_EXCEPTION]");
            return false;
        }

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
        Runtime::Shutdown();
        break;
    }
    }

    return TRUE;
}