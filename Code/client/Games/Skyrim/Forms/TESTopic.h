#pragma once

#include "TESForm.h"

struct TESObjectREFR;
struct TESQuest;
struct TESTopicInfo;

// A dialogue topic (CommonLibSSE-NG TESTopic): only what building a menu option needs.
struct TESTopic : TESForm
{
    // The line the speaker says to the player for it, as AddTopic (35303) asks (25544): its quest running, the
    // first line whose conditions pass (a random one of them for a random topic), none said within its reset hours.
    TESTopicInfo* LineFor(TESObjectREFR* apSpeaker, TESObjectREFR* apPlayer) noexcept;

    uint8_t unk20[0x40 - 0x20]; // 20, TESFullName, data, branch
    TESQuest* ownerQuest;       // 40
    TESTopicInfo** topicInfos;  // 48, its lines
    uint32_t numTopicInfos;     // 50
};

static_assert(offsetof(TESTopic, ownerQuest) == 0x40);
static_assert(offsetof(TESTopic, topicInfos) == 0x48);
static_assert(offsetof(TESTopic, numTopicInfos) == 0x50);
