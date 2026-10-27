#pragma once

#include <Papyrus/PartyRules.h>

// What NativeReach reads from the progression natives' code (its engine table, NativeReach.cpp; the reading itself:
// code_reach_tests.cpp), and the router's answer for one (PartyRules::RouteOf).
namespace Routes
{
using namespace PartyRules;

// Rewards, any script's but a body's: TeachWord, UnlockWord, IncrementSkill(By), AddPerkPoints, AddPerk, RemovePerk,
// ModActorValue and ForceActorValue (the permanent modifier).
inline constexpr Reach kReward{Call::kProgress, {.AnyScript = true}};
// AdvanceSkill: the story's only (a persuade's is its picker's).
inline constexpr Reach kStory{Call::kProgress, {}};
// AddSpell, RemoveSpell, SetFactionRank (and AddToFaction), RemoveFromFaction: the story's, kept on a guest's
// character.
inline constexpr Reach kStoryOnCopy{Call::kProgress, {.OnCopy = true}};

inline Routing ProgressRoute(Reach aReach, Target aTarget, uint32_t aCause, bool aStory = false, bool aBody = false,
                             bool aHosting = true)
{
    return RouteOf({.Reach = aReach,
                    .Target = aTarget,
                    .TargetId = aTarget == Target::kThisPlayer ? kThisPlayerFormId : aCause,
                    .Cause = aCause,
                    .Story = aStory,
                    .Body = aBody,
                    .Hosting = aHosting});
}

inline bool Relays(const Routing& acRouting)
{
    return acRouting.To == Destination::kEveryGame;
}
} // namespace Routes
