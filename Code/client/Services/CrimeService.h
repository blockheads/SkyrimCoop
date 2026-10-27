#pragma once

#include <Bridges/ICrimeBridge.h>
#include <Events/Listen.h>
#include <NoCopyMove.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct GameLoadedEvent;

struct CrimeService
{
    CrimeService(World&, entt::dispatcher&, TransportService&, ICrimeBridge&) noexcept;
    ~CrimeService() noexcept;
    NO_COPY_MOVE(CrimeService);
    std::vector<CrimeGold> ReadGold() const noexcept
    {
        return m_bridge.ReadGold();
    }
    bool Judge(uint32_t aActorSessionId, const CrimeRequest&) noexcept;
    std::vector<CrimeConfiscation> TakeConfiscations() noexcept;

  private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnState(const NotifyCrimeState&) noexcept;
    void OnLockCrime(const NotifyLockCrime&) noexcept;
    void OnConfiscation(const NotifyCrimeConfiscation&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnLoaded(const GameLoadedEvent&) noexcept;
    World& m_world;
    TransportService& m_transport;
    ICrimeBridge& m_bridge;
    uint32_t m_sequence{};
    uint32_t m_lastConfiscation{};
    bool m_hasState{};
    std::vector<CrimeGold> m_gold;
    std::vector<entt::scoped_connection> m_connections;
};
