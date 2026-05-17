#define OBLIVION
#include <windows.h>  
#include <string.h>

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

void SafeWrite8(UInt32 addr, UInt32 data)
{
	UInt32	oldProtect;
	VirtualProtect((void*)addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((UInt8*)addr) = (UInt8)data;
	VirtualProtect((void*)addr, 4, oldProtect, &oldProtect);
}

void SafeWrite16(UInt32 addr, UInt32 data)
{
	UInt32	oldProtect;
	VirtualProtect((void*)addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((UInt16*)addr) = (UInt16)data;
	VirtualProtect((void*)addr, 4, oldProtect, &oldProtect);
}

void SafeWrite32(UInt32 addr, UInt32 data)
{
	UInt32	oldProtect;
	VirtualProtect((void*)addr, 4, PAGE_EXECUTE_READWRITE, &oldProtect);
	*((UInt32*)addr) = data;
	VirtualProtect((void*)addr, 4, oldProtect, &oldProtect);
}

void SafeWriteBuf(UInt32 addr, void* data, UInt32 len)
{
	UInt32	oldProtect;
	VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
	memcpy((void*)addr, data, len);
	VirtualProtect((void*)addr, len, oldProtect, &oldProtect);
}

void WriteRelJump(UInt32 jumpSrc, UInt32 jumpTgt)
{
	SafeWrite8(jumpSrc, 0xE9);
	SafeWrite32(jumpSrc + 1, jumpTgt - jumpSrc - 1 - 4);
}

void WriteRelCall(UInt32 jumpSrc, UInt32 jumpTgt)
{
	SafeWrite8(jumpSrc, 0xE8);
	SafeWrite32(jumpSrc + 1, jumpTgt - jumpSrc - 1 - 4);
}

void WriteRelJz(UInt32 jumpSrc, UInt32 jumpTgt)
{
	SafeWrite16(jumpSrc, 0x840F);
	SafeWrite32(jumpSrc + 2, jumpTgt - jumpSrc - 2 - 4);
}

void WriteRelJnz(UInt32 jumpSrc, UInt32 jumpTgt)
{
	SafeWrite16(jumpSrc, 0x850F);
	SafeWrite32(jumpSrc + 2, jumpTgt - jumpSrc - 2 - 4);
}

void WriteRelJle(UInt32 jumpSrc, UInt32 jumpTgt)
{
	SafeWrite16(jumpSrc, 0x8E0F);
	SafeWrite32(jumpSrc + 2, jumpTgt - jumpSrc - 2 - 4);
}

void WriteNop(UInt32 nopAddr, UInt8 numOfByte) {
	for (UInt8 i = 0; i < numOfByte; i++) {
		SafeWrite8(nopAddr + i, 0x90);
	}
}

void PatchCallsInRange(UInt32 start, UInt32 end, UInt32 CallToPatch, UInt32 HookCall) {
	UInt32	oldProtect;
	VirtualProtect((void*)start, end - start, PAGE_EXECUTE_READWRITE, &oldProtect);
	for (UInt32 current = start; current < end; current++) {
		if (*(UInt8*)current == 0xE8) {
			UInt32 callTarget = *(UInt32*)(current + 1) + current + 1 + 4;
			if (callTarget == CallToPatch) {
				SafeWrite32(current + 1, HookCall - current - 1 - 4);
			}
		}
	}
	VirtualProtect((void*)start, end - start, oldProtect, &oldProtect);
}
