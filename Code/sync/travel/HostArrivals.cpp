#include <travel/HostArrivals.h>

#include <characters/HostCharacters.h>
#include <core/HostNetwork.h>
#include <core/HostPlayers.h>
#include <core/HostRouter.h>
#include <movement/SnapshotAge.h>

#include <Messages/EnterCellRequest.h>
#include <Messages/TravelMessages.h>
#include <Structs/GridCellCoords.h>

HostArrivals::HostArrivals(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, HostCharacters& aCharacters) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(aCharacters)
{
    aRouter.On(*this, &HostArrivals::OnEnterCell);
    aRouter.On(*this, &HostArrivals::OnTravelQueued);
    aRouter.On(*this, &HostArrivals::OnPartySummon);
}

void HostArrivals::Joined(ConnectionId_t aPlayer, const Vector3_NetQuantize& acPosition) noexcept
{
    if (!WithHost(aPlayer) || !InHostGrid(acPosition))
        BringToParty(aPlayer);
}

void HostArrivals::PlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_places.erase(aPlayer);
    m_aheadOfHost.erase(aPlayer);
    m_broughtToParty.erase(aPlayer);
    m_announced.erase(aPlayer);
}

void HostArrivals::OnEnterCell(const EnterCellRequest& acRequest, ConnectionId_t aSender) noexcept
{
    m_places[aSender] = PlaceOf(acRequest.CellId, acRequest.WorldSpaceId);
    const auto cAnnounced = m_announced.find(aSender);
    const bool cWasAnnounced = cAnnounced != m_announced.end() && At(cAnnounced->second, acRequest);
    if (cAnnounced != m_announced.end())
        m_announced.erase(cAnnounced);
    // The party tether: whoever travels by itself takes everyone along, the host too, inside its cell as well
    // (docs/design/party-travel.md, "Every teleport"). A trip announced before its load was sent then.
    if (acRequest.How == EnterCellRequest::Travel::kOwn)
    {
        NotifyPartyTravel travel;
        travel.CellId = acRequest.CellId;
        travel.WorldSpaceId = acRequest.WorldSpaceId;
        travel.Position = acRequest.Position;
        travel.Heading = acRequest.Heading;
        if (!cWasAnnounced)
            m_players.SendToOthers(travel, aSender, Net::kReliable);
        m_partyTravel = travel;
    }
    // A follower that arrived where the party is not (two players led at once, and it followed the one that led
    // first): sent on to where the party went last. Not seen or reproduced yet; a guard for that race.
    // One brought to the host's player (a join away from it, the leash) went there, not to the party's last trip.
    else if (acRequest.How == EnterCellRequest::Travel::kTether && m_partyTravel && !m_broughtToParty.contains(aSender))
    {
        if (!At(*m_partyTravel, acRequest))
        {
            spdlog::info("HostArrivals: {:x} followed to {:x}:{:x}, the party is at {:x}:{:x}: sent on", aSender,
                         acRequest.CellId.ModId, acRequest.CellId.BaseId, m_partyTravel->CellId.ModId,
                         m_partyTravel->CellId.BaseId);
            m_network.Send(aSender, *m_partyTravel, Net::kReliable);
        }
    }
    m_characters.ForEachCharacter([&](uint32_t, ConnectionId_t aOwner, NotifyCharacterSpawn* apRecord, RideSeats&) {
        if (aOwner != aSender || !apRecord)
            return;
        // Newer movement here already: its place stands.
        if (!SnapshotAge::IsStale(acRequest.Tick, apRecord->Tick))
        {
            apRecord->Tick = SnapshotAge::Cutoff(acRequest.Tick, m_network.GetTick(), apRecord->Tick);
            apRecord->CellId = acRequest.CellId;
            apRecord->Position = acRequest.Position;
        }
        apRecord->LatestAction = acRequest.LatestAction;
        apRecord->GraphState = acRequest.GraphState;
        apRecord->InitialActorValues = acRequest.Data.InitialActorValues;
        apRecord->IsDead = acRequest.Data.IsDead;
    });
    // Ahead of the host: the host's actors there reach it once the host arrives.
    if (!WithHost(aSender))
    {
        m_aheadOfHost.insert(aSender);
        spdlog::info("HostArrivals: {:x} arrived in {:x}:{:x} ahead of the host", aSender, acRequest.CellId.ModId,
                     acRequest.CellId.BaseId);
        return;
    }
    m_aheadOfHost.erase(aSender);

    // The host's actors here, to it, or once the host arrives, to those ahead of it. The players' characters they
    // have already: their copies travel with the party (docs/design/copy-contract.md, "Players' copies").
    if (aSender != m_players.Host())
    {
        m_characters.SendHostActors(aSender);
        // Its arrival by the party's travel; where it was as it joined (kBeforeJoining) may be "with the host" too.
        if (acRequest.How == EnterCellRequest::Travel::kTether && m_broughtToParty.erase(aSender))
            m_characters.SendPlayersMet(aSender);
        return;
    }
    std::vector<ConnectionId_t> arrived;
    for (const ConnectionId_t cPlayer : m_aheadOfHost)
        if (WithHost(cPlayer))
            arrived.push_back(cPlayer);
    for (const ConnectionId_t cPlayer : arrived)
    {
        m_aheadOfHost.erase(cPlayer);
        m_characters.SendHostActors(cPlayer);
    }
}

