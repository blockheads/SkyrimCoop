#pragma once

#include <Havok/hkbCharacter.h>
#include <Events/EventDispatcher.h>
#include <Games/Animation/BSAnimationGraphEvent.h>

struct BSFixedString;
struct BShkbHkxDB;
struct hkbBehaviorGraph;
struct bhkWorldM;
struct Actor;

struct BShkbAnimationGraph
{
    virtual ~BShkbAnimationGraph(){};
    // BSIRagdollDriver (CommonLibSSE-NG; 63620-63622 on 1.6.1170).
    virtual bool HasRagdoll();             // 01
    virtual bool AddRagdollToWorld();      // 02
    virtual bool RemoveRagdollFromWorld(); // 03

    volatile LONG refCount; // 08, BSIntrusiveRefCounted
    uint8_t padC[0x68 - 0xC];
    // The graph's events (BSTEventSource<BSAnimationGraphEvent>): what the graph tells its holder.
    EventDispatcher<BSAnimationGraphEvent> animationGraphEvents; // 68
    hkbCharacter character; // C0
    uint8_t pad160[0x1F0 - (0xC0 + sizeof(hkbCharacter))];
    const char* projectName; // 1F0, BSFixedString: "DefaultMale", "FirstPerson" (CommonLib)
    void* unk1F8;
    BShkbHkxDB* hkxDB;
    hkbBehaviorGraph* behaviorGraph; // 208
    Actor* holder;                   // 210 (CommonLib; Open Animation Replacer reads it from a graph's hkbCharacter)
    uint8_t pad218[0x238 - 0x218];
    bhkWorldM* hkWorldM; // 238

    // Re do not use
    bool ReSendEvent(BSFixedString* apEventName);
};

static_assert(offsetof(BShkbAnimationGraph, refCount) == 0x8);
static_assert(offsetof(BShkbAnimationGraph, animationGraphEvents) == 0x68);
static_assert(offsetof(BShkbAnimationGraph, character) == 0xC0);
static_assert(offsetof(BShkbAnimationGraph, projectName) == 0x1F0);
static_assert(offsetof(BShkbAnimationGraph, behaviorGraph) == 0x208);
static_assert(offsetof(BShkbAnimationGraph, holder) == 0x210);
