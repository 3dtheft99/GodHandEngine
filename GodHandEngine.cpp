#define OBLIVION 
#define OBLIVION_VERSION_1_2_416 0x010201A0

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <windows.h>  
#include <mmsystem.h> 
#include <psapi.h>  
#include <float.h> 

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "psapi.lib")

const char* const MASTER_PLUGIN_NAME = "Z_GodHandEngine_Definatory";
const std::uint32_t MASTER_PLUGIN_VERSION = 8;
const char* const MASTER_LOG_PATH = "Data\\OBSE\\Plugins\\Z_GodHandEngine_Safe.log";

typedef enum _IO_PRIORITY_HINT {
    IoPriorityVeryLow = 0,
    IoPriorityLow,
    IoPriorityNormal,
    IoPriorityHigh,
    IoPriorityCritical,
    MaxIoPriorityTypes
} IO_PRIORITY_HINT;

#ifndef ProcessIoPriorityPolicy
#define ProcessIoPriorityPolicy ((PROCESS_INFORMATION_CLASS)21)
#endif

#ifndef _OBSE_TYPES_
#define _OBSE_TYPES_
typedef unsigned char      UInt8;
typedef unsigned short     UInt16;
typedef unsigned long      UInt32;
typedef unsigned __int64   UInt64;
typedef signed char        SInt8;
typedef signed short       SInt16;
typedef signed long        SInt32;
typedef signed __int64     SInt64;
#endif

struct CommandInfo;
struct ParamInfo;
class TESObjectREFR;
class Script;
class TESForm;
struct ScriptEventList;
struct ArrayKey;

typedef UInt32 PluginHandle;
enum { kPluginHandle_Invalid = 0xFFFFFFFF };

enum {
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
    PluginHandle(*GetPluginHandle)();
    bool (*RegisterTypedCommand)(CommandInfo* info, UInt32 returnType);
    const char* (*GetOblivionDirectory)();
    bool (*GetPluginLoaded)(const char* pluginName);
    UInt32(*GetPluginVersion)(const char* pluginName);
};

struct PluginInfo
{
    UInt32 infoVersion;
    const char* name;
    UInt32 version;
};

static_assert(offsetof(OBSEInterface, RegisterCommand) == 16, "OBSEInterface ABI drift before RegisterCommand");
static_assert(offsetof(OBSEInterface, QueryInterface) == 24, "OBSEInterface ABI drift before QueryInterface");
static_assert(offsetof(OBSEInterface, GetPluginHandle) == 28, "OBSEInterface ABI drift before GetPluginHandle");
static_assert(sizeof(PluginInfo) == 12, "PluginInfo ABI drift");

struct OBSEMessagingInterface
{
    struct Message
    {
        const char* sender;
        UInt32 type;
        UInt32 dataLen;
        void* data;
    };

    using EventCallback = void (*)(Message* message);

    UInt32 version;
    bool (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
    bool (*Dispatch)(PluginHandle sender, UInt32 messageType, void* data, UInt32 dataLen, const char* receiver);
};

struct OBSETasks2Interface {
    UInt32 version;
    void* (*EnqueueTask)(bool (*f)());
    void (*RemoveTask)(void* f);
    bool (*IsTaskPresent)(bool (*f)());
    void (*ReEnqueueTask)(void* f);
    void* (*EnqueueTaskRemovable)(bool (*f)());
    void (*RemoveTaskRemovable)(void* f);
    bool (*IsTaskPresentRemovable)(bool (*f)());
    void (*ReEnqueueTaskRemovable)(void* f);
    bool (*HasTasks)();
};

struct Options {
    bool aboveNormalPriority = true;
    bool highIoPriority = true;
};
static Options g_options;

static PluginHandle          g_PluginHandle = kPluginHandle_Invalid;
static OBSETasks2Interface* g_TasksInterface = nullptr;
static OBSEMessagingInterface* g_MessagingInterface = nullptr;

static std::atomic<bool> g_HasLoggedFPUError(false);

class EngineLogger {
private:
    FILE* m_LogFile;
    bool m_IsReady;
    CRITICAL_SECTION m_Lock;

public:
    EngineLogger() : m_LogFile(nullptr), m_IsReady(false) {
        InitializeCriticalSectionAndSpinCount(&m_Lock, 4000);
    }

    ~EngineLogger() {
        Close();
        DeleteCriticalSection(&m_Lock);
    }

    void Initialize() {
        EnterCriticalSection(&m_Lock);
        if (!m_IsReady) {
            if (fopen_s(&m_LogFile, MASTER_LOG_PATH, "w") == 0 && m_LogFile != nullptr) {
                m_IsReady = true;
                fprintf(m_LogFile, "================================================================================\n");
                fprintf(m_LogFile, "[INIT] Z_GodHandEngine active.\n");
                fprintf(m_LogFile, "================================================================================\n");
                fflush(m_LogFile);
            }
        }
        LeaveCriticalSection(&m_Lock);
    }

