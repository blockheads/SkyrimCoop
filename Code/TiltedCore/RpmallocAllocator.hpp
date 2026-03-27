#pragma once

#include "Allocator.hpp"

namespace TiltedPhoques
{
    struct RpmallocAllocator : Allocator
    {
        RpmallocAllocator() noexcept = default;
        virtual ~RpmallocAllocator() = default;

        TP_NOCOPYMOVE(RpmallocAllocator);

        [[nodiscard]] void* Allocate(size_t aSize) noexcept override;
        void Free(void* apData) noexcept override;
        [[nodiscard]] size_t Size(void* apData) noexcept override;

        [[nodiscard]] static void* AlignedAllocate(size_t aSize, size_t aAlignment) noexcept;
        static void AlignedFree(void* apData) noexcept;
    };
}
