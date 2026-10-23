#include <Services/Host/HostCharacters.h>

#include <Services/Host/HostNetwork.h>
#include <Services/Host/HostAuthority.h>

#include <Messages/AssignCharacterRequest.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyPartyTravel.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>
#include <Messages/NotifyReferencesMove.h>
#include <Structs/GridCellCoords.h>


namespace
{
// For OwnSpawns: exclude no one (ids start at 1).
constexpr uint32_t cNoCharacter = 0;
} // namespace

HostCharacters::HostCharacters(HostAuthority& aAuthority, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_authority(aAuthority)
    , m_network(aNetwork)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostCharacters::OnAssignCharacter);
    aAuthority.On(*this, &HostCharacters::OnRemoveCharacter);
    aAuthority.On(*this, &HostCharacters::OnRespawn);
    aAuthority.On(*this, &HostCharacters::OnPlayerRespawn);
    aAuthority.On(*this, &HostCharacters::OnMovement);
    aAuthority.On(*this, &HostCharacters::OnEnterCell);
}

void HostCharacters::OnMovement(const ReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept
{
    NotifyReferencesMove notify;
    // The owner's time, as its actions carry: copies replay both on one timeline (never in the future).
    notify.Tick = std::min(acRequest.Tick, m_network.GetTick());
    for (const auto& [cSessionId, cUpdate] : acRequest.Updates)
    {
        auto it = m_characters.find(cSessionId);
        if (it == m_characters.end() || it->second.Owner != aSender)
        {
            spdlog::debug("HostCharacters: {:x} moved {:x}, which it does not own", aSender, cSessionId);
            continue;
        }
        notify.Updates[cSessionId] = cUpdate;
        if (auto& record = it->second.PlayerRecord)
        {
            record->CellId = cUpdate.UpdatedMovement.CellId;
            record->Position = cUpdate.UpdatedMovement.Position;
            record->Rotation = cUpdate.UpdatedMovement.Rotation;
        }
    }
    if (!notify.Updates.empty())
        m_players.SendToOthers(notify, aSender, Net::kUnreliable);
}

void HostCharacters::OnAssignCharacter(const AssignCharacterRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const bool cIsPlayer = acRequest.ReferenceId == GameId{0, 0x14};
    if (!cIsPlayer && aSender != m_players.Host())
    {
        // Under the party tether every actor but a player's own is in the host's game.
        spdlog::warn("HostCharacters: {:x} announced {:x}:{:x}, which only the host may", aSender, acRequest.ReferenceId.ModId, acRequest.ReferenceId.BaseId);
        return;
    }

    const uint32_t cSessionId = acRequest.SessionId;
    if (cIsPlayer)
    {
        const auto cReserved = m_playerCharacters.find(aSender);
        if (cReserved == m_playerCharacters.end() || cReserved->second != cSessionId)
        {
            spdlog::warn("HostCharacters: {:x} announced its player as {:x}, not the id its join gave it", aSender, cSessionId);
            return;
        }
        // A player has one character: announced again (after a respawn), its copies are made again.
        if (m_characters.contains(cSessionId))
            RemoveCharacter(cSessionId);
    }
    else if (cSessionId == 0 || cSessionId > m_lastSessionId || m_characters.contains(cSessionId) || IsPlayerCharacter(cSessionId))
    {
        spdlog::warn("HostCharacters: the host's game announced {:x}:{:x} as {:x}, an id it was not given", acRequest.ReferenceId.ModId,
                     acRequest.ReferenceId.BaseId, cSessionId);
        return;
    }

    const uint32_t cPlayerId = cIsPlayer ? m_players.PlayerIdOf(aSender) : 0;
    const auto cSpawn = NotifyCharacterSpawn::FromAssignment(acRequest, cSessionId, cPlayerId);

    auto& character = m_characters[cSessionId];
    character.Owner = aSender;
    if (cIsPlayer)
    {
        m_places[aSender] = PlaceOf(acRequest.CellId, acRequest.WorldSpaceId);
        character.PlayerRecord = cSpawn;
        if (aSender != m_players.Host())
        {
            m_authority.PlayerEntered(aSender, cSessionId);
            if (!WithHost(aSender) || !InHostGrid(acRequest.Position))
                BringToParty(aSender);
        }
    }

    SendToOthersWithHost(cSpawn, aSender);
    spdlog::info("HostCharacters: {:x} announced {:x}:{:x} as {:x}", aSender, acRequest.ReferenceId.ModId, acRequest.ReferenceId.BaseId, cSessionId);
}

void HostCharacters::OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept
{
    if (OwnerOf(acRequest.SessionId) == aSender)
        RemoveCharacter(acRequest.SessionId, acRequest.Whereabouts);
}

void HostCharacters::OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept
{
    auto it = m_characters.find(acRequest.ActorId);
    if (it == m_characters.end())
        return;

    if (it->second.Owner != aSender)
    {
        if (const auto cSpawn = BuildSpawn(acRequest.ActorId))
            m_network.Send(aSender, *cSpawn);
        return;
    }

    if (auto& record = it->second.PlayerRecord)
    {
        // The respawned copies start from the record: the owner's state now, else the host's copy's while it exists.
        if (acRequest.HasState)
        {
            record->LatestAction = acRequest.LatestAction;
            record->GraphState = acRequest.GraphState;
            record->InventoryContent = acRequest.Data.InitialInventory;
            record->InitialActorValues = acRequest.Data.InitialActorValues;
            record->IsDead = acRequest.Data.IsDead;
            record->IsWeaponDrawn = acRequest.Data.IsWeaponDrawn;
        }
        else
            m_game.ReadCopy(acRequest.ActorId, *record);
        if (!acRequest.AppearanceBuffer.empty())
        {
            record->AppearanceBuffer = acRequest.AppearanceBuffer;
            record->ChangeFlags = acRequest.ChangeFlags;
        }
    }
    spdlog::info("HostCharacters: {:x} respawns its {}", aSender, acRequest.ActorId);
    NotifyRespawn notify;
    notify.ActorId = acRequest.ActorId;
    m_players.SendToOthers(notify, aSender, Net::kReliable);
}

void HostCharacters::OnPlayerRespawn(const PlayerRespawnRequest&, ConnectionId_t aSender) const noexcept
{
    // Its copies come back from death badly otherwise: everyone else spawns it again.
    for (const auto& [cSessionId, cCharacter] : m_characters)
    {
        if (cCharacter.Owner == aSender && cCharacter.PlayerRecord)
        {
            NotifyRespawn notify;
            notify.ActorId = cSessionId;
            m_players.SendToOthers(notify, aSender, Net::kReliable);
        }
    }
}

void HostCharacters::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_playerCharacters.erase(aPlayer);
    m_places.erase(aPlayer);
    m_aheadOfHost.erase(aPlayer);
    m_broughtToParty.erase(aPlayer);
    std::vector<uint32_t> owned;
    for (const auto& [cSessionId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer)
            owned.push_back(cSessionId);
    for (const uint32_t cSessionId : owned)
        RemoveCharacter(cSessionId);
}

void HostCharacters::OnEnterCell(const EnterCellRequest& acRequest, ConnectionId_t aSender) noexcept
{
    m_places[aSender] = PlaceOf(acRequest.CellId, acRequest.WorldSpaceId);
    // The party tether: whoever travels by itself takes everyone along, the host too
    // (docs/design/host-authority-migration.md, "Party tether").
    if (acRequest.How == EnterCellRequest::Travel::kOwn)
    {
        NotifyPartyTravel travel;
        travel.CellId = acRequest.CellId;
        travel.WorldSpaceId = acRequest.WorldSpaceId;
        travel.Position = acRequest.Position;
        travel.Heading = acRequest.Heading;
        m_players.SendToOthers(travel, aSender, Net::kReliable);
        m_partyTravel = travel;
    }
    // A follower that arrived where the party is not (two players led at once, and it followed the one that led
    // first): sent on to where the party went last. Not seen or reproduced yet; a guard for that race.
    else if (acRequest.How == EnterCellRequest::Travel::kTether && m_partyTravel)
    {
        const bool cThere = acRequest.WorldSpaceId
                                ? acRequest.WorldSpaceId == m_partyTravel->WorldSpaceId &&
                                      GridCellCoords::IsCellInGridCell(
                                          GridCellCoords::CalculateGridCellCoords(acRequest.Position),
                                          GridCellCoords::CalculateGridCellCoords(m_partyTravel->Position), false)
                                : !m_partyTravel->WorldSpaceId && acRequest.CellId == m_partyTravel->CellId;
        if (!cThere)
        {
            spdlog::info("HostCharacters: {:x} followed to {:x}:{:x}, the party is at {:x}:{:x}: sent on", aSender,
                         acRequest.CellId.ModId, acRequest.CellId.BaseId, m_partyTravel->CellId.ModId,
                         m_partyTravel->CellId.BaseId);
            m_network.Send(aSender, *m_partyTravel, Net::kReliable);
        }
    }
    for (auto it = m_characters.begin(); it != m_characters.end(); ++it)
    {
        auto& record = it->second.PlayerRecord;
        if (it->second.Owner == aSender && record)
        {
            record->CellId = acRequest.CellId;
            record->Position = acRequest.Position;
            record->LatestAction = acRequest.LatestAction;
            record->GraphState = acRequest.GraphState;
            record->InventoryContent = acRequest.Data.InitialInventory;
            record->InitialActorValues = acRequest.Data.InitialActorValues;
            record->IsDead = acRequest.Data.IsDead;
            record->IsWeaponDrawn = acRequest.Data.IsWeaponDrawn;
        }
    }
    // Ahead of the host: the host's actors there reach it once the host arrives.
    if (!WithHost(aSender))
    {
        m_aheadOfHost.insert(aSender);
        spdlog::info("HostCharacters: {:x} arrived in {:x}:{:x} ahead of the host", aSender, acRequest.CellId.ModId,
                     acRequest.CellId.BaseId);
        return;
    }
    m_aheadOfHost.erase(aSender);

    // The host's actors here, to it, or once the host arrives, to those ahead of it. The players' characters they
    // have already: their copies travel with the party (docs/design/copy-contract.md, "Players' copies").
    if (aSender != m_players.Host())
    {
        SendHostActors(aSender);
        // Its arrival by the party's travel; where it was as it joined (kBeforeJoining) may be "with the host" too.
        if (acRequest.How == EnterCellRequest::Travel::kTether && m_broughtToParty.erase(aSender))
            SendPlayersMet(aSender);
        return;
    }
    std::vector<ConnectionId_t> arrived;
    for (const ConnectionId_t cPlayer : m_aheadOfHost)
        if (WithHost(cPlayer))
            arrived.push_back(cPlayer);
    for (const ConnectionId_t cPlayer : arrived)
    {
        m_aheadOfHost.erase(cPlayer);
        SendHostActors(cPlayer);
    }
}

uint32_t HostCharacters::PlayerCharacterOf(ConnectionId_t aPlayer) const noexcept
{
    for (const auto& [cSessionId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer && cCharacter.PlayerRecord)
            return cSessionId;
    return cNoCharacter;
}

uint32_t HostCharacters::NewCharacterId() noexcept
{
    return ++m_lastSessionId;
}

uint32_t HostCharacters::ReservePlayerCharacter(ConnectionId_t aPlayer) noexcept
{
    return m_playerCharacters[aPlayer] = NewCharacterId();
}

bool HostCharacters::IsPlayerCharacter(uint32_t aSessionId) const noexcept
{
    return std::ranges::any_of(m_playerCharacters, [aSessionId](const auto& acEntry) { return acEntry.second == aSessionId; });
}

void HostCharacters::BringToParty(ConnectionId_t aPlayer) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    const auto cHostCharacter = m_characters.find(PlayerCharacterOf(cHost));
    const auto cHostPlace = m_places.find(cHost);
    if (cHostCharacter == m_characters.end() || cHostPlace == m_places.end())
        return;
    // Where the host's player is now (its record follows its movement).
    const auto& cRecord = *cHostCharacter->second.PlayerRecord;
    NotifyPartyTravel travel;
    travel.CellId = cRecord.CellId;
    travel.WorldSpaceId = cHostPlace->second.WorldSpaceId;
    travel.Position = cRecord.Position;
    travel.Heading = cRecord.Rotation.y;
    m_network.Send(aPlayer, travel, Net::kReliable);
    m_broughtToParty.insert(aPlayer);
    spdlog::info("HostCharacters: {:x} joined away from the host: sent to {:x}:{:x}", aPlayer, travel.CellId.ModId,
                 travel.CellId.BaseId);
}

void HostCharacters::SendPlayersMet(ConnectionId_t aPlayer) noexcept
{
    if (const auto cSpawn = BuildSpawn(PlayerCharacterOf(aPlayer)))
        SendToOthersWithHost(*cSpawn, aPlayer);
    size_t count = 0;
    for (const auto& [cSessionId, cCharacter] : m_characters)
        if (cCharacter.PlayerRecord && cCharacter.Owner != aPlayer && WithHost(cCharacter.Owner))
            if (const auto cSpawn = BuildSpawn(cSessionId))
            {
                m_network.Send(aPlayer, *cSpawn);
                ++count;
            }
    spdlog::info("HostCharacters: {:x} reached the party it was brought to: {} players' characters each way", aPlayer,
                 count);
}

void HostCharacters::SendHostActors(ConnectionId_t aPlayer) noexcept
{
    const size_t cCount = SendHostSpawns(aPlayer, PlayerCharacterOf(m_players.Host()));
    spdlog::info("HostCharacters: sent {:x} the host's {} actors where it arrived", aPlayer, cCount);
}

size_t HostCharacters::SendHostSpawns(ConnectionId_t aPlayer, uint32_t aExcludedSessionId) noexcept
{
    const auto cSpawns = m_game.OwnSpawns(aExcludedSessionId);
    for (const auto& cSpawn : cSpawns)
        m_network.Send(aPlayer, cSpawn);
    return cSpawns.size();
}

HostCharacters::Place HostCharacters::PlaceOf(GameId aCellId, GameId aWorldSpaceId) noexcept
{
    // Outdoors the cell changes with every step; the worldspace is the place.
    return aWorldSpaceId ? Place{aWorldSpaceId, {}} : Place{{}, aCellId};
}

bool HostCharacters::WithHost(ConnectionId_t aPlayer) const noexcept
{
    const auto cPlayer = m_places.find(aPlayer);
    const auto cHost = m_places.find(m_players.Host());
    // Not announced yet: it is joining, where the host is.
    if (cPlayer == m_places.end() || cHost == m_places.end())
        return true;
    return cPlayer->second == cHost->second;
}

bool HostCharacters::InHostGrid(const Vector3_NetQuantize& acPosition) const noexcept
{
    const auto cHostCharacter = m_characters.find(PlayerCharacterOf(m_players.Host()));
    const auto cHostPlace = m_places.find(m_players.Host());
    if (cHostCharacter == m_characters.end() || cHostPlace == m_places.end() || !cHostPlace->second.WorldSpaceId)
        return true;
    return GridCellCoords::IsCellInGridCell(GridCellCoords::CalculateGridCellCoords(acPosition),
                                            GridCellCoords::CalculateGridCellCoords(cHostCharacter->second.PlayerRecord->Position),
                                            false);
}

void HostCharacters::SendToOthersWithHost(const NotifyCharacterSpawn& acSpawn, ConnectionId_t aSender) const noexcept
{
    for (const ConnectionId_t cPlayer : m_players.Connections())
        if (cPlayer != aSender && WithHost(cPlayer))
            m_network.Send(cPlayer, acSpawn, Net::kReliable);
}

HostCharacters::ConnectionId_t HostCharacters::OwnerOf(uint32_t aSessionId) const noexcept
{
    const auto it = m_characters.find(aSessionId);
    return it != m_characters.end() ? it->second.Owner : 0;
}

std::optional<NotifyCharacterSpawn> HostCharacters::BuildSpawn(uint32_t aSessionId) const noexcept
{
    const auto it = m_characters.find(aSessionId);
    if (it == m_characters.end())
        return std::nullopt;

    if (it->second.PlayerRecord && it->second.Owner != m_players.Host())
    {
        auto spawn = *it->second.PlayerRecord;
        m_game.ReadCopy(aSessionId, spawn); // else as recorded
        return spawn;
    }

    for (auto& spawn : m_game.OwnSpawns(cNoCharacter))
        if (spawn.SessionId == aSessionId)
            return std::move(spawn);
    return std::nullopt;
}

void HostCharacters::SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnSessionId) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    size_t count = SendHostSpawns(aPlayer, aOwnSessionId);
    for (const auto& [cSessionId, cCharacter] : m_characters)
    {
        if (cSessionId == aOwnSessionId || cCharacter.Owner == cHost || !cCharacter.PlayerRecord || !WithHost(cCharacter.Owner))
            continue;
        if (const auto cSpawn = BuildSpawn(cSessionId))
        {
            m_network.Send(aPlayer, *cSpawn);
            ++count;
        }
    }
    spdlog::info("HostCharacters: {} characters for the player who joined ({:x})", count, aPlayer);
}

void HostCharacters::RemoveCharacter(uint32_t aSessionId, const ActorWhereabouts& acWhereabouts) noexcept
{
    const auto it = m_characters.find(aSessionId);
    if (it == m_characters.end())
        return;

    NotifyRemoveCharacter notify;
    notify.SessionId = aSessionId;
    notify.Whereabouts = acWhereabouts;
    m_players.SendToOthers(notify, it->second.Owner, Net::kReliable);

    m_characters.erase(it);
}
