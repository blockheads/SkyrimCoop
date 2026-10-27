#pragma once
#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <chrono>

struct World;
class IPlayerBridge;
struct NotifyFollowerTrade;
struct MenuEvent;
struct UpdateEvent;
struct DisconnectedEvent;

// A follower's trade menu belongs to its picker; closing it releases the host's one inventory interaction.
struct FollowerService
{
    FollowerService(World&, entt::dispatcher&, IPlayerBridge&) noexcept;
    NO_COPY_MOVE(FollowerService);

  private:
    void OnTrade(const NotifyFollowerTrade&) noexcept;
    void OnMenu(const MenuEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void Release() noexcept;
    void OpenPending() noexcept;
    World& m_world;
    IPlayerBridge& m_player;
    uint32_t m_trading{};
    uint32_t m_pendingNpc{};
    bool m_opened{};
    std::chrono::steady_clock::time_point m_until{};
    std::vector<entt::scoped_connection> m_connections;
};
