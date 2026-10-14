#pragma once

#include <Misc/BSFixedString.h>

struct TESObjectREFR;

// An event a behavior graph sends its holder (an annotation or a state's event: "weaponSwing",
// "KillActor", "PairEnd"), through BShkbAnimationGraph::animationGraphEvents (CommonLibSSE-NG).
struct BSAnimationGraphEvent
{
    BSFixedString tag;     // 00
    TESObjectREFR* holder; // 08
    BSFixedString payload; // 10
};

static_assert(sizeof(BSAnimationGraphEvent) == 0x18);
