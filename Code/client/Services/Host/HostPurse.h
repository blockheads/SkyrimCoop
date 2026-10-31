#pragma once

#include <network/ENetInterface.h>

#include <array>
#include <string_view>

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

    // What a player did with the gold it spent, as every game's chat tells it ("Lydia devoured 40 gold"; the user's words).
    static constexpr std::array<std::string_view, 10> cSpendWords{
        "siphoned", "funneled", "stole", "coveted", "devoured", "tax-evaded", "donated to the chatholic church",
        "iatethosefood", "me -> ur mom", "uhm guys?"};

    // True for a change of a player's gold (Gold001 on a player's character): the purse's, not relayed as an item.
    [[nodiscard]] bool Change(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Every game's players carry the purse; every game but aChanger's (its engine showed it) shows aShown on its HUD.
    void Send(int32_t aShown = 0, ConnectionId_t aChanger = 0) const noexcept;

  private:
    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostGame& m_game;
};
