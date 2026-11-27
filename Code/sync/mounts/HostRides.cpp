#include <mounts/HostRides.h>

#include <characters/HostCharacters.h>
#include <characters/HostClaims.h>
#include <core/HostNetwork.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>

#include <Messages/Relayed.h>
#include <Structs/RidePart.h>

#include <algorithm>

HostRides::HostRides(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters, HostClaims& aClaims) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(aCharacters)
    , m_claims(aClaims)
{
    aRouter.On(*this, &HostRides::OnDismount);
    aRouter.On(*this, &HostRides::OnBoard);
    aRouter.On(*this, &HostRides::OnUnboard);
    // A ride is a claim on its horse (its rider's game simulates it, its movement the rider's input); every game hears
    // who rides it. A claim that ends with its rider still marked (its game could not take the horse) ends the ride.
    m_claims.SetRule(Payloads::Claim::kRide, {
        .Allows = [this](const Payloads::Claim& acClaim) {
            const uint32_t cMountId = acClaim.Members[0].ActorId;
            return acClaim.Members.size() == 1 && m_characters.SeatsOf(cMountId) &&
                   !m_characters.PlayerRecordOf(cMountId, m_characters.OwnerOf(cMountId));
        },
        .Granted = [this](const Payloads::Claim& acClaim, ConnectionId_t aTo, bool aOwnersChanged) {
            const uint32_t cMountId = acClaim.Members[0].ActorId;
            m_characters.SeatsOf(cMountId)->Rider = acClaim.ForId;
            NotifyMount notify;
            notify.RiderId = acClaim.ForId;
            notify.MountId = cMountId;
            if (!aOwnersChanged)
                return m_players.SendToOthers(notify, aTo);
            spdlog::info("HostRides: {:x} rides {:x}, which is its game's now", aTo, cMountId);
            m_players.SendToAll(notify);
        },
        .Ended = [this](const Payloads::Claim& acClaim) {
            const uint32_t cMountId = acClaim.Members.empty() ? 0 : acClaim.Members[0].ActorId;
            if (const RideSeats* cpSeats = m_characters.SeatsOf(cMountId); cpSeats && cpSeats->Rider == acClaim.ForId)
                EndRide(cMountId, acClaim.ForId, false);
        },
    });
}

void HostRides::OnDismount(const DismountRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (m_characters.OwnerOf(acRequest.RiderId) != aSender || !m_characters.SeatsOf(acRequest.MountId) ||
        m_characters.PlayerRecordOf(acRequest.MountId, aSender) || m_characters.OwnerOf(acRequest.MountId) != aSender)
    {
        spdlog::warn("HostRides: {:x} took {:x} off {:x}, which it cannot", aSender, acRequest.RiderId, acRequest.MountId);
        return;
    }
    // A passenger drives on at once; else the rider's game's give-back (HostClaims) follows this.
    if (HandOver(acRequest.MountId))
        SendUnboard(acRequest.RiderId, acRequest.MountId, aSender);
    else
        EndRide(acRequest.MountId, acRequest.RiderId, false);
}

void HostRides::OnBoard(const BoardRequest& acRequest, ConnectionId_t aSender) noexcept
{
    RideSeats* pSeats = m_characters.SeatsOf(acRequest.MountId);
    if (m_characters.OwnerOf(acRequest.RiderId) != aSender || !pSeats ||
        m_characters.PlayerRecordOf(acRequest.MountId, m_characters.OwnerOf(acRequest.MountId)) || !pSeats->Rider ||
        pSeats->Rider == acRequest.RiderId)
    {
        spdlog::info("HostRides: {:x} cannot board {:x}: nobody else drives it", acRequest.RiderId, acRequest.MountId);
        NotifyMountRefused refused;
        static_cast<Payloads::Mount&>(refused) = acRequest;
        m_network.Send(aSender, refused, Net::kReliable);
        return;
    }
    auto& passengers = pSeats->Passengers;
    if (std::ranges::find(passengers, acRequest.RiderId) == passengers.end())
        passengers.push_back(acRequest.RiderId);
    spdlog::info("HostRides: {:x} rides along on {:x}, {:x} driving", acRequest.RiderId, acRequest.MountId, pSeats->Rider);
    // Its game seats it; its Ride part seats its copies (CharacterService).
    NotifyBoard notify;
    static_cast<Payloads::Mount&>(notify) = acRequest;
    m_network.Send(aSender, notify, Net::kReliable);
}

