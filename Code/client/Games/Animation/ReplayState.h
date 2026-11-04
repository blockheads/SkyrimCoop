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
