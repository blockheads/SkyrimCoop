#pragma once

#include <Havok/hkbNode.h>

struct hkbCharacter;

struct hkbContext
{
    hkbCharacter* character; // 00
};

// One actor's half of a paired animation (a kill move); CommonLibSSE-NG, checked in IDA (Update 63050).
struct BSSynchronizedClipGenerator : hkbNode
{
    // Its scene's answers, asked each update (63050): a pair's instance, or the default scene's always yes (63048).
    struct Scene
    {
        virtual ~Scene() = default;      // 00
        virtual void Unused08() {}
        virtual void Unused10() {}
        virtual bool AllInScene() = 0;   // 18
        virtual bool AllAtMarks() = 0;   // 20
    };

    uint8_t pad48[0x6C - 0x48];
    bool leadCharacter;       // 6C
    bool reorientSupportChar; // 6D
    uint8_t pad6E[0x70 - 0x6E];
    Scene* scene; // 70
    uint8_t pad78[0x12A - 0x78];
    bool atMark; // 12A, doneReorientingSupportChar: the pair starts once every half is at its mark (32794)
    bool allInScene; // 12B, its scene's answers at its last update
    bool allAtMarks; // 12C: its inner clip advances only while set
    uint8_t pad12D[0x130 - 0x12D];
};
static_assert(offsetof(BSSynchronizedClipGenerator, leadCharacter) == 0x6C);
static_assert(offsetof(BSSynchronizedClipGenerator, scene) == 0x70);
static_assert(offsetof(BSSynchronizedClipGenerator, atMark) == 0x12A);
static_assert(offsetof(BSSynchronizedClipGenerator, allAtMarks) == 0x12C);
static_assert(sizeof(BSSynchronizedClipGenerator) == 0x130);
