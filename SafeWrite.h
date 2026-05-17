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

void SafeWrite8(UInt32 addr, UInt32 data);
void SafeWrite16(UInt32 addr, UInt32 data);
void SafeWrite32(UInt32 addr, UInt32 data);
void SafeWriteBuf(UInt32 addr, void* data, UInt32 len);

void WriteRelJump(UInt32 jumpSrc, UInt32 jumpTgt);
void WriteRelCall(UInt32 jumpSrc, UInt32 jumpTgt);
void WriteRelJz(UInt32 jumpSrc, UInt32 jumpTgt);
void WriteRelJnz(UInt32 jumpSrc, UInt32 jumpTgt);
void WriteRelJle(UInt32 jumpSrc, UInt32 jumpTgt);

void WriteNop(UInt32 nopAddr, UInt8 numOfByte);
void PatchCallsInRange(UInt32 start, UInt32 end, UInt32 CallToPatch, UInt32 HookCall);
