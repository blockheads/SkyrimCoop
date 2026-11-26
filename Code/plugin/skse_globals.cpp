// skse_globals.cpp — globals the original immersive launcher provided.
// SKSE loads the plugin with no launcher, so the DLL defines them.
#include <windows.h>
#include <cstdlib>


HICON g_SharedWindowIcon = nullptr;

// The SKSE route has no immersive launcher or its executable highrip section.
#include "SkseRipAllocator.h"

void* RipAllocateN(size_t aBlockLength)
{
    auto* memory = CoopSkse::AllocateJit(aBlockLength);
    if (!memory)
    {
        OutputDebugStringA("SkyrimCoop: unable to reserve nearby executable hook memory");
        std::abort();
    }
    return memory;
}

