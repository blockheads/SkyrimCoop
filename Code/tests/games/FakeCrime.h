#pragma once

#include "FakeEngine.h"
#include <Bridges/ICrimeBridge.h>
#include <FormIds.h>
#include <SessionIds.h>
#include <Services/Host/CrimeRules.h>
#include <functional>
#include <map>
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
    std::vector<CrimeConfiscation> Confiscated;
    std::vector<uint32_t> CrimePlayers;
    void UpdatePlayers(std::span<const uint32_t> aPlayers) noexcept override
    {
        CrimePlayers.assign(aPlayers.begin(), aPlayers.end());
    }
    std::vector<CrimeConfiscation> TakeConfiscations() noexcept override
    {
        return std::exchange(Confiscations, {});
    }
    void Confiscate(const CrimeConfiscation& acDue) noexcept override
    {
        Confiscated.push_back(acDue);
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
    std::vector<uint32_t> Pickpocketed; // PickpocketAlarm's victims (37423), past Judge's checks
    // Opt-in StealAlarm (37422) for a seen theft: the owner's warning (37556, to the thief: a guest's copy in the
    // engine's player's place) while the party has warnings left and the judge allows one, else bounty.
    bool SeenThefts = false;
    uint32_t TheftWarnings{};
    struct Warning
    {
        uint32_t Thief; // form id: the guest's copy
        GameId Owner;
    };
    std::vector<Warning> Warnings;
    // Skyrim: the warning's step (procedure 40) reaches the thief and takes back what the owner owns (Confiscate 16120)
    // into the owner; from a copy, which keeps no ownership, it goes to the guest's game.
    void EndWarnings(uint32_t aOwnerActor) noexcept
    {
        for (const auto& warning : std::exchange(Warnings, {}))
            Confiscations.push_back({.Actor = warning.Thief, .Owner = warning.Owner, .ToActor = aOwnerActor});
    }
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
        if (aRequest.Action == CrimeAction::Pickpocket && cActorTarget && !cStops)
            Pickpocketed.push_back(cTarget->first);
        const bool cTheft = (aRequest.Action == CrimeAction::Theft && !cStops) || aRequest.Action == CrimeAction::ContainerTheft;
        if (SeenThefts && cTheft)
        {
            if (aRequest.AllowWarning && TheftWarnings < 2)
            {
                ++TheftWarnings;
                // Skyrim: 37556 names g_thePlayer; the bridge's swaps put the criminal's copy there (CrimeRules::Warned).
                const uint32_t cWarned = CrimeRules::Warned<uint32_t>(FakeEngine::kPlayer, FakeEngine::kPlayer, aActor);
                Warnings.push_back({cWarned, Stolen.empty() ? aRequest.Owner : Stolen.back().Owner});
            }
            else if (Gold.empty())
                Gold.push_back({GameId(0, 0x28170), 0, 5});
            else
                Gold.front().NonViolent += 5;
        }
        return JudgeAction && JudgeAction(aActor, aRequest);
    }
    // Skyrim: a witness's state of a copy goes stale, and an ask is answered by a later detection pass (crime.md).
    uint32_t Asks{};
    void AskWitnesses(uint32_t) noexcept override
    {
        ++Asks;
    }
    // What each NPC holds against the party, as Skyrim keeps it (crime.md, "Who an NPC fights for a party crime").
    std::map<uint32_t, CrimeRules::Grudge> Grudges;
    bool PartyIsCriminalTo(uint32_t aActorFormId) const noexcept override
    {
        const auto cGrudge = Grudges.find(aActorFormId);
        return cGrudge != Grudges.end() && CrimeRules::PartyIsCriminalTo(cGrudge->second);
    }
    std::vector<CrimeReport> Reports; // reported by this game's engine (37679), not yet taken
    std::vector<CrimeReport> Shown;   // bounty lines this game's HUD showed
    std::vector<CrimeReport> TakeReports() noexcept override
    {
        return std::exchange(Reports, {});
    }
    void ShowReport(const CrimeReport& acReport) noexcept override
    {
        Shown.push_back(acReport);
    }
    void LockCaught(uint32_t aAttempt) noexcept override
    {
        Caught = aAttempt;
    }

private:
    const FakeEngine& m_engine;
};
