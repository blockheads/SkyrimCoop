#pragma once

#include <cstdint>
#include <string_view>

namespace ReplayState
{
constexpr uint32_t kWeaponMask = 7u << 5;

constexpr uint32_t kKnockMask = 7u << 25;

// A copy's actor state with its owner's action (sneaking, sitting, life state): its owner's, its own ragdoll kept.
constexpr uint32_t Flags1(uint32_t aOwner, uint32_t aCopy) noexcept
{
    return (aOwner & ~kKnockMask) | (aCopy & kKnockMask);
}

constexpr uint32_t Flags2(uint32_t aOwner, uint32_t aCopy) noexcept
{
    return (aOwner & ~kWeaponMask) | (aCopy & kWeaponMask);
}

// ActorState1::sitSleepState's wants-to-stand: from an exit action until its clip's own exit event (42835).
constexpr uint8_t kSitWantsToStand = 4;

// A copy on its own body, off its owner's stream: in a pair's instance or a kill move, getting on its mount
// (GETTING_ON_OFF_MOUNT, to 42881) or off it (no Actor::Dismount sets that on a copy: its sit state). Knocked down
// it stays on its stream: its get-up's pick is its owner's (GraphPick).
constexpr bool CopyOwnBody(bool aInPair, bool aInKillMove, bool aGettingOnOff, uint8_t aSitState) noexcept
{
    return aInPair || aInKillMove || aGettingOnOff || aSitState == kSitWantsToStand;
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

enum class PairOutcome
{
    kPlay,  // the partner here is free
    kSkip,  // the pair's other half came back, the partner is paired elsewhere, or this game's pair swung first
    kYield, // crossed, and the replayed pair swung first: this game ends its own pair, then plays that one
};

struct PairedReplay
{
    std::string_view EventName;  // the replayed action's
    uint32_t CopyId;             // the copy replaying it
    bool PartnerInKillMove;      // its partner, this game's own actor
    std::string_view PairEvent;  // the partner's pair: its target's event
    uint32_t PairTargetId;       // and its target
    uint64_t Tick, PairTick;     // when each pair was swung, on the session's clock
    uint32_t Session, PairSession; // each attacker's session id: same tick, the lower wins
};

// The engine takes a pair on two actors already paired as the same pair and ends both clips at once (32803). Both games
// see the same ticks, so they agree who swung first with no message (docs/re/systems/kill-moves.md).
constexpr PairOutcome SettlePairedReplay(const PairedReplay& acReplay) noexcept
{
    if (!acReplay.PartnerInKillMove)
        return PairOutcome::kPlay;
    if (acReplay.PairEvent == acReplay.EventName || acReplay.PairTargetId != acReplay.CopyId)
        return PairOutcome::kSkip;
    const bool cReplayedFirst = acReplay.Tick != acReplay.PairTick ? acReplay.Tick < acReplay.PairTick : acReplay.Session < acReplay.PairSession;
    return cReplayedFirst ? PairOutcome::kYield : PairOutcome::kSkip;
}
// An accepted action carries its owner's whole graph state for a copy that refuses it (ReplayGraph::Recover). A pair's
// halves never do. A streamed owner's copies take no actions, but while they are on their own bodies (aOwnBody: the
// owner's own CopyOwnBody, its copies' follows it): then they play its actions (graph-input-stream.md, Cost measured).
constexpr bool CarriesGraphState(bool aPaired, bool aStreamed, bool aOwnBody) noexcept
{
    return !aPaired && (!aStreamed || aOwnBody);
}
} // namespace ReplayState
