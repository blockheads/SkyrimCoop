#pragma once

#include "ExtraData.h"
#include <Misc/BSFixedString.h>
#include <Components/TESDescription.h>
#include <Components/TESFullName.h>
#include <Forms/TESForm.h>

struct BGSMessage : TESForm, TESFullName, TESDescription
{
    void* pIcon;
    void* pOwnerQuest;
    uint8_t menuButtons[0x10];
    uint32_t uiFlags;
    uint32_t uiDisplayTime;
};

static_assert(sizeof(BGSMessage) == 0x68);

struct ExtraTextDisplayData : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::TextDisplayData;
    // iOwnerInstance: a player's name (CommonLib's DisplayDataType::kCustomName).
    static constexpr int32_t kCustomName = -2;

    BSFixedString DisplayName{};
    BGSMessage* pDisplayNameText{};
    TESQuest* pOwnerQuest{};
    int32_t iOwnerInstance{};
    float fTemperFactor{};
    uint16_t usCustomNameLength{}; // the name without its temper suffix, for kCustomName

    // TODO: implement the rest when i dont feel lazy
    /*
    BGSMessage *pDisplayNameText;
    TESQuest *pOwnerQuest;
    uint32_t uiOwnerInstance;
    BSTArray<BSTTuple<BSFixedString,TESForm *>,BSTArrayHeapAllocator> *pTextPairA;
    unsigned __int16 usCustomNameLength;
    */
};

static_assert(sizeof(ExtraTextDisplayData) == 0x38);
static_assert(offsetof(ExtraTextDisplayData, DisplayName) == 0x10);
static_assert(offsetof(ExtraTextDisplayData, pOwnerQuest) == 0x20);
static_assert(offsetof(ExtraTextDisplayData, iOwnerInstance) == 0x28);
static_assert(offsetof(ExtraTextDisplayData, usCustomNameLength) == 0x30);
