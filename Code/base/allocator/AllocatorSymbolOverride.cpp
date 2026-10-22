// The plugin's global new and delete: rpmalloc for every allocation (containers, messages, buffers).
#include <rpmalloc.h>

#include <cstdlib>
#include <new>

namespace
{
// The SKSE DLL has no executable main() to initialize rpmalloc: static globals allocate before SKSEPlugin_Load,
// and engine-created threads can enter the allocator without our thread startup code. So it starts lazily, before
// the first allocation on each thread; it is never finalized (engine threads and static destructors still use it).
void EnsureThreadHeap() noexcept
{
    static const int cInitialized = rpmalloc_initialize();
    if (cInitialized != 0)
        std::abort();
    if (!rpmalloc_is_thread_initialized())
        rpmalloc_thread_initialize();
}

void* Allocate(size_t aSize) noexcept
{
    EnsureThreadHeap();
    return rpmalloc(aSize);
}

void Free(void* apBlock) noexcept
{
    EnsureThreadHeap();
    rpfree(apBlock);
}
} // namespace

void* operator new(size_t size)
{
    return Allocate(size);
}

void operator delete(void* p) noexcept
{
    Free(p);
}

void* operator new[](size_t size)
{
    return Allocate(size);
}

void operator delete[](void* p) noexcept
{
    Free(p);
}

void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    return Allocate(size);
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
    return Allocate(size);
}

void operator delete(void* p, const std::nothrow_t&) noexcept
{
    Free(p);
}

void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    Free(p);
}

void operator delete(void* p, size_t) noexcept
{
    Free(p);
}

void operator delete[](void* p, size_t) noexcept
{
    Free(p);
}
