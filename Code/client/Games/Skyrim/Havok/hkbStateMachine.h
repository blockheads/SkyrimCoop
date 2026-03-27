#pragma once

struct hkbStateMachine
{
    virtual ~hkbStateMachine();

    uint8_t pad8[0x38 - 0x8];
    char* name;
};

SKYRIM_STRUCT_ASSERT(offsetof(hkbStateMachine, name) == 0x38);
