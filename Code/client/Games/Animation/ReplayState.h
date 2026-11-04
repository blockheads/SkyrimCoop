#pragma once

#include <cstdint>

namespace ReplayState
{
constexpr uint32_t kWeaponMask = 7u << 5;

constexpr uint32_t Flags2(uint32_t aOwner, uint32_t aCopy) noexcept
{
    return (aOwner & ~kWeaponMask) | (aCopy & kWeaponMask);
}

enum class PairReplay
{
    kPlay,  // the partner here is free: the engine pairs them
    kSkip,  // the partner is already in this pair (its other half came back), or in another
    kClash, // the two games each started a pair on these two: neither plays, both end
};

// The engine takes a pair on two actors already paired as the same pair and ends both clips at once (32803; docs/re/systems/kill-moves.md).
constexpr PairReplay ReplayPair(bool aLocalPartner, bool aPartnerInKillMove, bool aPartnersOtherHalf, bool aPartnerPairedWithCopy) noexcept
{
    if (!aLocalPartner || !aPartnerInKillMove)
        return PairReplay::kPlay;
    return !aPartnersOtherHalf && aPartnerPairedWithCopy ? PairReplay::kClash : PairReplay::kSkip;
}
} // namespace ReplayState
