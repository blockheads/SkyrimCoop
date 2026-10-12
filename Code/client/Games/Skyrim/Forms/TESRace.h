#pragma once

#include <Forms/TESForm.h>
#include <Components/TESFullName.h>

struct TESRace : TESForm, TESFullName
{
    // RACE_DATA::Flag (CommonLibSSE-NG)
    static constexpr uint32_t kFaceGenHead = 1 << 1;

    uint8_t pad30[0xE8 - 0x30]; // TESDescription ... BGSAttackDataForm, then RACE_DATA at 0xE8
    uint8_t raceDataPad[0x20];
    uint32_t raceFlags; // RACE_DATA::flags
};

SKYRIM_STRUCT_ASSERT(offsetof(TESRace, raceFlags) == 0x108);
