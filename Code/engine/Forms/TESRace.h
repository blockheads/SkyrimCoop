#pragma once

#include <Forms/TESForm.h>
#include <Components/TESFullName.h>

struct TESRace : TESForm, TESFullName
{
    // RACE_DATA::Flag (CommonLibSSE-NG)
    static constexpr uint32_t kFaceGenHead = 1 << 1;
    static constexpr uint32_t kSizeExtraLarge = 3;
    static constexpr uint32_t kUseHeadTrackAnims = 1u << 15; // RACE_DATA::Flag

    uint8_t pad30[0xE8 - 0x30]; // TESDescription ... BGSAttackDataForm, then RACE_DATA at 0xE8
    uint8_t raceDataPad[0x20];
    uint32_t raceFlags; // RACE_DATA::flags
    uint8_t pad10C[0x128 - 0x10C];
    uint32_t size; // RACE_DATA::raceSize: 0 small .. 3 extra large
};

static_assert(offsetof(TESRace, raceFlags) == 0x108);
static_assert(offsetof(TESRace, size) == 0x128);
