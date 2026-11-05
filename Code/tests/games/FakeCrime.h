#pragma once

#include <Bridges/ICrimeBridge.h>
#include <functional>
#include <unordered_map>

// Engine seam. Native costs are covered by the Skyrim scenarios; the witness rule below is the engine's (crime.md).
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
    // An NPC watches every theft; StealAlarm sees the thief only if its detection was asked for within 2 s (37764).
    bool Watched{};
    CrimeGold WatchedBounty{GameId(0, 0x267EA), 0, 5};
    static constexpr uint32_t kFreshFrames = 120;
    uint32_t Frame{};
    std::unordered_map<uint32_t, uint32_t> AskedAt;
    void UpdatePlayers(std::span<const uint32_t> aPlayers) noexcept override
    {
        CrimePlayers.assign(aPlayers.begin(), aPlayers.end());
        ++Frame;
    }
    void AskWitnesses(uint32_t aActor) noexcept override
    {
        AskedAt[aActor] = Frame;
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
        if (JudgeAction)
            return JudgeAction(aActor, aRequest);
        const auto cAsked = AskedAt.find(aActor);
        if (Watched && aRequest.Action == CrimeAction::Theft && cAsked != AskedAt.end() && Frame - cAsked->second <= kFreshFrames)
            Gold = {WatchedBounty};
        return false;
    }
    void LockCaught(uint32_t aAttempt) noexcept override
    {
        Caught = aAttempt;
    }
};
