#pragma once

#include <animation/PoseBlend.h>
#include <Havok/hkbCharacter.h>

#include <algorithm>
#include <span>
#include <Events/EventDispatcher.h>
#include <Animation/BSAnimationGraphEvent.h>

struct BSFixedString;
struct BShkbHkxDB;
struct hkbBehaviorGraph;
struct bhkWorldM;
struct Actor;

// A generator output's tracks (hkbGeneratorOutput::Tracks): headers of 16 bytes from +0x10. Standard tracks: 2 pose,
// 4 rigid body ragdoll controls, 6 powered ragdoll controls, 8 keyframed ragdoll bones, 9 keyframe targets (58291, 63562).
struct hkbGeneratorOutputTracks
{
    struct Header
    {
        int16_t capacity;
        int16_t numData; // 2
        int16_t dataOffset;
        int16_t elementSizeBytes;
        float onFraction; // 8
        uint8_t flags;
        uint8_t type;
        uint8_t padE[2];
    };
    int32_t capacity;
    int32_t numTracks; // 4
    uint8_t pad8[8];
    Header headers[1]; // 10, numTracks of them
};
static_assert(sizeof(hkbGeneratorOutputTracks::Header) == 0x10);
static_assert(offsetof(hkbGeneratorOutputTracks, headers) == 0x10);

struct hkbGeneratorOutput
{
    hkbGeneratorOutputTracks* tracks; // 00

    // Its pose track (2): a bone transform (hkQsTransform, 48 bytes) for each bone posed, as 63581/63582 read it.
    [[nodiscard]] std::span<PoseBone> Pose() const noexcept
    {
        if (!tracks || tracks->numTracks <= 2)
            return {};
        const auto& cHeader = tracks->headers[2];
        auto* pBones = reinterpret_cast<PoseBone*>(reinterpret_cast<uint8_t*>(tracks) + cHeader.dataOffset);
        return {pBones, static_cast<size_t>(std::max<int16_t>(cHeader.numData, 0))};
    }
};

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
    uint8_t pad218[0x220 - 0x218];
    struct hkbGeneratorOutput* generatorOutputs[2]; // 220 (CommonLib): [0] the pose its ragdoll passes read (63562, 63563)
    uint8_t pad230[0x238 - 0x230];
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
static_assert(offsetof(BShkbAnimationGraph, generatorOutputs) == 0x220);
static_assert(offsetof(BShkbAnimationGraph, interpCountdown) == 0x1E8);
static_assert(offsetof(BShkbAnimationGraph, lod) == 0x240);
static_assert(offsetof(BShkbAnimationGraph, behaviourUpdated) == 0x246);
