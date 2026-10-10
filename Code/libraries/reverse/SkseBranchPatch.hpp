#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>

extern void* RipAllocateN(size_t bytes);

namespace TiltedPhoques::detail
{
inline void WriteSkseBranch(void* source, const void* destination, uint8_t opcode) noexcept
{
    auto from = reinterpret_cast<intptr_t>(source);
    auto to = reinterpret_cast<intptr_t>(destination);
    auto displacement = to - from - 5;
    if (displacement < INT32_MIN || displacement > INT32_MAX)
    {
        // A call to this stub retains the original return address. Its indirect
        // jump preserves registers and reaches a DLL anywhere in the process.
        auto* stub = static_cast<uint8_t*>(RipAllocateN(14));
        if (!stub) std::abort();
        const uint8_t jump[] = {0xFF, 0x25, 0, 0, 0, 0};
        std::memcpy(stub, jump, sizeof(jump));
        std::memcpy(stub + 6, &destination, sizeof(destination));
        FlushInstructionCache(GetCurrentProcess(), stub, 14);
        displacement = reinterpret_cast<intptr_t>(stub) - from - 5;
        if (displacement < INT32_MIN || displacement > INT32_MAX) std::abort();
    }
    DWORD previous = 0;
    if (!VirtualProtect(source, 5, PAGE_EXECUTE_READWRITE, &previous)) std::abort();
    const auto relative = static_cast<int32_t>(displacement);
    auto* bytes = static_cast<uint8_t*>(source);
    bytes[0] = opcode;
    std::memcpy(bytes + 1, &relative, sizeof(relative));
    FlushInstructionCache(GetCurrentProcess(), source, 5);
    DWORD ignored = 0;
    if (!VirtualProtect(source, 5, previous, &ignored)) std::abort();
}
}
