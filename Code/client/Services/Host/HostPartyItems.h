#pragma once

#include <network/ENetInterface.h>
#include <Structs/GameId.h>

#include <array>
#include <string_view>
#include <map>
#include <optional>
#include <unordered_map>

struct HostCharacters;
struct HostItemsGame;
struct HostPlayers;
struct RequestInventoryChanges;

/**
 * @brief What the party shares (docs/design/shared-items.md): its gold, keys, quest items and the story's rewards. The
 * host's player's stacks are the party's; a change to any player's changes them, and every game's players carry the
 * result (NotifyPartyGold, NotifyPartyItem).
 */
struct HostPartyItems
{
    using ConnectionId_t = Net::ConnectionId_t;

    HostPartyItems(const HostPlayers& acPlayers, const HostCharacters& acCharacters, const HostItemsGame& acGame) noexcept;

    // What a player did with the gold it spent, as every game's chat tells it ("Lydia devoured 40 gold"; the user's words).
    static constexpr std::array<std::string_view, 10> cSpendWords{
        "siphoned", "funneled", "stole", "coveted", "devoured", "tax-evaded", "donated to the chatholic church",
        "iatethosefood", "me -> ur mom", "uhm guys?"};

    // True for a change of gold or a party stack on a player's character: the party's, not relayed as an item.
    // aInFinderPack false: a grant the finder's game has not added yet (a container take), so no rename is asked of it.
    [[nodiscard]] bool Change(const RequestInventoryChanges& acFound, ConnectionId_t aSender, bool aInFinderPack = true) noexcept;
    // A guest found the quest item the host's player now holds for the party: chat names it at the arrival (0 forgets).
    void FoundBy(const GameId& acItem, ConnectionId_t aFinder) noexcept
    {
        if (aFinder)
            m_finders[acItem] = aFinder;
        else
            m_finders.erase(acItem);
    }
    // The real stack the host's player dropped for that player's drop of its mirror, taken once (HostItems: its twin);
    // an empty id: the drop was refused.
    [[nodiscard]] std::optional<GameId> TakeDropped(ConnectionId_t aPlayer, const GameId& acItem) noexcept
    {
        const auto it = m_dropped.find({aPlayer, acItem.LogFormat()});
        if (it == m_dropped.end())
            return std::nullopt;
        const GameId cObject = it->second;
        m_dropped.erase(it);
        return cObject;
    }
    // Every game's players carry the party's gold and every party item, no stale one (a join).
    void Send() const noexcept;

  private:
    // Sends the result to every game: aShownThere's engine showed the change (0: none did), the others' HUDs show it.
    template <class T> void SendChange(T aNotify, int32_t aChange, ConnectionId_t aShownThere) const noexcept;
    void TellSpent(ConnectionId_t aPlayer, int32_t aGold) const noexcept;
    void TellFound(const RequestInventoryChanges& acRequest, ConnectionId_t aPlayer) const noexcept;

    const HostPlayers& m_players;
    const HostCharacters& m_characters;
    const HostItemsGame& m_game;
    std::unordered_map<GameId, ConnectionId_t> m_finders;
    std::map<std::pair<ConnectionId_t, uint64_t>, GameId> m_dropped; // (player, item) -> the real stack it dropped
    std::map<std::pair<ConnectionId_t, uint64_t>, int32_t> m_refused; // (player, item) -> refused drops still coming back
};
