#include "RpmallocAllocator.hpp"

#include <rpmalloc.h>
#include <cstdlib>

namespace
{
void EnsureThreadHeap() noexcept
{
#ifdef _WIN32
    // The SKSE DLL has no executable main() to initialize rpmalloc. Static
    // globals allocate before SKSEPlugin_Load/DllMain, and engine-created
    // threads can later enter the allocator without our thread startup code.
    // Initialize lazily before the first allocation, including static ctors.
    static const int initialized = rpmalloc_initialize();
    if (initialized != 0)
        std::abort();
    if (!rpmalloc_is_thread_initialized())
        rpmalloc_thread_initialize();
    // The resident plugin owns this allocator for the process lifetime; do not
    // finalize it while engine threads or DLL static destructors can use it.
#endif
}
}

namespace TiltedPhoques
{
    void* RpmallocAllocator::Allocate(const size_t aSize) noexcept
    {
        EnsureThreadHeap();
        return rpmalloc(aSize);
    }

    void RpmallocAllocator::Free(void* apData) noexcept
    {
        EnsureThreadHeap();
        rpfree(apData);
    }

    size_t RpmallocAllocator::Size(void* apData) noexcept
    {
        if (apData == nullptr) return 0;

        return rpmalloc_usable_size(apData);
    }

    void* RpmallocAllocator::AlignedAllocate(size_t aSize, size_t aAlignment) noexcept
    {
        EnsureThreadHeap();
        return rpaligned_alloc(aAlignment, aSize);
    }

    void RpmallocAllocator::AlignedFree(void* apData) noexcept
    {
        EnsureThreadHeap();
        rpfree(apData);
    }
}
