#pragma once

#include <network/ENetInterface.h>

struct HostCharacters;
struct HostGame;
struct HostPlayers;
struct RequestInventoryChanges;

/**
 * @brief The party's items (docs/design/party-items.md): keys, quest items and the story's rewards. The host's
 * player's stacks are the party's counts, as its gold is the purse (HostPurse); a change to any player's party stack
 * changes them, and every game's players carry the result (NotifyPartyItem).
 */
struct HostPartyItems
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostPartyItems(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostGame& acGame) noexcept;

    // True for a change of a party stack on a player's character: the party's, not relayed as an item.
    [[nodiscard]] bool Change(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Every game's players carry every party item and no stale one (a join).
    void Send() const noexcept;

  private:
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
