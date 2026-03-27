#pragma once

template <class T> struct hkbVariableValueSet
{
    virtual ~hkbVariableValueSet();

    uint8_t pad8[0x8]; // 8
    T* data;           // 10
    uint32_t size;     // 18
};

SKYRIM_STRUCT_ASSERT(offsetof(hkbVariableValueSet<int>, data) == 0x10);
SKYRIM_STRUCT_ASSERT(offsetof(hkbVariableValueSet<int>, size) == 0x18);
