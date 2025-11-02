#pragma once

namespace Server
{

struct Player;

/**
 * @brief Dispatches when a player leaves the server.
 */
struct PlayerLeaveEvent
{
    PlayerLeaveEvent(Player* apPlayer)
        : pPlayer{apPlayer}
    {
    }

    Player* pPlayer;
};

} // namespace Server
