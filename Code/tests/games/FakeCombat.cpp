#include "FakeEngine.h"
#include <Events/AiStateChangeEvent.h>
#include <World.h>

void FakeEngine::RunNativeCombat() noexcept
{
    // Actor::UpdateCombatSearchState (37273) clears the target on copies with no native combat controller.
    for (auto& [id, actor] : Actors)
        if (actor.Copy)
        {
            actor.Ai.CombatTargetId = 0;
            actor.Ai.CombatTargetId = actor.OwnerCombatTarget;
        }
    for (const auto& [cActor, cCandidates] : CombatCandidates)
    {
        for (uint32_t selection = 0; selection < CombatSelectionsPerFrame; ++selection)
        {
            const auto it = Actors.find(cActor);
            if (it == Actors.end() || it->second.Copy || it->second.Dead || !it->second.Ai.CombatState)
                continue;
            auto& ai = it->second.Ai;
            const uint32_t cNative = CombatProposals.contains(cActor) ? CombatProposals.at(cActor) : ai.CombatTargetId;
            auto candidates = cCandidates;
            for (auto& candidate : candidates)
                if (const auto sight = CombatPerception.find(PartyThreat::State::Key(cActor, candidate.ActorId));
                    sight != CombatPerception.end())
                    candidate.Eligible &=
                        PartyThreats.CanTarget(cActor, candidate.ActorId, ai.CombatTargetId, sight->second);
            const uint32_t cChosen = PartyThreats.Resolve(cActor, ai.CombatTargetId, cNative, candidates);
            if (cChosen != ai.CombatTargetId)
            {
                ai.CombatTargetId = cChosen;
                World::Get().GetRunner().Trigger(AiStateChangeEvent{cActor});
            }
        }
    }
}