    void Log(const char* text) {
        if (!m_IsReady || m_LogFile == nullptr) return;
        EnterCriticalSection(&m_Lock);
        SYSTEMTIME time;
        GetLocalTime(&time);
        fprintf(m_LogFile, "[%02d:%02d:%02d.%03d] %s\n",
            time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, text);
        fflush(m_LogFile);
        LeaveCriticalSection(&m_Lock);
    }

    void Close() {
        EnterCriticalSection(&m_Lock);
        if (m_IsReady && m_LogFile != nullptr) {
            fprintf(m_LogFile, "[EXIT] Log closed.\n");
            fclose(m_LogFile);
            m_LogFile = nullptr;
            m_IsReady = false;
        }
        LeaveCriticalSection(&m_Lock);
    }
};

static EngineLogger g_SafeLog;

void Log(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    g_SafeLog.Log(buf);
}

static void EngineSafeWrite8(UInt32 addr, UInt32 data) {
    DWORD oldProtect;
    if (VirtualProtect((void*)addr, sizeof(UInt8), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *((UInt8*)addr) = (UInt8)data;
        VirtualProtect((void*)addr, sizeof(UInt8), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (void*)addr, sizeof(UInt8));
    }
}

static void EngineSafeWriteBuf(UInt32 addr, void* data, UInt32 len) {
    DWORD oldProtect;
    if (VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy((void*)addr, data, len);
        VirtualProtect((void*)addr, len, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (void*)addr, len);
    }
}

static void EngineWriteRelCall(UInt32 jumpSrc, UInt32 jumpTgt) {
    UInt8 buffer[5];
    buffer[0] = 0xE8;
    *reinterpret_cast<UInt32*>(buffer + 1) = (jumpTgt - jumpSrc - 5);
    EngineSafeWriteBuf(jumpSrc, buffer, 5);
}

static void EngineWriteNop(UInt32 nopAddr, UInt8 numOfByte) {
    if (numOfByte == 0) return;
    DWORD oldProtect;
    if (VirtualProtect((void*)nopAddr, numOfByte, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memset((void*)nopAddr, 0x90, numOfByte);
        VirtualProtect((void*)nopAddr, numOfByte, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), (void*)nopAddr, numOfByte);
    }
}

bool IsModuleHandleValid(HMODULE module) {
    if (module == nullptr) return false;
    __try {
        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)module;
        return (dosHeader->e_magic == IMAGE_DOS_SIGNATURE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

typedef BOOL(WINAPI* PFN_SetProcessInformation)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

void ApplyPriorityAndAffinity()
{
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!IsModuleHandleValid(hKernel32)) return;

    HANDLE process = GetCurrentProcess();

    if (g_options.aboveNormalPriority)
    {
        if (SetPriorityClass(process, ABOVE_NORMAL_PRIORITY_CLASS))
        {
            g_SafeLog.Log("[SUCCESS] CPU Priority set to ABOVE_NORMAL.");
        }
    }

    OSVERSIONINFOA osInfo;
    ZeroMemory(&osInfo, sizeof(OSVERSIONINFOA));
    osInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    bool isWindows7OrOlder = true;

#pragma warning(suppress: 4996) 
    if (GetVersionExA(&osInfo)) {
        if (osInfo.dwMajorVersion > 6 || (osInfo.dwMajorVersion == 6 && osInfo.dwMinorVersion >= 2)) {
            isWindows7OrOlder = false;
        }
    }

    if (isWindows7OrOlder) {
        g_SafeLog.Log("[WARN] Legacy OS detected. I/O feature bypassed.");
        return;
    }

    if (g_options.highIoPriority)
    {
        PFN_SetProcessInformation pSetProcessInformation = (PFN_SetProcessInformation)GetProcAddress(hKernel32, "SetProcessInformation");

        if (pSetProcessInformation) {
            IO_PRIORITY_HINT ioPriority = IoPriorityNormal;
            __try {
                if (pSetProcessInformation(process, ProcessIoPriorityPolicy, &ioPriority, sizeof(ioPriority)))
                {
                    g_SafeLog.Log("[SUCCESS] Storage I/O priority set to NORMAL.");
                }
                else
                {
                    g_SafeLog.Log("[WARN] System refused I/O priority assignment.");
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                g_SafeLog.Log("[ERROR] I/O priority policy triggered an exception.");
            }
        }
        else {
            g_SafeLog.Log("[WARN] SetProcessInformation API not found.");
        }
    }
}

typedef void(__cdecl* PFN_RUNSCRIPTS_NATIO)(void* pTargetRefr, double a4, double a5, double a3);
static PFN_RUNSCRIPTS_NATIO g_OriginalRunScripts = nullptr;

void __cdecl HookedRunScripts(void* pTargetRefr, double a4, double a5, double a3) {
    if (pTargetRefr == nullptr) return;

    unsigned int currentControl;
    _controlfp_s(&currentControl, _MCW_EM, _MCW_EM);
    _clearfp();

    __try {
        if (g_OriginalRunScripts) {
            g_OriginalRunScripts(pTargetRefr, a4, a5, a3);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_HasLoggedFPUError.exchange(true)) {
            g_SafeLog.Log("[ERROR] 0x440368 FPU Exception absorbed.");
        }
    }
}

typedef UInt32(__fastcall* PFN_SCRIPTRUN_NATIO)(void* pScriptThis, void* pDummyEdx, void* pCallingRef, UInt32 arg2, UInt32 arg3, UInt32 arg4);
static PFN_SCRIPTRUN_NATIO g_OriginalScriptRun = nullptr;

__declspec(noinline) UInt32 __fastcall HookedScriptRun(void* pScriptThis, void* pDummyEdx, void* pCallingRef, UInt32 arg2, UInt32 arg3, UInt32 arg4) {
    if (pScriptThis == nullptr) return 0;

    _clearfp();

    __try {
        if (g_OriginalScriptRun) {
            return g_OriginalScriptRun(pScriptThis, pDummyEdx, pCallingRef, arg2, arg3, arg4);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        *(BYTE*)(0x0B361AC) = 0;
        *(BYTE*)(0x0B33A5C) = 0;
        g_SafeLog.Log("[ERROR] 0x4FBF43 Script exception intercepted. State reset.");
    }
    return 0;
}

void ApplyEngineAssemblyPatches() {
    g_SafeLog.Log("[INIT] Applying memory patches...");

    DWORD oldProtect;

    __try {
        if (VirtualProtect((void*)0x440368, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            if (*(UInt8*)0x440368 == 0xE8) {
                UInt32 targetCallOffset1 = *(UInt32*)(0x440368 + 1);
                g_OriginalRunScripts = (PFN_RUNSCRIPTS_NATIO)(0x440368 + 5 + targetCallOffset1);
                EngineWriteRelCall(0x440368, (UInt32)HookedRunScripts);
                g_SafeLog.Log("[SUCCESS] 0x440368 signature matched. (Scenario Protection).");
            }
            else {
                g_SafeLog.Log("[WARN] Expected signature at 0x440368 not found. Hook bypassed.");
            }
            VirtualProtect((void*)0x440368, 16, oldProtect, &oldProtect);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_SafeLog.Log("[ERROR] 0x440368 signature mismatch. Hook bypassed.");
    }

    __try {
        if (VirtualProtect((void*)0x4FBF43, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            if (*(UInt8*)0x4FBF43 == 0xE8) {
                UInt32 targetCallOffset2 = *(UInt32*)(0x4FBF43 + 1);
                g_OriginalScriptRun = (PFN_SCRIPTRUN_NATIO)(0x4FBF43 + 5 + targetCallOffset2);
                EngineWriteRelCall(0x4FBF43, (UInt32)HookedScriptRun);
                g_SafeLog.Log("[SUCCESS] 0x4FBF43 signature matched. (VM Script Protection).");
            }
            else {
                g_SafeLog.Log("[WARN] Expected signature at 0x4FBF43 not found. Hook bypassed.");
            }
            VirtualProtect((void*)0x4FBF43, 16, oldProtect, &oldProtect);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_SafeLog.Log("[ERROR] 0x4FBF43 signature mismatch. Hook bypassed.");
    }

    __try {
        if (VirtualProtect((void*)0x440379, 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            EngineWriteNop(0x440379, 7);
            g_SafeLog.Log("[SUCCESS] 0x440379 stack alignment NOP applied.");
            VirtualProtect((void*)0x440379, 8, oldProtect, &oldProtect);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        g_SafeLog.Log("[ERROR] 0x440379 VirtualProtect exception.");
    }
}

extern "C" {

    __declspec(dllexport) bool OBSEPlugin_Query(const OBSEInterface* obse, PluginInfo* info) {
        g_SafeLog.Initialize();
        g_SafeLog.Log("[QUERY] OBSE Plugin Query triggered.");

        if (obse == nullptr || info == nullptr) return false;
        __try {
            info->infoVersion = 3;
            info->name = MASTER_PLUGIN_NAME;
            info->version = MASTER_PLUGIN_VERSION;

            if (obse->oblivionVersion < OBLIVION_VERSION_1_2_416) {
                g_SafeLog.Log("[ERROR] Incompatible Oblivion version.");
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return true;
    }

    __declspec(dllexport) bool OBSEPlugin_Load(const OBSEInterface* obse) {
        if (obse == nullptr) return false;
        g_SafeLog.Log("[LOAD] OBSE Plugin Load triggered.");

        __try {
            if (obse->GetPluginHandle) {
                g_PluginHandle = obse->GetPluginHandle();
            }

            g_TasksInterface = (OBSETasks2Interface*)(obse->QueryInterface(kInterface_Tasks2));
            g_MessagingInterface = (OBSEMessagingInterface*)(obse->QueryInterface(kInterface_Messaging));

            if (g_MessagingInterface) {
                g_SafeLog.Log("[LOAD] Messaging Interface linked.");
            }

            ApplyPriorityAndAffinity();
            ApplyEngineAssemblyPatches();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            g_SafeLog.Log("[CRITICAL] Load exception intercepted.");
            return false;
        }

        g_SafeLog.Log("================================================================================");
        g_SafeLog.Log("================================================================================");
        return true;
    }
}
