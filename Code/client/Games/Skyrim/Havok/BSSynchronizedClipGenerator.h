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
    uint8_t pad48[0x6C - 0x48];
    bool leadCharacter;       // 6C
    bool reorientSupportChar; // 6D
    uint8_t pad6E[0x12A - 0x6E];
    bool atMark; // 12A, doneReorientingSupportChar: the pair starts once every half is at its mark (32794)
    uint8_t pad12B[0x130 - 0x12B];
};
static_assert(offsetof(BSSynchronizedClipGenerator, leadCharacter) == 0x6C);
static_assert(offsetof(BSSynchronizedClipGenerator, atMark) == 0x12A);
static_assert(sizeof(BSSynchronizedClipGenerator) == 0x130);
