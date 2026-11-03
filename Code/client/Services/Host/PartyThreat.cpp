#include <Services/Host/PartyThreat.h>
#include <algorithm>
#include <cmath>

namespace PartyThreat
{
bool State::IsPlayer(uint32_t aActor) const noexcept
{
    return std::find(Players.begin(), Players.end(), aActor) != Players.end();
}
void State::Hit(uint32_t aVictim, uint32_t aAttacker, float aDamage) noexcept
{
    if (!Active || !IsPlayer(aAttacker) || !aVictim || IsPlayer(aVictim) || !std::isfinite(aDamage) || aDamage <= 0 ||
        !std::isfinite(Now))
        return;
    DamageByPair[Key(aVictim, aAttacker)] = {DamageOf(aVictim, aAttacker) + aDamage, Now};
}
double State::DamageOf(uint32_t aVictim, uint32_t aAttacker) const noexcept
{
    const auto it = DamageByPair.find(Key(aVictim, aAttacker));
    if (it == DamageByPair.end())
        return 0;
    return it->second.Amount * std::exp2(-std::max(0.0, Now - it->second.At) / kDamageHalfLife);
}
bool State::RecentlyHit(uint32_t aVictim, uint32_t aAttacker) const noexcept
{
    const auto it = DamageByPair.find(Key(aVictim, aAttacker));
    return it != DamageByPair.end() && it->second.Amount > 0 && Now >= it->second.At &&
           Now - it->second.At <= kRecentHit;
}
bool State::CanTarget(uint32_t aVictim, uint32_t aPlayer, uint32_t aCurrent, const Perception& p) const noexcept
{
    if (!Active || !IsPlayer(aPlayer) || p.Invisible || (p.Sneaking && p.Detection <= 0))
        return false;
    if (!p.Lost && ((p.Detection > 0 && p.Stealth <= 0) || (aPlayer == aCurrent && p.Known)))
        return true;
    return (p.LineOfSight || p.ClearPath) && (aPlayer == aCurrent || RecentlyHit(aVictim, aPlayer));
}
uint32_t State::Resolve(uint32_t aActor, uint32_t aCurrent, uint32_t aNative,
                        std::span<const Candidate> aCandidates) const noexcept
{
    const auto combat = Combats.find(aActor);
    if (!Active || !IsPlayer(aNative) || !IsPlayer(aCurrent) || combat == Combats.end() ||
        combat->second.Target != aCurrent)
        return aNative;
    const uint32_t cChosen = Choose(aActor, aCurrent, aCandidates);
    for (const auto& cCandidate : aCandidates)
        if (cCandidate.ActorId == cChosen && cCandidate.Eligible && std::isfinite(cCandidate.Distance) &&
            cCandidate.Distance >= 0)
            return cChosen;
    return aNative;
}
uint32_t State::Choose(uint32_t aActor, uint32_t aCurrent, std::span<const Candidate> aCandidates) const noexcept
{
    const auto combat = Combats.find(aActor);
    if (!Active || !IsPlayer(aCurrent) || combat == Combats.end() || combat->second.Target != aCurrent)
        return aCurrent;
    const auto valid = [this](const Candidate& acCandidate) {
        return acCandidate.Eligible && IsPlayer(acCandidate.ActorId) && std::isfinite(acCandidate.Distance) &&
               acCandidate.Distance >= 0;
    };
    const auto score = [this, aActor](const Candidate& acCandidate) {
        return 20.0 / (1.0 + acCandidate.Distance / 512.0) + 4.0 * DamageOf(aActor, acCandidate.ActorId);
    };
    const auto current = std::find_if(aCandidates.begin(), aCandidates.end(),
                                      [&](const auto& c) { return c.ActorId == aCurrent && valid(c); });
    const bool cHasCurrent = current != aCandidates.end();
    if (cHasCurrent && Now - combat->second.ChangedAt < kTargetHold)
        return aCurrent;
    uint32_t best = cHasCurrent ? aCurrent : 0;
    double bestScore = cHasCurrent ? score(*current) : -1;
    for (const auto& cCandidate : aCandidates)
        if (valid(cCandidate) && score(cCandidate) > bestScore)
        {
            best = cCandidate.ActorId;
            bestScore = score(cCandidate);
        }
    if (!best || (cHasCurrent && best != aCurrent && bestScore <= score(*current) * kSwitchRatio))
        return aCurrent;
    return best;
}
} // namespace PartyThreat
