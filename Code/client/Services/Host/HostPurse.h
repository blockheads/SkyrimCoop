#pragma once

#include <network/ENetInterface.h>

struct HostCharacters;
struct HostGame;
struct HostPlayers;
struct RequestInventoryChanges;

/**
 * @brief The party's gold (docs/design/party-purse.md): one purse, the host's player's gold. A change to any player's
 * gold changes it, and every game's players (its own and its copies) carry it (NotifyPartyGold).
 */
struct HostPurse
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostPurse(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept;

    // True for a change of a player's gold (Gold001 on a player's character): the purse's, not relayed as an item.
    [[nodiscard]] bool Change(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Every game's players carry the purse.
    void Send() const noexcept;

  private:
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
