#pragma once

#include <network/ENetInterface.h>
#include <Messages/RelayedMessages.h>
#include <Structs/RecordPart.h>

#include <NoCopyMove.h>

#include <cstdint>
#include <vector>

struct HostCharacters;
struct HostClaims;
struct HostNetwork;
struct HostPlayers;
struct HostRouter;

// Who rides a mount: one rider, from its ride's claim until the ride ends, and its passengers. Kept in the
// character table (HostCharacters::SeatsOf, ForEachCharacter), so the table's order is the rides' order.
struct RideSeats
{
    uint32_t Rider{};
    // In boarding order: the first drives next.
    std::vector<uint32_t> Passengers;
};

// Rides on the host: a ride is a claim on its horse (its rider's game simulates it); passengers board a horse another
// player drives, and the first drives on when its rider gets off or leaves.
struct HostRides
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Registers its rules with aRouter, and the ride's claim rule with aClaims.
    HostRides(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters, HostClaims& aClaims) noexcept;

    NO_COPY_MOVE(HostRides);

    // A rider's Ride part (RidePart) names a horse the host gave it a seat on (OnBoard), or none.
    [[nodiscard]] bool RideGranted(uint32_t aRider, const RecordPart& acPart) const noexcept;
    // Who rides what, to a game that just got the riders' and mounts' spawns.
    void SendRides(ConnectionId_t aPlayer) const noexcept;
    // A character gone: a mount it rode goes to its first passenger, else back to the host; a passenger leaves its list.
    void Removed(uint32_t aSessionId) noexcept;
    // A player gone: each mount its game holds for its rider aRider goes on the same way.
    void PlayerLeft(ConnectionId_t aPlayer, uint32_t aRider) noexcept;

private:
    // Its rider got off: its first passenger drives on, else the ride ends (the rider's game gives the mount back).
    void OnDismount(const DismountRequest& acRequest, ConnectionId_t aSender) noexcept;
    // A player got on a horse another player drives: a passenger, its copy seated on every other game.
    void OnBoard(const BoardRequest& acRequest, ConnectionId_t aSender) noexcept;
    void OnUnboard(const UnboardRequest& acRequest, ConnectionId_t aSender) noexcept;
    // The ride ends, every game told; aRelease: the mount is the host's again at once (its rider gone).
    void EndRide(uint32_t aMountId, uint32_t aRiderId, bool aRelease) noexcept;
    // The driver is gone: the first passenger drives, the horse its game's. False with no passenger.
    bool HandOver(uint32_t aMountId) noexcept;
    void SendUnboard(uint32_t aRiderId, uint32_t aMountId, ConnectionId_t aSender) noexcept;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    HostCharacters& m_characters;
    HostClaims& m_claims;
};
