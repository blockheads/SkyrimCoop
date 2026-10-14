#pragma once

#include <Components/TESCondition.h>
#include <Forms/TESForm.h>
#include <Misc/BSString.h>

struct NiFormArray;

// An idle animation in the idle tree (IDLE, CommonLibSSE-NG TESIdleForm): picked when its conditions and its
// parents' hold (docs/re/systems/conditions.md).
struct TESIdleForm : TESForm
{
    enum Flags : uint8_t
    {
        kParent = 1 << 0,
        kSequence = 1 << 1,
        kNoAttacking = 1 << 2,
        kBlocking = 1 << 3,
    };

    TESCondition conditions;         // 20
    int8_t loopMin;                  // 28 (DATA)
    int8_t loopMax;                  // 29
    uint8_t flags;                   // 2A, Flags
    uint8_t animationGroupSelection; // 2B
    uint16_t replayDelay;            // 2C
    uint16_t pad2E;
    NiFormArray* childIdles;     // 30
    TESIdleForm* parentIdle;     // 38 (ANAM)
    TESIdleForm* prevIdle;       // 40 (ANAM)
    BSFixedString animFileName;  // 48 (DNAM)
    BSFixedString animEventName; // 50 (ENAM)
    BSString formEditorID;       // 58 (EDID)
};
SKYRIM_STRUCT_ASSERT(offsetof(TESIdleForm, conditions) == 0x20);
SKYRIM_STRUCT_ASSERT(offsetof(TESIdleForm, replayDelay) == 0x2C);
SKYRIM_STRUCT_ASSERT(offsetof(TESIdleForm, parentIdle) == 0x38);
SKYRIM_STRUCT_ASSERT(offsetof(TESIdleForm, animEventName) == 0x50);
SKYRIM_STRUCT_ASSERT(sizeof(TESIdleForm) == 0x68);
