#pragma once

#include "TESForm.h"

struct TESTopic;

// One line of a topic (CommonLibSSE-NG TESTopicInfo): said when its conditions pass.
struct TESTopicInfo : TESForm
{
    enum : uint16_t
    {
        kSayOnce = 1 << 2, // TOPIC_INFO_DATA::kSayOnce
    };

    TESTopic* parentTopic;   // 20
    TESTopicInfo* dataInfo;  // 28
    void* objConditions;     // 30, TESCondition
    uint16_t infoIndex;      // 38
    bool saidOnce;           // 3A, said here (a kSayOnce line is not said again)
    uint8_t favorLevel;      // 3B
    uint16_t flags;          // 3C, TOPIC_INFO_DATA
    uint16_t timeUntilReset; // 3E

    // Whether this game could say it for the topic: one of its lines, not a say-once line said already.
    [[nodiscard]] bool CanSayFor(const TESTopic* apTopic) const noexcept
    {
        return parentTopic == apTopic && !((flags & kSayOnce) && saidOnce);
    }
};

static_assert(offsetof(TESTopicInfo, parentTopic) == 0x20);
static_assert(offsetof(TESTopicInfo, saidOnce) == 0x3A);
static_assert(offsetof(TESTopicInfo, flags) == 0x3C);
