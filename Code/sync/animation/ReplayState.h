#pragma once

#include <animation/KnockReplay.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace ReplayState
{
// ActorState1::sitSleepState's wants-to-stand: from an exit action until its clip's own exit event (42835).
constexpr uint8_t kSitWantsToStand = 4;

// A copy held off its stream for a pair with this game's actor (PairOnStream) stays on its own body until its half
// has played out: out of the pair's instance and its kill move, on its mount (GETTING_ON_OFF_MOUNT, to 42881) or off
// it (no Actor::Dismount sets that on a copy: its sit state).
constexpr bool CopyOwnBody(bool aInPair, bool aInKillMove, bool aGettingOnOff, uint8_t aSitState) noexcept
{
    return aInPair || aInKillMove || aGettingOnOff || aSitState == kSitWantsToStand;
}

// A copy's bodies take its owner's layer (seated, mounted: SetCopyPhysics) unless they are its own ragdoll's: lying in
// it or getting up (its knock state's: KnockReplay), a kill move's victim state's (AddRagdollHandler 42873;
// physics.md), or its death's: dead, or holding its pose for its owner's death (PoseHeld).
constexpr bool TakesOwnerBodies(bool aLiesInRagdoll, uint8_t aKnock, bool aInKillMove, bool aDying) noexcept
{
    return !aLiesInRagdoll && !KnockReplay::BodiesSimulated(aKnock) && !aInKillMove && !aDying;
}

// A copy's owner's pair: on its stream with a copy partner here, but a taken one, which has no stream to replay
// (kill-moves.md, "Claimed pairs"). A partner this game's own, or not here: its half is its own body's.
constexpr bool PairOnStream(bool aPartnerIsCopy, bool aVictimTaken = false) noexcept
{
    return aPartnerIsCopy && !aVictimTaken;
}

// Playing a "pa_Kill..." action puts its actor and its target in a kill move (39004: EnterKillMove 38613 on each);
// two copies that take the pair from their streams play no action, so it is done for them at the action, both in
// one frame. A copy made later enters from its record (ActorStateService).
constexpr bool StartsWithNoCase(std::string_view acText, std::string_view acLowerPrefix) noexcept
{
    if (acText.size() < acLowerPrefix.size())
        return false;
    for (size_t i = 0; i < acLowerPrefix.size(); ++i)
        if ((acText[i] | 0x20) != acLowerPrefix[i] && acText[i] != acLowerPrefix[i])
            return false;
    return true;
}

constexpr bool EntersKillMove(std::string_view acEvent) noexcept
{
    return StartsWithNoCase(acEvent, "pa_kill");
}

// ActorState1::sitSleepState's seated (PairedStopHandler 42868 at the get-on's end).
constexpr uint8_t kSitSeated = 3;

// ActorState1::sitSleepState's getting on: from HorseEnter (39004) until its pair's end seats it (42868).
constexpr uint8_t kSitGettingOn = 2;

// An actor handed to this game (a mount, a claimed kill move's victim) is taken once its copy has run every op of its
// last owner's through the hand-back's tick: its pair's PairEnd among them (only the instance sends it, 32797). Off
// its stream, or tick 0 (its owner left, or recorded nothing): at once. docs/re/systems/mounts.md
constexpr bool TakesOverMount(uint64_t aTick, bool aOnStream, uint64_t aDoneThrough) noexcept
{
    return !aTick || !aOnStream || aDoneThrough >= aTick;
}

// A copy takes the engine's position writes (SetPosition 19790) while stepped, or seated on a mount: its
// REFRSyncController (20318) keeps its reference on the mount (player-mount.md).
constexpr bool CopyTakesEnginePlace(bool aStepped, bool aLinkedToMount, uint8_t aSitState) noexcept
{
    return aStepped || (aLinkedToMount && aSitState == kSitSeated);
}

// A copy linked to a mount, not seated yet (sit state 0 or 2): its capsule collides with nothing, as the mount
// package's kGettingOnOffMount does for an owner (player-mount.md).
constexpr bool GettingOnMount(bool aLinked, uint8_t aSitState) noexcept
{
    return aLinked && (aSitState == 0 || aSitState == kSitGettingOn);
}

// A pair starts once each supporting actor can sweep to its mark (32792), judged on this game's positions.
// A replayed pair was judged on its owner's game (docs/re/systems/kill-moves.md).
constexpr bool PairMarkClear(bool aMoved, bool aReplayed) noexcept
{
    return aMoved || aReplayed;
}

// A copy in a pair keeps it: its owner's plain action was sent before its game heard of the pair, and would take the
// copy out of the clip, ending the pair before the blow (docs/re/systems/kill-moves.md).
constexpr bool HeldByPair(bool aCopyInKillMove, bool aPaired) noexcept
{
    return aCopyInKillMove && !aPaired;
}

// A yielded pair: a clip behind an uninterruptible transition (pa_KillMoveI: 0xd04) ignores pairedStop until its blend
// ends, so it is sent again while that clip plays; a new kill move is left alone.
constexpr uint8_t cClashStopUpdates = 30;
constexpr uint64_t cClashGuardMs = 5000; // past the longest kill clip

constexpr bool ResendPairStop(uint8_t aStopsLeft, bool aInKillMove, bool aInClip) noexcept
{
    return aStopsLeft && !aInKillMove && aInClip;
}

// A blow from a yielded clip: its hitter's kill move yielded and it has started no attack since, whatever half it plays
// now (HitFrame and PairEnd can share a frame). Its own new swing or pair ends this.
constexpr bool CancelledBlow(bool aInClashWindow) noexcept
{
    return aInClashWindow;
}

constexpr bool StartsAttack(std::string_view acEvent) noexcept
{
    return acEvent.starts_with("attack") || acEvent.starts_with("pa_");
}

// A kill move's victim dies on any ragdoll contact (63092). Only when both halves are this game's.
constexpr bool ContactKills(bool aPairedWithCopy) noexcept
{
    return !aPairedWithCopy;
}

// A copy at its clip's KillActor does not die (deferred kill): its graph would blend to standing until its owner's death
// action, then its ragdoll would start from there. It holds that pose instead (docs/re/systems/kill-moves.md).
constexpr uint64_t cPoseHoldMs = 1500; // a death action that never comes: its owner lived

constexpr bool PoseHeld(uint64_t aNow, uint64_t aHeldUntil, bool aDead) noexcept
{
    return !aDead && aNow < aHeldUntil;
}

// A copy holding its pose takes its owner's death when heard, the actions queued before it dropped
// (kill-moves.md, "The claimer's victim waits for the host's word").
template <class TQueue, class TIsDying> bool TakeOverdueDeath(TQueue& aActions, bool aPoseHeld, TIsDying&& aIsDying)
{
    if (!aPoseHeld)
        return false;
    const auto cDeath = std::find_if(aActions.begin(), aActions.end(), aIsDying);
    if (cDeath == aActions.end())
        return false;
    aActions.erase(aActions.begin(), cDeath);
    return true;
}

// The queue's first action is played at its playback time, or now if it is the overdue death (that once only).
constexpr bool ActionDue(uint64_t aTick, uint64_t aPlayback, bool& aDeathNow) noexcept
{
    const bool cDue = aTick <= aPlayback || aDeathNow;
    aDeathNow = false;
    return cDue;
}

// A kill move on an actor of another game is claimed through the host (HostClaims; one-simulator.md piece 4b): what a
// game knows of the claim of the pair a copy replays.
enum class ClaimAnswer
{
    kNotNeeded, // no kill move: a mount's pair, a pair's other half
    kUnknown,   // the host's answer is still on its way
    kClaimed,
    kRefused,
};

enum class PairOutcome
{
    kPlay,  // the partner here is free
    kSkip,  // refused by the host, or the partner is in a pair already (this pair's other half came back)
    kYield, // the host gave the replayed pair the claim over this game's own: it ends its own pair, then plays that one
    kWait,  // the host has not answered yet: the action stays first
};

// A replayed pair whose partner is this game's own player. The engine takes a pair on two actors already paired as the
// same pair and ends both clips at once (32803), so two crossed kill moves need one answer: the host's first claim.
constexpr PairOutcome SettlePairedReplay(bool aPartnerInKillMove, ClaimAnswer aClaim) noexcept
{
    if (aClaim == ClaimAnswer::kRefused)
        return PairOutcome::kSkip;
    if (aClaim == ClaimAnswer::kUnknown)
        return PairOutcome::kWait;
    if (!aPartnerInKillMove)
        return PairOutcome::kPlay;
    return aClaim == ClaimAnswer::kClaimed ? PairOutcome::kYield : PairOutcome::kSkip;
}

// A kill move replayed on any game by its claim: refused, its claimer ended it (every game skips it); claimed away
// (ClaimedAway) and not answered yet, it waits; else on.
constexpr PairOutcome ClaimOutcome(ClaimAnswer aClaim, bool aClaimedAway) noexcept
{
    if (aClaim == ClaimAnswer::kRefused)
        return PairOutcome::kSkip;
    return aClaimedAway && aClaim == ClaimAnswer::kUnknown ? PairOutcome::kWait : PairOutcome::kPlay;
}

// A kill move replayed on an NPC this game owns is never paired here: its claim makes the NPC the claimer's game's
// for the pair (a copy here, both on their streams), or is refused and ended there (kill-moves.md).
constexpr bool ClaimedAway(bool aKillMove, bool aPartnerIsCopy, bool aPartnerIsPlayer) noexcept
{
    return aKillMove && !aPartnerIsCopy && !aPartnerIsPlayer;
}

// A claimed kill move's NPC partner, a copy here: taken by the claimer's game (HostClaims).
constexpr bool TakenVictim(bool aKillMove, ClaimAnswer aClaim, bool aPartnerIsCopy, bool aPartnerIsPlayer) noexcept
{
    return aKillMove && aClaim == ClaimAnswer::kClaimed && aPartnerIsCopy && !aPartnerIsPlayer;
}

// A copy's own action is refused (its owner's game plays it) but for: one we force, its half of the local player's
// pair, or its half of a pair this game replays between two copies (a taken NPC's: 32803 plays it through Func5 41557,
// and refused, the pair fails). Those two are taken and never sent.
constexpr bool CopyTakesAction(bool aForced, bool aPlayersPartner, bool aReplayedPairHalf) noexcept
{
    return aForced || aPlayersPartner || aReplayedPairHalf;
}

// A replayed pair makes an instance here (32803, its target's half played by it) with a partner of this game's own
// that takes it, or a taken one: no mount (its owner played its half), no NPC claimed away.
constexpr bool PairsHere(bool aPartnerIsCopy, bool aPartnerIsMount, bool aPartnerIsPlayer, bool aKillMove, bool aVictimTaken = false) noexcept
{
    return (!aPartnerIsCopy || aVictimTaken) && !aPartnerIsMount && !ClaimedAway(aKillMove, aPartnerIsCopy, aPartnerIsPlayer);
}

// A claimed kill move is done on its claimer's game: a taken victim killed, or out of its pair with no kill queued
// (37316's task, kill-moves.md); a player partner's once the claimer's own actor left its kill move.
constexpr bool PairDone(bool aTaken, bool aKilled, bool aKillQueued, bool aPartnerPaired, bool aActorPaired) noexcept
{
    return aTaken ? aKilled || (!aPartnerPaired && !aKillQueued) : !aActorPaired;
}

// An accepted action carries its owner's whole graph state for a copy that refuses it (ReplayGraph::Recover). A pair's
// halves never do. A streamed owner's copies take no actions, but while they are on their own bodies (aOwnBody: the
// owner's own CopyOwnBody, its copies' follows it): then they play its actions (graph-input-stream.md, Cost measured).
constexpr bool CarriesGraphState(bool aPaired, bool aStreamed, bool aOwnBody) noexcept
{
    return !aPaired && (!aStreamed || aOwnBody);
}
} // namespace ReplayState
