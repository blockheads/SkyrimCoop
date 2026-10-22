#pragma once

#include <xbyak/xbyak.h>
#include <mem/mem.h>

// Stubs within a branch's reach of the game (skse_globals.cpp).
extern void* RipAllocateN(size_t aBlockLength);

namespace Hooks
{
    // Wraps the generator in a core stub page.
    struct CodeGenerator : Xbyak::CodeGenerator
    {
        // Warning(Force): Ensure those are exponents of 2.
        static constexpr size_t kDefaultAllocStubsize = 64;

        CodeGenerator(const size_t aAllocSize = kDefaultAllocStubsize) :
            Xbyak::CodeGenerator(aAllocSize, RipAllocateN(aAllocSize)){}

        void jmp_S(const mem::pointer acEa) {
            Xbyak::CodeGenerator::jmp(acEa.as<const void*>());
        }
    };
}
