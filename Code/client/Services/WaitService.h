#pragma once

#include <Bridges/IWaitBridge.h>
#include <Events/Listen.h>
#include <Messages/WaitMessages.h>
#include <NoCopyMove.h>

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct GameLoadedEvent;

struct WaitService
{
    WaitService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                IWaitBridge& aBridge) noexcept;
    ~WaitService() noexcept;
    NO_COPY_MOVE(WaitService);

  private:
    bool Request(uint32_t aHours, bool aSleeping) noexcept;
    bool Cancel() noexcept;
    void Stepped(uint32_t aRemainingSeconds) noexcept;
    void Send(WaitAction aAction, uint32_t aValue = 0, bool aSleeping = false) noexcept;
    void Reset() noexcept;
    void OnState(const NotifyWaitState& acMessage) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnLoaded(const GameLoadedEvent&) noexcept;

    World& m_world;
    TransportService& m_transport;
    IWaitBridge& m_bridge;
    uint32_t m_id{};
    uint32_t m_remainingSeconds{};
    std::vector<uint32_t> m_steps;
    bool m_pending{};
    bool m_cancelPending{};
    bool m_ready{};
    bool m_running{};
    bool m_driver{};
    bool m_finished{};
    uint64_t m_askedAt{};
    std::vector<entt::scoped_connection> m_connections;
};
