#pragma once
#include <windows.h>

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
struct PluginInfo;

typedef UInt32	PluginHandle;
enum { kPluginHandle_Invalid = 0xFFFFFFFF };

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
	UInt32	obseVersion;
	UInt32	oblivionVersion;
	UInt32	editorVersion;
	UInt32	isEditor;
	bool	(*RegisterCommand)(CommandInfo* info);
	void	(*SetOpcodeBase)(UInt32 opcode);
	void* (*QueryInterface)(UInt32 id);
	PluginHandle(*GetPluginHandle)(void);
	bool	(*RegisterTypedCommand)(CommandInfo* info, UInt32 retnType);
	const char* (*GetOblivionDirectory)();
	bool	(*GetPluginLoaded)(const char* pluginName);
	UInt32(*GetPluginVersion)(const char* pluginName);
};

struct OBSETasks2Interface {
	UInt32 version;
	void* (*EnqueueTask)(bool (*f)());
	void (*RemoveTask)(void* f);
	bool (*IsTaskPresent)(void* f);
	void (*ReEnqueueTask)(void* f);
	void* (*EnqueueTaskRemovable)(bool (*f)());
	void (*RemoveTaskRemovable)(void* f);
	bool (*IsTaskPresentRemovable)(void* f);
	void (*ReEnqueueTaskRemovable)(void* f);
	bool (*HasTasks)();
};

struct PluginInfo
{
	enum { kInfoVersion = 3 };
	UInt32			infoVersion;
	const char* name;
	UInt32			version;
};

typedef bool(__cdecl* _OBSEPlugin_Query)(const OBSEInterface* obse, PluginInfo* info);
typedef bool(__cdecl* _OBSEPlugin_Load)(const OBSEInterface* obse);