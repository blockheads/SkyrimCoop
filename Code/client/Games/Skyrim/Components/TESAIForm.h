#pragma once

#include <Components/BaseFormComponent.h>
#include <Games/Primitives.h>

struct TESPackage;

struct TESAIForm : BaseFormComponent
{
    // Its confidence: cowardly, cautious, average, brave or foolhardy (TESAIForm 14457).
    uint8_t GetConfidence() const noexcept;

    uint32_t flags;
    uint8_t unkC[0xC];
    GameList<TESPackage> AIPackList;
};
