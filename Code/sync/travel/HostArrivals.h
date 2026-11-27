#pragma once

#include <network/ENetInterface.h>
#include <Messages/NotifyPartyTravel.h>

#include <NoCopyMove.h>

#include <optional>
#include <unordered_map>
#include <unordered_set>

struct EnterCellRequest;
struct HostCharacters;
struct HostNetwork;
struct HostPlayers;
struct HostRouter;
struct PartySummonRequest;
struct PartyTravelRequest;

/**
 * @brief Where each player's game is, under the party tether: everyone is where the host is, or on the way there.
 *
 * Arriving somewhere (a door, coc, fast travel), a game loses its copies with the old cell and says where it is. Once
 * it is with the host, it gets everyone with the host and they get it; a player still on the way gets nothing, so
 * nothing lands in the wrong place.
 */
struct HostArrivals
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter.
    HostArrivals(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept;

    NO_COPY_MOVE(HostArrivals);

    // A player's character announced in that cell (outdoors its worldspace).
    void Announced(ConnectionId_t aPlayer, GameId aCellId, GameId aWorldSpaceId) noexcept { m_places[aPlayer] = PlaceOf(aCellId, aWorldSpaceId); }
    // A joiner where the host is not, or outdoors out of its grid, goes there (the party tether).
    void Joined(ConnectionId_t aPlayer, const Vector3_NetQuantize& acPosition) noexcept;
    // Its game is where the host's is (not still on the way).
    [[nodiscard]] bool WithHost(ConnectionId_t aPlayer) const noexcept;
    void PlayerLeft(ConnectionId_t aPlayer) noexcept;

    // Where a player's character stands now: its cell (outdoors its worldspace too), position and heading, from its
    // game's announcement, arrivals and movement (the host's own from its engine). None for any other character.
    [[nodiscard]] std::optional<NotifyPartyTravel> CharacterPlace(uint32_t aSessionId) const noexcept;
    // A player's character outdoors: its worldspace and position from its records (no engine read). None indoors, on
    // its way somewhere, or before its announcement.
    struct Outdoors
    {
        GameId WorldSpaceId{};
        Vector3_NetQuantize Position{};
    };
    [[nodiscard]] std::optional<Outdoors> OutdoorsAt(ConnectionId_t aPlayer) const noexcept;
    // The party tether's trip to the host's player (the soft leash, HostLeash).
    void BringToHost(ConnectionId_t aPlayer) noexcept { BringToParty(aPlayer); }
    // Sent to the host's player and not arrived yet.
    [[nodiscard]] bool OnWayToHost(ConnectionId_t aPlayer) const noexcept { return m_broughtToParty.contains(aPlayer); }

private:
    // A player's game arrived somewhere: with the host, it gets everyone there and they get it.
    void OnTravelQueued(const PartyTravelRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnEnterCell(const EnterCellRequest& acRequest, ConnectionId_t aSender) noexcept;
    // /tpa: everyone else goes to the sender's spot; the tether's last place becomes it.
    void OnPartySummon(const PartySummonRequest& acRequest, ConnectionId_t aSender) noexcept;
    // Indoors anywhere; outdoors within the host's player's loaded grid (GridCellCoords::IsCellInGridCell).
    [[nodiscard]] bool InHostGrid(const Vector3_NetQuantize& acPosition) const noexcept;
    // A player that joined where the host is not, or outdoors out of its grid, goes there (the party tether).
    void BringToParty(ConnectionId_t aPlayer) noexcept;

    // Where a player's game is: a worldspace (outdoors) or an interior cell.
    struct Place
    {
        GameId WorldSpaceId{};
        GameId CellId{};
        bool operator==(const Place&) const noexcept = default;
    };
    static Place PlaceOf(GameId aCellId, GameId aWorldSpaceId) noexcept;
    // An arrival in that place: its interior cell, or outdoors its worldspace within the loaded grid.
    static bool At(const NotifyPartyTravel& acPlace, const EnterCellRequest& acArrival) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    HostCharacters& m_characters;
    // From each player's announcement, then its arrivals.
    std::unordered_map<ConnectionId_t, Place> m_places;
    // Arrived where the host is not yet (it led, or the host is still loading): the host's actors there reach
    // them once the host arrives.
    std::unordered_set<ConnectionId_t> m_aheadOfHost;
    // Sent to the party at its join (BringToParty), not arrived yet.
    std::unordered_set<ConnectionId_t> m_broughtToParty;
    // Where the party went last (the latest player to travel by itself), for followers that arrive elsewhere.
    std::optional<NotifyPartyTravel> m_partyTravel;
    // Each player's trip announced before its load (PartyTravelRequest), not arrived yet: its arrival there is not
    // sent again (a follower that walked off its spot stays).
    std::unordered_map<ConnectionId_t, NotifyPartyTravel> m_announced;
};
