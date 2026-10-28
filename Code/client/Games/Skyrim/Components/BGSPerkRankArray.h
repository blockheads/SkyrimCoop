#pragma once

#include <Components/BaseFormComponent.h>

struct BGSPerk;
struct PerkRankData
{
    BGSPerk* perk{};
    uint8_t rank{};
    uint8_t pad09[7]{};
};
static_assert(sizeof(PerkRankData) == 0x10);
static_assert(offsetof(PerkRankData, rank) == 0x08);

struct BGSPerkRankArray : BaseFormComponent
{
    PerkRankData* perkRanks;
    uint32_t count;
};

static_assert(offsetof(BGSPerkRankArray, perkRanks) == 0x08);
static_assert(offsetof(BGSPerkRankArray, count) == 0x10);
static_assert(sizeof(BGSPerkRankArray) == 0x18);
