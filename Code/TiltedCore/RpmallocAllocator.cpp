#include "RpmallocAllocator.hpp"

#include <rpmalloc.h>

namespace TiltedPhoques
{
    void* RpmallocAllocator::Allocate(const size_t aSize) noexcept
    {
        return rpmalloc(aSize);
    }

    void RpmallocAllocator::Free(void* apData) noexcept
    {
        rpfree(apData);
    }

    size_t RpmallocAllocator::Size(void* apData) noexcept
    {
        if (apData == nullptr) return 0;

        return rpmalloc_usable_size(apData);
    }

    void* RpmallocAllocator::AlignedAllocate(size_t aSize, size_t aAlignment) noexcept
    {
        return rpaligned_alloc(aAlignment, aSize);
    }

    void RpmallocAllocator::AlignedFree(void* apData) noexcept
    {
        rpfree(apData);
    }
}
