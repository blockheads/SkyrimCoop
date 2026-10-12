#pragma once

#include <Havok/hkbCharacter.h>

struct BSFixedString;
struct BShkbHkxDB;
struct hkbBehaviorGraph;
struct bhkWorldM;

struct BShkbAnimationGraph
{
    virtual ~BShkbAnimationGraph(){};

    uint8_t pad8[0xC0 - 0x8];
    hkbCharacter character;
    uint8_t pad160[0x1F0 - (0xC0 + sizeof(hkbCharacter))];
    const char* projectName; // 1F0, BSFixedString: "DefaultMale", "FirstPerson" (CommonLib)
    void* unk1F8;
    BShkbHkxDB* hkxDB;
    hkbBehaviorGraph* behaviorGraph; // 208
    uint8_t pad210[0x238 - 0x210];
    bhkWorldM* hkWorldM; // 238

    // Re do not use
    bool ReSendEvent(BSFixedString* apEventName);
};

SKYRIM_STRUCT_ASSERT(offsetof(BShkbAnimationGraph, projectName) == 0x1F0);
SKYRIM_STRUCT_ASSERT(offsetof(BShkbAnimationGraph, behaviorGraph) == 0x208);
