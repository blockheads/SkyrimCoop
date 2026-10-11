#pragma once

#include <Events/PacketEvent.h>

struct PlayerLevelRequest;

namespace Server
{

struct World;

/**
 * @brief The player list's per-player state (level); the host's bridge handles the rest.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PlayerService() noexcept = default;

    TP_NOCOPYMOVE(PlayerService);

protected:
    void OnPlayerLevelRequest(const PacketEvent<PlayerLevelRequest>& acMessage) const noexcept;

private:
    World& m_world;

    entt::scoped_connection m_playerLevelConnection;
};

} // namespace Server
