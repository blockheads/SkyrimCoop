#pragma once

#include <Events/PacketEvent.h>

struct PlayerRespawnRequest;
struct PlayerLevelRequest;

namespace Server
{

struct World;

/**
 * @brief Handles player specific actions that might change the information needed by other clients about that player.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PlayerService() noexcept = default;

    TP_NOCOPYMOVE(PlayerService);

protected:
    void OnPlayerRespawnRequest(const PacketEvent<PlayerRespawnRequest>& acMessage) const noexcept;
    void OnPlayerLevelRequest(const PacketEvent<PlayerLevelRequest>& acMessage) const noexcept;

private:
    World& m_world;

    entt::scoped_connection m_playerRespawnConnection;
    entt::scoped_connection m_playerLevelConnection;
};

} // namespace Server
