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
    // A load replaces every crime record: who did the old ones no longer applies.
    virtual void ForgetCulprits() noexcept = 0;
    virtual void LockCaught(uint32_t aAttempt) noexcept = 0;
    virtual std::vector<CrimeConfiscation> TakeConfiscations() noexcept = 0;
    virtual void Confiscate(const GameId&) noexcept = 0;
    virtual void UpdatePlayers(std::span<const uint32_t> aActorFormIds) noexcept = 0;
};