void HostRides::OnUnboard(const UnboardRequest& acRequest, ConnectionId_t aSender) noexcept
{
    RideSeats* pSeats = m_characters.SeatsOf(acRequest.MountId);
    if (m_characters.OwnerOf(acRequest.RiderId) != aSender || !pSeats)
        return;
    std::erase(pSeats->Passengers, acRequest.RiderId);
    SendUnboard(acRequest.RiderId, acRequest.MountId, aSender);
}

// Its ride ended with no exit clip (38705/40352, mounts.md): its copies need telling, as a lone rider's NotifyDismount.
void HostRides::SendUnboard(uint32_t aRiderId, uint32_t aMountId, ConnectionId_t aSender) noexcept
{
    NotifyUnboard notify;
    notify.RiderId = aRiderId;
    notify.MountId = aMountId;
    m_players.SendToOthers(notify, aSender, Net::kReliable);
}

bool HostRides::HandOver(uint32_t aMountId) noexcept
{
    RideSeats* pSeats = m_characters.SeatsOf(aMountId);
    if (!pSeats)
        return false;
    auto& passengers = pSeats->Passengers;
    while (!passengers.empty())
    {
        const uint32_t cNext = passengers.front();
        passengers.erase(passengers.begin());
        const ConnectionId_t cOwner = m_characters.OwnerOf(cNext);
        if (!cOwner)
            continue;
        // The ride's claim passes on: its rule sets the rider and tells every game.
        if (!m_claims.PassOn(aMountId, cOwner, cNext))
            continue;
        spdlog::info("HostRides: {:x} drives {:x} now, its game's", cNext, aMountId);
        return true;
    }
    return false;
}

void HostRides::EndRide(uint32_t aMountId, uint32_t aRiderId, bool aRelease) noexcept
{
    RideSeats* pSeats = m_characters.SeatsOf(aMountId);
    if (!pSeats)
        return;
    pSeats->Rider = 0;
    pSeats->Passengers.clear();
    NotifyDismount notify;
    notify.RiderId = aRiderId;
    notify.MountId = aMountId;
    m_players.SendToAll(notify);
    if (aRelease)
        m_claims.Release(aMountId);
}

void HostRides::SendRides(ConnectionId_t aPlayer) const noexcept
{
    m_characters.ForEachCharacter([&](uint32_t aSessionId, ConnectionId_t, NotifyCharacterSpawn*, RideSeats& aSeats) {
        if (aSeats.Rider)
        {
            NotifyMount notify;
            notify.RiderId = aSeats.Rider;
            notify.MountId = aSessionId;
            m_network.Send(aPlayer, notify, Net::kReliable); // passengers: their Ride parts, on their spawns
        }
    });
}

bool HostRides::RideGranted(uint32_t aRider, const RecordPart& acPart) const noexcept
{
    RidePart ride;
    if (acPart.Kind != RecordKind::kRide || (FromPartBytes(acPart.Bytes, ride) && !ride.Mount))
        return true;
    const RideSeats* cpSeats = m_characters.SeatsOf(ride.Mount);
    return cpSeats && std::ranges::find(cpSeats->Passengers, aRider) != cpSeats->Passengers.end();
}

void HostRides::Removed(uint32_t aSessionId) noexcept
{
    // A rider gone: its first passenger drives, else the mount is the host's again. A passenger gone: off the list.
    std::vector<uint32_t> ridden;
    m_characters.ForEachCharacter([&](uint32_t aMountId, ConnectionId_t, NotifyCharacterSpawn*, RideSeats& aSeats) {
        std::erase(aSeats.Passengers, aSessionId);
        if (aSeats.Rider == aSessionId)
            ridden.push_back(aMountId);
    });
    for (const uint32_t cMount : ridden)
        if (!HandOver(cMount))
            EndRide(cMount, aSessionId, true);
}

void HostRides::PlayerLeft(ConnectionId_t aPlayer, uint32_t aRider) noexcept
{
    std::vector<uint32_t> ridden;
    m_characters.ForEachCharacter([&](uint32_t aMountId, ConnectionId_t aOwner, NotifyCharacterSpawn*, RideSeats& aSeats) {
        if (aOwner == aPlayer && aSeats.Rider)
            ridden.push_back(aMountId);
    });
    // A mount it rides: its first passenger's, else the host's again, the rider gone.
    for (const uint32_t cMount : ridden)
        if (!HandOver(cMount))
            EndRide(cMount, aRider, true);
}
