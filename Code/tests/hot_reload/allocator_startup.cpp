// Regression for SKSE error 998 during DLL static initialization.
#include <RpmallocAllocator.hpp>
#include <SkseRipAllocator.h>
#include "../../libraries/reverse/Memory.hpp"
struct TESObjectREFR;
#include <Games/Overrides.h>
#include <rpmalloc.h>
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

static TiltedPhoques::RpmallocAllocator allocator;
static void* beforeMain = allocator.Allocate(128);

__attribute__((noinline)) static int JumpTarget() { return 42; }

void* RipAllocateN(size_t bytes) { return CoopSkse::AllocateJit(bytes); }

static void TestFarBranches()
{
    auto* farCode = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(0x600000000000ULL),
        4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(farCode);
    const uint8_t returns99[] = {0xB8, 99, 0, 0, 0, 0xC3};
    std::memcpy(farCode, returns99, sizeof(returns99));
    FlushInstructionCache(GetCurrentProcess(), farCode, sizeof(returns99));

    MEMORY_BASIC_INFORMATION before{}, after{};
    auto* target = reinterpret_cast<void*>(&JumpTarget);
    assert(VirtualQuery(target, &before, sizeof(before)));
    assert(before.Protect == PAGE_EXECUTE_READ);
    TiltedPhoques::Jump(mem::pointer(target), farCode);
    assert(VirtualQuery(target, &after, sizeof(after)) && after.Protect == before.Protect);
    int (*volatile function)() = JumpTarget;
    assert(function() == 99);

    const auto nearAddress = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + 0x50000000;
    auto* call = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(nearAddress),
        4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    assert(call);
    const uint8_t tail[] = {0x83, 0xC0, 1, 0xC3}; // add eax,1; ret
    std::memcpy(call + 5, tail, sizeof(tail));
    DWORD previous = 0;
    assert(VirtualProtect(call, 4096, PAGE_EXECUTE_READ, &previous));
    auto farFunction = reinterpret_cast<int (*)()>(farCode);
    TiltedPhoques::PutCall(mem::pointer(call), farFunction);
    assert(reinterpret_cast<int (*)()>(call)() == 100);
    assert(VirtualQuery(call, &after, sizeof(after)) && after.Protect == PAGE_EXECUTE_READ);
    int (*oldFunction)() = nullptr;
    TiltedPhoques::SwapCall(mem::pointer(call), oldFunction, farFunction);
    assert(oldFunction() == 99);
    TiltedPhoques::Nop(mem::pointer(call + 5), 3);
    TiltedPhoques::Put<uint8_t>(mem::pointer(call + 8), 0xC3);
    assert(reinterpret_cast<int (*)()>(call)() == 99);
    assert(VirtualQuery(call, &after, sizeof(after)) && after.Protect == PAGE_EXECUTE_READ);
    VirtualFree(call, 0, MEM_RELEASE);
    // JumpTarget now references farCode; keep it alive until process exit.
}

static DWORD WINAPI EngineThread(void*)
{
    assert(!rpmalloc_is_thread_initialized());
    assert(!ScopedExperienceOverride::IsOverriden());
    {
        ScopedExperienceOverride outer;
        assert(ScopedExperienceOverride::IsOverriden());
        { ScopedExperienceOverride inner; }
        assert(ScopedExperienceOverride::IsOverriden());
    }
    assert(!ScopedExperienceOverride::IsOverriden());
    for (int i = 0; i < 1000; ++i)
    {
        auto* memory = allocator.Allocate(256);
        assert(memory && allocator.Size(memory) >= 256);
        std::memset(memory, 0xAB, 256);
        allocator.Free(memory);
        memory = allocator.AlignedAllocate(256, 64);
        assert(memory && reinterpret_cast<uintptr_t>(memory) % 64 == 0);
        allocator.AlignedFree(memory);
    }
    assert(rpmalloc_is_thread_initialized());
    auto* stub = CoopSkse::AllocateJit(16);
    assert(stub && reinterpret_cast<uintptr_t>(stub) % 16 == 0);
    rpmalloc_thread_finalize(1);
    return 0;
}

int main()
{
    assert(beforeMain);
    allocator.Free(beforeMain);
    auto* code = static_cast<uint8_t*>(CoopSkse::AllocateJit(16));
    assert(code);
    const auto displacement = reinterpret_cast<intptr_t>(&JumpTarget) - reinterpret_cast<intptr_t>(code + 5);
    assert(displacement >= INT32_MIN && displacement <= INT32_MAX);
    const auto rel32 = static_cast<int32_t>(displacement);
    code[0] = 0xE9; // jmp rel32 into the host executable
    std::memcpy(code + 1, &rel32, sizeof(rel32));
    FlushInstructionCache(GetCurrentProcess(), code, 5);
    assert(reinterpret_cast<int (*)()>(code)() == 42);
    assert(!CoopSkse::AllocateJit(std::numeric_limits<size_t>::max()));
    TestFarBranches();
    HANDLE threads[8]{};
    ScopedExperienceOverride mainThreadOverride;
    for (auto& thread : threads)
    {
        thread = CreateThread(nullptr, 0, EngineThread, nullptr, 0, nullptr);
        assert(thread);
    }
    assert(WaitForMultipleObjects(8, threads, TRUE, 10000) == WAIT_OBJECT_0);
    for (auto thread : threads)
    {
        DWORD result = 1;
        assert(GetExitCodeThread(thread, &result) && result == 0);
        CloseHandle(thread);
    }
    assert(ScopedExperienceOverride::IsOverriden());
    puts("PASS: allocation before main, 8 Windows threads, override TLS isolation, near/far branches and code page permissions");
}
