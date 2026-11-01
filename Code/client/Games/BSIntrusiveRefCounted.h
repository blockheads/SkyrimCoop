#pragma once

#include <cstdint>

struct alignas(sizeof(void*)) BSIntrusiveRefCounted
{
    volatile int32_t refCount;
};

static_assert(sizeof(BSIntrusiveRefCounted) == sizeof(void*));
