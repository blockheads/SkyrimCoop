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
// ends, so it is sent again for a while; a clip can still land its blow, so the two players' hits are refused meanwhile.
constexpr uint8_t cClashStopUpdates = 30;
constexpr uint64_t cClashGuardMs = 5000; // past the longest kill clip

constexpr bool ClashRefusesHit(uint32_t aHitter, uint32_t aClashPartner, uint64_t aNow, uint64_t aClashUntil) noexcept
{
    return aHitter && aHitter == aClashPartner && aNow < aClashUntil;
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
