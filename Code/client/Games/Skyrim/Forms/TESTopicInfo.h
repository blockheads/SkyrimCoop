#pragma once

#include "TESForm.h"

struct TESTopic;

// One line of a topic (CommonLibSSE-NG TESTopicInfo): said when its conditions pass.
struct TESTopicInfo : TESForm
{
    enum : uint16_t
    {
        kSayOnce = 1 << 2,      // TOPIC_INFO_DATA::kSayOnce
        kInfoRefusal = 1 << 4,  // TOPIC_INFO_DATA::kInfoRefusal: never marked said
    };

    TESTopic* parentTopic;   // 20
    TESTopicInfo* dataInfo;  // 28
    void* objConditions;     // 30, TESCondition
    uint16_t infoIndex;      // 38
    bool saidOnce;           // 3A, said here (a kSayOnce line is not said again)
    uint8_t favorLevel;      // 3B
    uint16_t flags;          // 3C, TOPIC_INFO_DATA
    uint16_t timeUntilReset; // 3E, not 0: not said again to the player within its reset hours

    // Marks it said, as TESTopicInfo::Begin (25616) does when it is said (with the change flag the save keeps).
    void MarkSaid() noexcept
    {
        if (flags & kInfoRefusal)
            return;
        saidOnce = true;
        MarkChanged(0x80000000);
    }

    // Whether this game could say it for the topic: one of its lines, not a say-once line said already.
    [[nodiscard]] bool CanSayFor(const TESTopic* apTopic) const noexcept
    {
        return parentTopic == apTopic && !((flags & kSayOnce) && saidOnce);
    }
};

static_assert(offsetof(TESTopicInfo, parentTopic) == 0x20);
static_assert(offsetof(TESTopicInfo, saidOnce) == 0x3A);
static_assert(offsetof(TESTopicInfo, flags) == 0x3C);
static_assert(offsetof(TESTopicInfo, timeUntilReset) == 0x3E);