bool HostArrivals::At(const NotifyPartyTravel& acPlace, const EnterCellRequest& acArrival) noexcept
{
    if (!acArrival.WorldSpaceId)
        return !acPlace.WorldSpaceId && acArrival.CellId == acPlace.CellId;
    return acArrival.WorldSpaceId == acPlace.WorldSpaceId &&
           GridCellCoords::IsCellInGridCell(GridCellCoords::CalculateGridCellCoords(acArrival.Position),
                                            GridCellCoords::CalculateGridCellCoords(acPlace.Position), false);
}

void HostArrivals::BringToParty(ConnectionId_t aPlayer) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    const auto* cpRecord = m_characters.PlayerRecordOf(m_characters.PlayerCharacterOf(cHost), cHost);
    const auto cHostPlace = m_places.find(cHost);
    if (!cpRecord || cHostPlace == m_places.end())
        return;
    // Where the host's player is now (its record follows its movement).
    NotifyPartyTravel travel;
    travel.CellId = cpRecord->CellId;
    travel.WorldSpaceId = cHostPlace->second.WorldSpaceId;
    travel.Position = cpRecord->Position;
    travel.Heading = cpRecord->Rotation.y;
    m_network.Send(aPlayer, travel, Net::kReliable);
    m_broughtToParty.insert(aPlayer);
    spdlog::info("HostArrivals: {:x} joined away from the host: sent to {:x}:{:x}", aPlayer, travel.CellId.ModId,
                 travel.CellId.BaseId);
}

HostArrivals::Place HostArrivals::PlaceOf(GameId aCellId, GameId aWorldSpaceId) noexcept
{
    // Outdoors the cell changes with every step; the worldspace is the place.
    return aWorldSpaceId ? Place{aWorldSpaceId, {}} : Place{{}, aCellId};
}

bool HostArrivals::WithHost(ConnectionId_t aPlayer) const noexcept
{
    const auto cPlayer = m_places.find(aPlayer);
    const auto cHost = m_places.find(m_players.Host());
    // Not announced yet: it is joining, where the host is.
    if (cPlayer == m_places.end() || cHost == m_places.end())
        return true;
    return cPlayer->second == cHost->second;
}

bool HostArrivals::InHostGrid(const Vector3_NetQuantize& acPosition) const noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    const auto* cpRecord = m_characters.PlayerRecordOf(m_characters.PlayerCharacterOf(cHost), cHost);
    const auto cHostPlace = m_places.find(cHost);
    if (!cpRecord || cHostPlace == m_places.end() || !cHostPlace->second.WorldSpaceId)
        return true;
    return GridCellCoords::IsCellInGridCell(GridCellCoords::CalculateGridCellCoords(acPosition),
                                            GridCellCoords::CalculateGridCellCoords(cpRecord->Position), false);
}

std::optional<NotifyPartyTravel> HostArrivals::CharacterPlace(uint32_t aSessionId) const noexcept
{
    const ConnectionId_t cOwner = m_characters.OwnerOf(aSessionId);
    const auto* cpRecord = m_characters.PlayerRecordOf(aSessionId, cOwner);
    if (!cpRecord)
        return std::nullopt;
    // The host's own player's record is its announcement's: its engine has where it is now.
    const auto cBody = cOwner == m_players.Host() ? m_characters.BuildSpawn(aSessionId) : std::optional(*cpRecord);
    const auto cPlace = m_places.find(cOwner);
    if (!cBody || cPlace == m_places.end())
        return std::nullopt;
    NotifyPartyTravel place;
    place.WorldSpaceId = cPlace->second.WorldSpaceId;
    place.CellId = cBody->CellId;
    place.Position = cBody->Position;
    place.Heading = cBody->Rotation.y;
    return place;
}

std::optional<HostArrivals::Outdoors> HostArrivals::OutdoorsAt(ConnectionId_t aPlayer) const noexcept
{
    const auto cPlace = m_places.find(aPlayer);
    const auto* cpRecord = m_characters.PlayerRecordOf(m_characters.PlayerCharacterOf(aPlayer), aPlayer);
    if (cPlace == m_places.end() || !cPlace->second.WorldSpaceId || !cpRecord || m_aheadOfHost.contains(aPlayer) ||
        m_broughtToParty.contains(aPlayer) || m_announced.contains(aPlayer))
        return std::nullopt;
    return Outdoors{cPlace->second.WorldSpaceId, cpRecord->Position};
}

void HostArrivals::OnTravelQueued(const PartyTravelRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (!m_characters.PlayerCharacterOf(aSender))
        return;
    m_partyTravel = acRequest.Destination;
    m_announced[aSender] = acRequest.Destination;
    m_players.SendToOthers(*m_partyTravel, aSender, Net::kReliableNoNagle);
}

void HostArrivals::OnPartySummon(const PartySummonRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (!m_characters.PlayerCharacterOf(aSender))
        return;
    m_partyTravel = acRequest.Destination;
    NotifyPartySummon summon;
    summon.Destination = acRequest.Destination;
    summon.Username = m_players.PlayerNameOf(aSender);
    m_players.SendToOthers(summon, aSender, Net::kReliableNoNagle);
    spdlog::info("HostArrivals: {:x} brought the party to cell {:x}:{:x}", aSender, summon.Destination.CellId.ModId,
                 summon.Destination.CellId.BaseId);
}
