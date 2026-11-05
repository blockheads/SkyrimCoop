#pragma once

#include <Bridges/ICrimeBridge.h>
#include <functional>

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
    bool Judge(uint32_t aActor, const CrimeRequest& aRequest) noexcept override
    {
        Judged.emplace_back(aActor, aRequest);
        return JudgeAction && JudgeAction(aActor, aRequest);
    }
    void LockCaught(uint32_t aAttempt) noexcept override
    {
        Caught = aAttempt;
    }
};
