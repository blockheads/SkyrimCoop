#pragma once

#include <cstdint>
#include <string_view>

namespace ReplayState
{
constexpr uint32_t kWeaponMask = 7u << 5;

constexpr uint32_t Flags2(uint32_t aOwner, uint32_t aCopy) noexcept
{
    return (aOwner & ~kWeaponMask) | (aCopy & kWeaponMask);
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

// After crossed pairs: a clip behind an uninterruptible transition (pa_KillMoveI: 0xd04) ignores pairedStop until its blend
// ends, so it is sent again while that clip plays; a new kill move is left alone.
constexpr uint8_t cClashStopUpdates = 30;
constexpr uint64_t cClashGuardMs = 5000; // past the longest kill clip

constexpr bool ResendPairStop(uint8_t aStopsLeft, bool aInKillMove, bool aInClip) noexcept
{
    return aStopsLeft && !aInKillMove && aInClip;
}

// A blow from a cancelled clip: its hitter's kill move ended in a clash and it has started no attack since (HitFrame and
// PairEnd can share a frame, so bIsSynced is no guide). A new kill move's blow is not one.
constexpr bool CancelledBlow(bool aInClashWindow, bool aInKillMove) noexcept
{
    return aInClashWindow && !aInKillMove;
}

// Graph events that begin a fresh swing (attackStart, attackPowerStartLeft...); a kill move's are pa_.
constexpr bool StartsAttack(std::string_view acEvent) noexcept
{
    return acEvent.starts_with("attack");
}

// A kill move's victim dies on any ragdoll contact (63092). Only when both halves are this game's.
constexpr bool ContactKills(bool aPairedWithCopy) noexcept
{
    return !aPairedWithCopy;
}

struct PairedReplay
{
    std::string_view EventName;    // the replayed action's
    uint32_t CopyId;               // the copy replaying it
    bool LocalPartner;             // its partner is this game's own actor
    bool PartnerInKillMove;
    std::string_view PairEvent;    // the partner's pair: its target's event
    uint32_t PairTargetId;         // and its target
};

// The engine takes a pair on two actors already paired as the same pair and ends both clips at once (32803;
// docs/re/systems/kill-moves.md). True: the action is settled here. Crossed pairs end both (aEnd(copy), aEnd(partner));
// the pair's other half coming back, or a partner paired with someone else, changes nothing.
template <class End> bool SettlePairedReplay(const PairedReplay& acReplay, End&& aEnd)
{
    if (!acReplay.LocalPartner || !acReplay.PartnerInKillMove)
        return false;
    if (acReplay.PairEvent != acReplay.EventName && acReplay.PairTargetId == acReplay.CopyId)
    {
        aEnd(true);
        aEnd(false);
    }
    return true;
}
} // namespace ReplayState
