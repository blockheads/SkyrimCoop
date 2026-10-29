#pragma once

#include <cstddef>
#include <cstdint>

// A behavior graph variable's role and type (Havok hkbVariableInfo, CommonLibSSE-NG RE::hkbVariableInfo).
struct hkbVariableInfo
{
    enum Type : int8_t
    {
        kBool = 0,
        kInt8 = 1,
        kInt16 = 2,
        kInt32 = 3,
        kReal = 4,
        kPointer = 5,
        kVector3 = 6,
        kVector4 = 7,
        kQuaternion = 8,
    };

    int16_t role;
    int16_t roleFlags;
    Type type;
};

static_assert(sizeof(hkbVariableInfo) == 0x6);
static_assert(offsetof(hkbVariableInfo, type) == 0x4);
