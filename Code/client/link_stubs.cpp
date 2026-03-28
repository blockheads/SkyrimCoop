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
#include <cstring>
#include <windows.h>

// --- Skyrim engine virtual function stubs ---
// Include the actual headers to get correct class definitions and mangling.
// These virtual functions are defined in SkyrimSE.exe and resolved at runtime
// when the DLL is loaded into the game's process by SKSE.

#include "Games/Skyrim/Misc/ActorValueOwner.h"
#include "Games/Animation/IAnimationGraphManagerHolder.h"
#include "Games/Skyrim/Components/BGSKeywordForm.h"

// ---- ActorValueOwner vtable stubs ----

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

// ---- IAnimationGraphManagerHolder vtable stubs ----

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

uint32_t IAnimationGraphManagerHolder::sub_3() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_4() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_5() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_6() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_7() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_8() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_9() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_A() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_B() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_C() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_D() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_E() { return 0; }
uint32_t IAnimationGraphManagerHolder::sub_F() { return 0; }

bool IAnimationGraphManagerHolder::GetVariableFloat(BSFixedString* apVariable, float* apReturn)
{
    (void)apVariable;
    (void)apReturn;
    return false;
}

bool IAnimationGraphManagerHolder::GetVariableInt(BSFixedString* apVariable, uint32_t* apReturn)
{
    (void)apVariable;
    (void)apReturn;
    return false;
}

bool IAnimationGraphManagerHolder::GetVariableBool(BSFixedString* apVariable, bool* apReturn)
{
    (void)apVariable;
    (void)apReturn;
    return false;
}

// ---- BGSKeywordForm vtable stubs ----

bool BGSKeywordForm::Contains(BGSKeyword* apKeyword) const
{
    (void)apKeyword;
    return false;
}

void BGSKeywordForm::sub_5()
{
}

// --- Global variable stubs ---
// These globals are defined in immersive_launcher or SkyrimSE.exe.
// The DLL references them but at runtime they live in the host process.

HICON g_SharedWindowIcon = nullptr;

// --- RipAllocateN stub ---
// Defined in immersive_launcher/memory/RipAllocator.cpp. The DLL uses this
// for JIT assembly allocation. Provide a simple heap-based fallback that
// satisfies the linker; the real allocator is in the launcher process.

static uint8_t s_ripHeap[1024 * 1024]; // 1MB fallback pool
static uint8_t* s_pRipCursor = s_ripHeap;

void* RipAllocateN(size_t aBlockLength)
{
    const size_t cAlignment = 16;
    uintptr_t current = reinterpret_cast<uintptr_t>(s_pRipCursor);
    uintptr_t aligned = (current + (cAlignment - 1)) & ~(cAlignment - 1);
    size_t alignedLen = (aBlockLength + (cAlignment - 1)) & ~(cAlignment - 1);

    // Bounds check against pool
    if (aligned + alignedLen > reinterpret_cast<uintptr_t>(s_ripHeap + sizeof(s_ripHeap)))
        return nullptr;

    s_pRipCursor = reinterpret_cast<uint8_t*>(aligned + alignedLen);
    return reinterpret_cast<void*>(aligned);
}

// --- MSVC intrinsic stubs ---
// _ReturnAddress is an MSVC intrinsic. GCC provides __builtin_return_address.
// We cannot replace the call site from here, so provide a C-linkage stub.
// The caller in UI.cpp uses it for debug logging only — returning nullptr is safe.

extern "C" void* _ReturnAddress(void)
{
    return __builtin_return_address(0);
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
