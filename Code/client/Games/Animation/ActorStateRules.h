#pragma once

#include <Structs/WeaponState.h>

#include <cstdint>
#include <optional>

// Who writes each bit of a copy's actor state (one-simulator.md, piece 2; docs/re/systems/actor-state.md).
namespace ActorStateRules
{
// flags1 (CommonLibSSE-NG ActorState1).
constexpr uint32_t kSneaking = 1u << 9;
constexpr uint32_t kSitMask = 0xFu << 14;
constexpr uint32_t kLifeMask = 0xFu << 21;
constexpr uint32_t kKnockMask = 7u << 25;
constexpr uint32_t kAttackMask = 0xFu << 28;
// flags2 (ActorState2).
constexpr uint32_t kWeaponMask = 7u << 5;
constexpr uint32_t kWantBlocking = 0x100;
constexpr uint32_t kRecoilMask = 0xC00;

// The record's part: bits only an action sets, with no handler on the copy (sneak 37951; block by actions 45/47 in
// 39004), and the life states that are no death.
constexpr uint32_t kPartBits1 = kSneaking | kLifeMask;
constexpr uint32_t kPartBits2 = kWantBlocking;

constexpr uint32_t Take(uint32_t aOwner, uint32_t aCopy, uint32_t aMask) noexcept
{
    return (aCopy & ~aMask) | (aOwner & aMask);
}

// A copy taking its owner's flags with an action or a graph state (its stream's action, a sync point, a replayed
// action): all but its part's bits, its own ragdoll (knock) and its own weapon transition. An action's start (attack,
// sit, recoil: 39004) comes this way, and its own handlers carry it on.
constexpr uint32_t FromSnapshot1(uint32_t aOwner, uint32_t aCopy) noexcept { return Take(aOwner, aCopy, ~(kPartBits1 | kKnockMask)); }
constexpr uint32_t FromSnapshot2(uint32_t aOwner, uint32_t aCopy) noexcept { return Take(aOwner, aCopy, ~(kPartBits2 | kWeaponMask)); }

// ACTOR_LIFE_STATE: dying (1) and dead (2) are its stream's death action's; a copy in them keeps them.
constexpr uint8_t kLifeDying = 1;
constexpr uint8_t kLifeDead = 2;
constexpr uint8_t LifeOf(uint32_t aFlags1) noexcept { return aFlags1 >> 21 & 0xF; }
constexpr bool IsDeath(uint8_t aLife) noexcept { return aLife == kLifeDying || aLife == kLifeDead; }

// A copy taking its owner's part.
constexpr uint32_t FromPart1(uint32_t aOwner, uint32_t aCopy) noexcept
{
    const bool cLife = !IsDeath(LifeOf(aOwner)) && !IsDeath(LifeOf(aCopy));
    return Take(aOwner, aCopy, cLife ? kPartBits1 : kPartBits1 & ~kLifeMask);
}
constexpr uint32_t FromPart2(uint32_t aOwner, uint32_t aCopy) noexcept { return Take(aOwner, aCopy, kPartBits2); }

// On its stream, a part applies once the copy's playback reaches the owner's frame it was read at.
constexpr bool Due(bool aOnStream, uint64_t aPlayback, uint64_t aPartTick) noexcept
{
    return !aOnStream || aPlayback >= aPartTick;
}

// A part's kill move enters the copy's (38613, with its killer).
constexpr bool EntersKillMove(bool aPartInKillMove, bool aCopyInKillMove, bool aCopyDead) noexcept
{
    return aPartInKillMove && !aCopyInKillMove && !aCopyDead;
}

// Its end is the copy's own KillMoveEnd (42871) while its pair plays. A copy still in one its owner left, with no
// pair playing (made mid-kill-move, its end before its sync point), is taken out, its killer cleared first (38614
// kills an actor with a killer).
constexpr bool LeavesKillMove(bool aPartInKillMove, bool aCopyInKillMove, bool aCopyInPair, bool aCopyDead) noexcept
{
    return !aPartInKillMove && aCopyInKillMove && !aCopyInPair && !aCopyDead;
}

// Drawn hands: an owner's are declared once settled (drawn or sheathed), as the copy's own draw handlers (42824, 42825)
// reach the same state from its stream. Not a dead or dying owner's: its hands drop with its death, which its copies
// play later.
constexpr std::optional<bool> SettledHands(WeaponState aOwner, bool aOwnerDead) noexcept
{
    if (aOwnerDead)
        return std::nullopt;
    if (aOwner == WeaponState::Drawn)
        return true;
    if (aOwner == WeaponState::Sheathed)
        return false;
    return std::nullopt;
}
// A copy not mid-animation that shows the other hands is put right; mid-animation, it is looked at again after.
constexpr bool PutsHandsRight(bool aPartHandsUp, WeaponState aCopy) noexcept
{
    return !IsAnimating(aCopy) && ShowsHandsUp(aCopy) != aPartHandsUp;
}
constexpr bool HandsPending(WeaponState aCopy) noexcept
{
    return IsAnimating(aCopy);
}
} // namespace ActorStateRules
