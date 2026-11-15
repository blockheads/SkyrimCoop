#pragma once

#include <Messages/CrimeMessages.h>
#include <span>

class ICrimeBridge
{
  public:
    enum class Role
    {
        Offline,
        Host,
        Guest
    };
    virtual ~ICrimeBridge() = default;
    virtual void SetRole(Role) noexcept = 0;
    virtual std::vector<CrimeRequest> TakeActions() noexcept = 0;
    virtual std::vector<CrimeGold> ReadGold() const noexcept = 0;
    virtual void ApplyGold(const std::vector<CrimeGold>&) noexcept = 0;
    // True when the native lockpick check caught this attempt.
    virtual bool Judge(uint32_t aActorFormId, const CrimeRequest&) noexcept = 0;
    // Asks every witness for its detection of this copy; a later detection pass answers (crime.md).
    virtual void AskWitnesses(uint32_t aActorFormId) noexcept = 0;
    // The NPC is hostile for a party crime (CrimeRules::Grudge, crime.md).
    virtual bool PartyIsCriminalTo(uint32_t aActorFormId) const noexcept = 0;
    virtual void LockCaught(uint32_t aAttempt) noexcept = 0;
    virtual std::vector<CrimeConfiscation> TakeConfiscations() noexcept = 0;
    // This game's player's stolen items, as the host's engine took them from its copy (CrimeConfiscation).
    virtual void Confiscate(const CrimeConfiscation&) noexcept = 0;
    virtual void UpdatePlayers(std::span<const uint32_t> aActorFormIds) noexcept = 0;
    // The bounties the host's engine reported for the party since the last call (37679).
    virtual std::vector<CrimeReport> TakeReports() noexcept = 0;
    // The engine's bounty HUD line for a report from the host.
    virtual void ShowReport(const CrimeReport&) noexcept = 0;
};
