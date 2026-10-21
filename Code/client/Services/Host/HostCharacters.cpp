#include <Services/Host/HostCharacters.h>

#include <GameServer.h>
#include <Services/Host/HostAuthority.h>

#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CancelAssignmentRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/EnterCellRequest.h>
#include <Messages/NotifyPartyTravel.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Structs/GridCellCoords.h>

#include <limits>

namespace
{
// For OwnSpawns: exclude no one (0 is a real id, usually the host's player).
constexpr uint32_t cNoCharacter = std::numeric_limits<uint32_t>::max();
} // namespace

HostCharacters::HostCharacters(HostAuthority& aAuthority, Server::GameServer& aServer, const HostPlayers& acPlayers, const HostGame& acGame) noexcept
    : m_authority(aAuthority)
    , m_server(aServer)
    , m_players(acPlayers)
    , m_game(acGame)
{
    aAuthority.On(*this, &HostCharacters::OnAssignCharacter);
    aAuthority.On(*this, &HostCharacters::OnCancelAssignment);
    aAuthority.On(*this, &HostCharacters::OnRemoveCharacter);
    aAuthority.On(*this, &HostCharacters::OnRespawn);
    aAuthority.On(*this, &HostCharacters::OnPlayerRespawn);
    aAuthority.On(*this, &HostCharacters::OnMovement);
    aAuthority.On(*this, &HostCharacters::OnEnterCell);
}

void HostCharacters::OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept
{
    ServerReferencesMoveRequest notify;
    // The owner's time, as its actions carry: copies replay both on one timeline (never in the future).
    notify.Tick = std::min(acRequest.Tick, m_server.GetTick());
    for (const auto& [cServerId, cUpdate] : acRequest.Updates)
    {
        auto it = m_characters.find(cServerId);
        if (it == m_characters.end() || it->second.Owner != aSender)
        {
            spdlog::debug("HostCharacters: {:x} moved {:x}, which it does not own", aSender, cServerId);
            continue;
        }
        notify.Updates[cServerId] = cUpdate;
        if (auto& record = it.value().PlayerRecord)
        {
            record->CellId = cUpdate.UpdatedMovement.CellId;
            record->Position = cUpdate.UpdatedMovement.Position;
            record->Rotation = cUpdate.UpdatedMovement.Rotation;
        }
    }
    if (!notify.Updates.empty())
        m_players.SendToOthers(notify, aSender, TiltedPhoques::kUnreliable);
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

    if (cIsPlayer)
    {
        // A player has one character: an earlier one (re-announced after a respawn) goes.
        Vector<uint32_t> previous;
        for (const auto& [cServerId, cCharacter] : m_characters)
            if (cCharacter.Owner == aSender && cCharacter.PlayerRecord)
                previous.push_back(cServerId);
        for (const uint32_t cServerId : previous)
            RemoveCharacter(cServerId);
    }

    const uint32_t cServerId = m_server.NewCharacterId();
    const uint32_t cPlayerId = cIsPlayer ? m_players.PlayerIdOf(aSender) : 0;
    const auto cSpawn = CharacterSpawnRequest::FromAssignment(acRequest, cServerId, cPlayerId);

    auto& character = m_characters[cServerId];
    character.Owner = aSender;
    character.Cookie = acRequest.Cookie;
    if (cIsPlayer)
    {
        m_places[aSender] = PlaceOf(acRequest.CellId, acRequest.WorldSpaceId);
        character.PlayerRecord = cSpawn;
        m_server.SetPlayerCharacter(aSender, cServerId, acRequest.QuestContent);
        if (aSender != m_players.Host())
            m_authority.PlayerEntered(aSender, cServerId);
    }

    AssignCharacterResponse response;
    response.Cookie = acRequest.Cookie;
    response.ServerId = cServerId;
    response.PlayerId = cPlayerId;
    response.Owner = true;
    m_server.Send(aSender, response);

    SendToOthersWithHost(cSpawn, aSender);
    spdlog::info("HostCharacters: {:x} announced {:x}:{:x} as {:x}", aSender, acRequest.ReferenceId.ModId, acRequest.ReferenceId.BaseId, cServerId);
}

void HostCharacters::OnCancelAssignment(const CancelAssignmentRequest& acRequest, ConnectionId_t aSender) noexcept
{
    for (const auto& [cServerId, cCharacter] : m_characters)
    {
        if (cCharacter.Owner == aSender && cCharacter.Cookie == acRequest.Cookie)
        {
            RemoveCharacter(cServerId);
            return;
        }
    }
}

void HostCharacters::OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept
{
    if (OwnerOf(acRequest.ServerId) == aSender)
        RemoveCharacter(acRequest.ServerId);
}

