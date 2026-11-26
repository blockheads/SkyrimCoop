#pragma once
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PartyThreat
{
inline constexpr double kDamageHalfLife = 8.0;
inline constexpr double kTargetHold = 1.5;
inline constexpr double kSwitchRatio = 1.2;
inline constexpr double kRecentHit = 3.0;
struct Damage
{
    double Amount{};
    double At{};
};
struct Combat
{
    uint32_t Target{};
    double ChangedAt{};
};
struct Candidate
{
    uint32_t ActorId{};
    float Distance{};
    bool Eligible{};
};
struct Perception
{
    bool Known{true};
    bool Lost{};
    int32_t Detection{100};
    float Stealth{};
    bool Invisible{};
    bool Sneaking{};
    bool LineOfSight{};
    bool ClearPath{};
};
// A guest's friendly-hit anger (Actor.cpp, key = actor << 32 | the guest's copy) is the party's: every player's body
// is the angry actor's enemy (combat-targets.md, A fight with one player is the party's).
[[nodiscard]] constexpr bool AngerCovers(uint64_t aAngerKey, uint32_t aActor, bool aOtherIsPlayer) noexcept
{
    return aOtherIsPlayer && aAngerKey && uint32_t(aAngerKey >> 32) == aActor;
}
// The anger ends once its actor has fought a player, any player, and left combat (as kAngryWithPlayer's fight).
[[nodiscard]] constexpr bool AngerFought(bool aFought, bool aTargetIsPlayer) noexcept
{
    return aFought || aTargetIsPlayer;
}
struct State
{
    bool Active{};
    double Now{};
    std::vector<uint32_t> Players;
    std::unordered_map<uint64_t, Damage> DamageByPair;
    std::unordered_map<uint32_t, Combat> Combats;
    std::unordered_set<uint32_t> PartyFights; // NPCs fighting a player, for any reason: every player is their foe

    static uint64_t Key(uint32_t aVictim, uint32_t aAttacker) noexcept
    {
        return uint64_t(aVictim) << 32 | aAttacker;
    }
    bool IsPlayer(uint32_t aActor) const noexcept;
    // An NPC in a party fight sees every player as an enemy (combat-targets.md, A fight with one player is the party's).
    bool IsPartyFoe(uint32_t aActor, uint32_t aTarget) const noexcept;
    void Hit(uint32_t aVictim, uint32_t aAttacker, float aDamage) noexcept;
    double DamageOf(uint32_t aVictim, uint32_t aAttacker) const noexcept;
    bool RecentlyHit(uint32_t aVictim, uint32_t aAttacker) const noexcept;
    bool CanTarget(uint32_t aVictim, uint32_t aPlayer, uint32_t aCurrent,
                   const Perception& acPerception) const noexcept;
    uint32_t Resolve(uint32_t aActor, uint32_t aCurrent, uint32_t aNative,
                     std::span<const Candidate> aCandidates) const noexcept;
    uint32_t Choose(uint32_t aActor, uint32_t aCurrent, std::span<const Candidate> aCandidates) const noexcept;
};
} // namespace PartyThreat
