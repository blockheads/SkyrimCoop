// link_stubs.cpp — Stub implementations for symbols resolved at runtime
// These satisfy the PE linker. At runtime, SKSE's address library patches
// vtable entries to point at the real implementations in SkyrimSE.exe.
// The MSVC intrinsic stubs allow linking against packages built with MSVC.
#ifndef __MINGW32__
#error "This file should only be compiled under MinGW"
#endif

#include <cstdint>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

// --- Skyrim engine virtual function stubs ---
// Include the actual headers to get correct class definitions and mangling.
// These virtual functions are defined in SkyrimSE.exe and resolved at runtime
// when the DLL is loaded into the game's process by SKSE.

#include "Games/Skyrim/Misc/ActorValueOwner.h"
#include "Games/Animation/IAnimationGraphManagerHolder.h"

// Provide the actual member function implementations that the linker needs.
// At runtime, calls go through the vtable which points at Skyrim's code.

ActorValueOwner::~ActorValueOwner() {}

float ActorValueOwner::GetValue(uint32_t aId) const noexcept
{
    (void)aId;
    return 0.0f;
}

float ActorValueOwner::GetPermanentValue(uint32_t aId) const noexcept
{
    (void)aId;
    return 0.0f;
}

float ActorValueOwner::GetBaseValue(uint32_t aId) const noexcept
{
    (void)aId;
    return 0.0f;
}

void ActorValueOwner::SetBaseValue(uint32_t aId)
{
    (void)aId;
}

void ActorValueOwner::ModValue(uint32_t aId, float aValue)
{
    (void)aId;
    (void)aValue;
}

void ActorValueOwner::ForceCurrent(ForceMode aMode, uint32_t aId, float aValue)
{
    (void)aMode;
    (void)aId;
    (void)aValue;
}

void ActorValueOwner::SetValue(uint32_t aId, float aValue) noexcept
{
    (void)aId;
    (void)aValue;
}

bool ActorValueOwner::IsPlayerOwner()
{
    return false;
}

IAnimationGraphManagerHolder::~IAnimationGraphManagerHolder() {}

bool IAnimationGraphManagerHolder::SendAnimationEvent(BSFixedString* apAnimEvent)
{
    (void)apAnimEvent;
    return false;
}

bool IAnimationGraphManagerHolder::GetBSAnimationGraph(BSAnimationGraphManager** aPtr) const
{
    (void)aPtr;
    return false;
}

// --- MSVC UCRT intrinsic stubs ---
// These are used by lua and libuv packages that were compiled with MSVC.

extern "C" {

int __intrinsic_setjmpex(void* apBuf)
{
    return _setjmp(static_cast<jmp_buf*>(apBuf)[0], nullptr);
}

unsigned long long __local_stdio_printf_options(void)
{
    return 0;
}

int __stdio_common_vsnwprintf_s(
    unsigned long long aOptions,
    wchar_t* apBuffer,
    size_t aBufferCount,
    size_t aMaxCount,
    const wchar_t* apFormat,
    void* apLocale,
    va_list aArgList)
{
    (void)aOptions;
    (void)apLocale;
    if (!apBuffer || aBufferCount == 0)
        return -1;
    return vsnwprintf(apBuffer, aBufferCount < aMaxCount ? aBufferCount : aMaxCount, apFormat, aArgList);
}

} // extern "C"
