#pragma once

#include <cstddef>
#include <cstdint>

// CommonLibSSE NiUpdateData, NiAVObject::Update's argument (70251).
struct NiUpdateData
{
    enum : uint32_t
    {
        kDirty = 1
    };
    float time{};
    uint32_t flags{};
};
static_assert(sizeof(NiUpdateData) == 0x8);
static_assert(offsetof(NiUpdateData, time) == 0x0);
static_assert(offsetof(NiUpdateData, flags) == 0x4);
