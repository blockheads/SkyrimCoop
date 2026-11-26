#pragma once

#include <Messages/WaitMessages.h>
#include <NoCopyMove.h>
#include <network/ENetInterface.h>
#include <optional>
#include <set>

struct HostRouter;
struct HostNetwork;
struct HostPlayers;
struct HostTimeGame;
struct HostWorldGame;

// One native countdown, driven by the host after every game's menu has paused it.
struct HostWait
{
    using ConnectionId_t = Net::ConnectionId_t;
    HostWait(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers,
             const HostTimeGame& acTime, const HostWorldGame& acWorld) noexcept;
    NO_COPY_MOVE(HostWait);

    void Update(uint64_t aTick) noexcept;
    void OnPlayerLeft(ConnectionId_t aPlayer) noexcept;
    void OnPlayerEntered() noexcept;

  private:
    struct Wait
    {
        uint32_t Id{};
        uint32_t Hours{};
        uint32_t RemainingSeconds{};
        ConnectionId_t Requester{};
        bool Sleeping{};
        bool Running{};
        uint64_t LastActivity{};
        std::set<ConnectionId_t> Pending;
    };

    void OnRequest(const WaitRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnWorldRequest(const WaitWorldRequest& acRequest, ConnectionId_t aSender) noexcept;
    void Tell(WaitPhase aPhase) const noexcept;
    void End() noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    const HostTimeGame& m_time;
    const HostWorldGame& m_world;
    std::optional<Wait> m_wait;
    uint32_t m_lastId{};
    uint32_t m_completedId{};
    uint64_t m_tick{};
};
