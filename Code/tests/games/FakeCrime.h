#pragma once

#include "FakeEngine.h"
#include <Bridges/ICrimeBridge.h>
#include <FormIds.h>
#include <SessionIds.h>
#include <Services/Host/CrimeRules.h>
#include <functional>
#include <set>

// Engine seam only. Native witnesses/costs are covered by the Skyrim scenarios.
struct FakeCrime final : ICrimeBridge
{
    Role CurrentRole{};
    std::vector<CrimeGold> Gold;
    std::vector<CrimeRequest> Actions;
    std::vector<std::pair<uint32_t, CrimeRequest>> Judged;
    std::function<bool(uint32_t, const CrimeRequest&)> JudgeAction;
    uint32_t Caught{};
    std::vector<CrimeConfiscation> Confiscations;
    std::vector<GameId> Confiscated;
    std::vector<uint32_t> CrimePlayers;
    void UpdatePlayers(std::span<const uint32_t> aPlayers) noexcept override
    {
        CrimePlayers.assign(aPlayers.begin(), aPlayers.end());
    }
    std::vector<CrimeConfiscation> TakeConfiscations() noexcept override
    {
        return std::exchange(Confiscations, {});
    }
    void Confiscate(const GameId& aFaction) noexcept override
    {
        Confiscated.push_back(aFaction);
    }

    void SetRole(Role aRole) noexcept override
    {
        CurrentRole = aRole;
    }
    std::vector<CrimeRequest> TakeActions() noexcept override
    {
        return std::exchange(Actions, {});
    }
    std::vector<CrimeGold> ReadGold() const noexcept override
    {
        return Gold;
    }
    void ApplyGold(const std::vector<CrimeGold>& aGold) noexcept override
    {
        Gold = aGold;
    }
    explicit FakeCrime(const FakeEngine& acEngine) noexcept : m_engine(acEngine)
    {
    }
    // Thefts past CrimeBridge::Judge's checks (CrimeRules), with the owner StealAlarm gets. Skyrim: IsAnOwner (20210)
    // reads an actor reference as the player's, so an ownership check of an actor stops there.
    struct Alarm
    {
        CrimeRequest Request;
        GameId Owner;
    };
    std::vector<Alarm> Stolen;
    bool Judge(uint32_t aActor, const CrimeRequest& aRequest) noexcept override
    {
        Judged.emplace_back(aActor, aRequest);
        const auto cTarget = aRequest.TargetActor ? m_engine.Actors.find(Utils::GetFormIdBySessionId(aRequest.TargetActor)) : m_engine.Actors.end();
        const auto cCriminal = m_engine.Actors.find(aActor);
        const bool cActorTarget = cTarget != m_engine.Actors.end();
        const auto cChecks = CrimeRules::ChecksFor(aRequest.Action, cActorTarget);
        const bool cElsewhere = cActorTarget && cCriminal != m_engine.Actors.end() && cTarget->second.Cell != cCriminal->second.Cell;
        const bool cStops = (cActorTarget && cChecks.Ownership) || (cChecks.SameCell && cElsewhere);
        if (aRequest.Action == CrimeAction::Theft && !cStops)
        {
            const GameId cTargetOwner = cActorTarget ? ToSession(cTarget->second.Owner).value_or(GameId{}) : GameId{};
            Stolen.push_back({aRequest, CrimeRules::AlarmOwner(aRequest.Owner, cTargetOwner)});
        }
        return JudgeAction && JudgeAction(aActor, aRequest);
    }
    // Skyrim: a witness's state of a copy goes stale, and an ask is answered by a later detection pass (crime.md).
    uint32_t Asks{};
    void AskWitnesses(uint32_t) noexcept override
    {
        ++Asks;
    }
    std::set<uint32_t> CriminalTo; // NPCs a party crime made hostile
    bool PartyIsCriminalTo(uint32_t aActorFormId) const noexcept override
    {
        return CriminalTo.contains(aActorFormId);
    }
    void LockCaught(uint32_t aAttempt) noexcept override
    {
        Caught = aAttempt;
    }

private:
    const FakeEngine& m_engine;
};
