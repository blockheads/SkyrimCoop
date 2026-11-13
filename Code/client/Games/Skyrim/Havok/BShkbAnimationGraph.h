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
    virtual void SetWorld(void* apWorld);  // 04: hkWorldM, the physics world it steps with
    virtual void ResetRagdoll();           // 05

    volatile LONG refCount; // 08, BSIntrusiveRefCounted
    uint8_t padC[0x68 - 0xC];
    // The graph's events (BSTEventSource<BSAnimationGraphEvent>): what the graph tells its holder.
    EventDispatcher<BSAnimationGraphEvent> animationGraphEvents; // 68
    hkbCharacter character; // C0
    uint8_t pad160[0x1E8 - (0xC0 + sizeof(hkbCharacter))];
    float interpCountdown[2]; // 1E8: a far graph's time until its next step (63587); -FLT_MAX unset
    const char* projectName; // 1F0, BSFixedString: "DefaultMale", "FirstPerson" (CommonLib)
    void* unk1F8;
    BShkbHkxDB* hkxDB;
    hkbBehaviorGraph* behaviorGraph; // 208
    Actor* holder;                   // 210 (CommonLib; Open Animation Replacer reads it from a graph's hkbCharacter)
    uint8_t pad218[0x238 - 0x218];
    bhkWorldM* hkWorldM; // 238 (CommonLib physicsWorld): null, its ragdoll update (63562) does nothing
    uint16_t lod; // 240: its pose's level of detail, set each update (63587: the maximum when forced, else 63575)
    uint8_t pad242[0x246 - 0x242];
    bool behaviourUpdated; // 246: it updated this frame (63587), so 63589 runs its generate pass (63568)

    // Re do not use
    bool ReSendEvent(BSFixedString* apEventName);
};

static_assert(offsetof(BShkbAnimationGraph, refCount) == 0x8);
static_assert(offsetof(BShkbAnimationGraph, animationGraphEvents) == 0x68);
static_assert(offsetof(BShkbAnimationGraph, character) == 0xC0);
static_assert(offsetof(BShkbAnimationGraph, projectName) == 0x1F0);
static_assert(offsetof(BShkbAnimationGraph, behaviorGraph) == 0x208);
static_assert(offsetof(BShkbAnimationGraph, holder) == 0x210);
static_assert(offsetof(BShkbAnimationGraph, hkWorldM) == 0x238);
static_assert(offsetof(BShkbAnimationGraph, interpCountdown) == 0x1E8);
static_assert(offsetof(BShkbAnimationGraph, lod) == 0x240);
static_assert(offsetof(BShkbAnimationGraph, behaviourUpdated) == 0x246);
