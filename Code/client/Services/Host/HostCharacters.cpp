#include <Services/Host/HostCharacters.h>

#include <GameServer.h>
#include <Services/Host/HostBridge.h>

#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CancelAssignmentRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>
#include <Messages/ServerReferencesMoveRequest.h>

HostCharacters::HostCharacters(HostBridge& aBridge, Server::GameServer& aServer, const HostGame& acGame) noexcept
    : m_bridge(aBridge)
    , m_server(aServer)
    , m_game(acGame)
{
    aBridge.On(*this, &HostCharacters::OnAssignCharacter);
    aBridge.On(*this, &HostCharacters::OnCancelAssignment);
    aBridge.On(*this, &HostCharacters::OnRemoveCharacter);
    aBridge.On(*this, &HostCharacters::OnRespawn);
    aBridge.On(*this, &HostCharacters::OnPlayerRespawn);
    aBridge.On(*this, &HostCharacters::OnMovement);
}

void HostCharacters::OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept
{
    ServerReferencesMoveRequest notify;
    notify.Tick = m_server.GetTick();
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
        m_server.SendToOthers(notify, aSender, TiltedPhoques::kUnreliable);
}

void HostCharacters::OnAssignCharacter(const AssignCharacterRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const bool cIsPlayer = acRequest.ReferenceId == GameId{0, 0x14};
    if (!cIsPlayer && aSender != m_server.HostConnection())
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
    const uint32_t cPlayerId = cIsPlayer ? m_server.PlayerIdOf(aSender) : 0;
    const auto cSpawn = CharacterSpawnRequest::FromAssignment(acRequest, cServerId, cPlayerId);

    auto& character = m_characters[cServerId];
    character.Owner = aSender;
    character.Cookie = acRequest.Cookie;
    if (cIsPlayer)
    {
        character.PlayerRecord = cSpawn;
        m_server.SetPlayerCharacter(aSender, cServerId, acRequest.QuestContent);
        if (aSender != m_server.HostConnection())
            m_bridge.PlayerEntered(aSender, cServerId);
    }

    AssignCharacterResponse response;
    response.Cookie = acRequest.Cookie;
    response.ServerId = cServerId;
    response.PlayerId = cPlayerId;
    response.Owner = true;
    m_server.Send(aSender, response);

    m_server.SendToOthers(cSpawn, aSender, TiltedPhoques::kReliable);
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

    if (auto& record = it.value().PlayerRecord; record && !acRequest.AppearanceBuffer.empty())
    {
        record->AppearanceBuffer = acRequest.AppearanceBuffer;
        record->ChangeFlags = acRequest.ChangeFlags;
    }
    NotifyRespawn notify;
    notify.ActorId = acRequest.ActorId;
    m_server.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
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
            m_server.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
        }
    }
}

void HostCharacters::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    Vector<uint32_t> owned;
    for (const auto& [cServerId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer)
            owned.push_back(cServerId);
    for (const uint32_t cServerId : owned)
        RemoveCharacter(cServerId);
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

    if (it->second.PlayerRecord && it->second.Owner != m_server.HostConnection())
    {
        auto spawn = *it->second.PlayerRecord;
        m_game.ReadCopy(aServerId, spawn); // else as recorded
        return spawn;
    }

    for (auto& spawn : m_game.OwnSpawns(0))
        if (spawn.ServerId == aServerId)
            return std::move(spawn);
    return std::nullopt;
}

void HostCharacters::SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnServerId) const noexcept
{
    const ConnectionId_t cHost = m_server.HostConnection();
    uint32_t count = 0;
    for (const auto& cSpawn : m_game.OwnSpawns(aOwnServerId))
    {
        m_server.Send(aPlayer, cSpawn);
        ++count;
    }
    for (const auto& [cServerId, cCharacter] : m_characters)
    {
        if (cServerId == aOwnServerId || cCharacter.Owner == cHost || !cCharacter.PlayerRecord)
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
    m_server.SendToOthers(notify, it->second.Owner, TiltedPhoques::kReliable);

    m_characters.erase(it);
    m_server.FreeCharacterId(aServerId);
}