void HostCharacters::OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept
{
    auto it = m_characters.find(acRequest.ActorId);
    if (it == m_characters.end())
        return;

    if (it->second.Owner != aSender)
    {
        if (const auto cSpawn = BuildSpawn(acRequest.ActorId))
            m_server.Send(aSender, *cSpawn);
        return;
    }

    if (auto& record = it.value().PlayerRecord)
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
    m_players.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
}

void HostCharacters::OnPlayerRespawn(const PlayerRespawnRequest&, ConnectionId_t aSender) const noexcept
{
    // Its copies come back from death badly otherwise: everyone else spawns it again.
    for (const auto& [cServerId, cCharacter] : m_characters)
    {
        if (cCharacter.Owner == aSender && cCharacter.PlayerRecord)
        {
            NotifyRespawn notify;
            notify.ActorId = cServerId;
            m_players.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
        }
    }
}

void HostCharacters::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    m_places.erase(aPlayer);
    m_aheadOfHost.erase(aPlayer);
    Vector<uint32_t> owned;
    for (const auto& [cServerId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer)
            owned.push_back(cServerId);
    for (const uint32_t cServerId : owned)
        RemoveCharacter(cServerId);
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
        m_players.SendToOthers(travel, aSender, TiltedPhoques::kReliable);
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
            m_server.Send(aSender, *m_partyTravel, TiltedPhoques::kReliable);
        }
    }
    for (auto it = m_characters.begin(); it != m_characters.end(); ++it)
    {
        auto& record = it.value().PlayerRecord;
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
        return;
    }
    TiltedPhoques::Vector<ConnectionId_t> arrived;
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
    for (const auto& [cServerId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer && cCharacter.PlayerRecord)
            return cServerId;
    return cNoCharacter;
}

void HostCharacters::SendHostActors(ConnectionId_t aPlayer) const noexcept
{
    uint32_t count = 0;
    for (const auto& cSpawn : m_game.OwnSpawns(PlayerCharacterOf(m_players.Host())))
    {
        m_server.Send(aPlayer, cSpawn);
        ++count;
    }
    spdlog::info("HostCharacters: sent {:x} the host's {} actors where it arrived", aPlayer, count);
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

void HostCharacters::SendToOthersWithHost(const CharacterSpawnRequest& acSpawn, ConnectionId_t aSender) const noexcept
{
    for (const ConnectionId_t cPlayer : m_players.Connections())
        if (cPlayer != aSender && WithHost(cPlayer))
            m_server.Send(cPlayer, acSpawn, TiltedPhoques::kReliable);
}

ConnectionId_t HostCharacters::OwnerOf(uint32_t aServerId) const noexcept
{
    const auto it = m_characters.find(aServerId);
    return it != m_characters.end() ? it->second.Owner : 0;
}

std::optional<CharacterSpawnRequest> HostCharacters::BuildSpawn(uint32_t aServerId) const noexcept
{
    const auto it = m_characters.find(aServerId);
    if (it == m_characters.end())
        return std::nullopt;

    if (it->second.PlayerRecord && it->second.Owner != m_players.Host())
    {
        auto spawn = *it->second.PlayerRecord;
        m_game.ReadCopy(aServerId, spawn); // else as recorded
        return spawn;
    }

    for (auto& spawn : m_game.OwnSpawns(cNoCharacter))
        if (spawn.ServerId == aServerId)
            return std::move(spawn);
    return std::nullopt;
}

void HostCharacters::SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnServerId) const noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    uint32_t count = 0;
    for (const auto& cSpawn : m_game.OwnSpawns(aOwnServerId))
    {
        m_server.Send(aPlayer, cSpawn);
        ++count;
    }
    for (const auto& [cServerId, cCharacter] : m_characters)
    {
        if (cServerId == aOwnServerId || cCharacter.Owner == cHost || !cCharacter.PlayerRecord || !WithHost(cCharacter.Owner))
            continue;
        if (const auto cSpawn = BuildSpawn(cServerId))
        {
            m_server.Send(aPlayer, *cSpawn);
            ++count;
        }
    }
    spdlog::info("HostCharacters: {} characters for the player who joined ({:x})", count, aPlayer);
}

void HostCharacters::RemoveCharacter(uint32_t aServerId) noexcept
{
    const auto it = m_characters.find(aServerId);
    if (it == m_characters.end())
        return;

    NotifyRemoveCharacter notify;
    notify.ServerId = aServerId;
    m_players.SendToOthers(notify, it->second.Owner, TiltedPhoques::kReliable);

    m_characters.erase(it);
    m_server.FreeCharacterId(aServerId);
}
