#pragma once

#include <Bridges/ICrimeBridge.h>

class CrimeBridge final : public ICrimeBridge
{
  public:
    ~CrimeBridge() override;
    void SetRole(Role) noexcept override;
    std::vector<CrimeRequest> TakeActions() noexcept override;
    std::vector<CrimeGold> ReadGold() const noexcept override;
    void ApplyGold(const std::vector<CrimeGold>&) noexcept override;
    bool Judge(uint32_t aActorFormId, const CrimeRequest&) noexcept override;
    void LockCaught(uint32_t aAttempt) noexcept override;
    std::vector<CrimeConfiscation> TakeConfiscations() noexcept override;
    void Confiscate(const GameId&) noexcept override;
    void UpdatePlayers(std::span<const uint32_t> aActorFormIds) noexcept override;

  private:
    std::vector<CrimeGold> m_reactionState;
    std::vector<uint32_t> m_players;
};
