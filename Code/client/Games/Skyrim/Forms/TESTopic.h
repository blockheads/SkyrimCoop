#pragma once

#include "TESForm.h"

struct TESQuest;
struct TESTopicInfo;

// A dialogue topic (CommonLibSSE-NG TESTopic): only what building a menu option needs.
struct TESTopic : TESForm
{
    uint8_t unk20[0x40 - 0x20]; // 20, TESFullName, data, branch
    TESQuest* ownerQuest;       // 40
    TESTopicInfo** topicInfos;  // 48, its lines
    uint32_t numTopicInfos;     // 50
};

static_assert(offsetof(TESTopic, ownerQuest) == 0x40);
static_assert(offsetof(TESTopic, topicInfos) == 0x48);
static_assert(offsetof(TESTopic, numTopicInfos) == 0x50);
