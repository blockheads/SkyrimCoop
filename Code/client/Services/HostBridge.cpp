// No game or client World here: tests build it against a headless server (Code/tests/bridge).
#include <Services/HostBridge.h>

#include <GameServer.h>

#include <type_traits>

#include <Messages/AddTargetRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/CancelAssignmentRequest.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestRespawn.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/DialogueRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/MountRequest.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyMount.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/SpellCastRequest.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/SyncExperienceRequest.h>

namespace
{
// The two relays that change what they pass on; the rest share a payload (Messages/Payloads.h).

NotifyInventoryChanges ToNotify(const RequestInventoryChanges& acRequest)
{
    NotifyInventoryChanges notify;
    notify.ServerId = acRequest.ServerId;
    notify.Item = acRequest.Item;
    notify.Drop = false; // Dropped items are not synced (the server's experimental bEnableItemDrops, off).
    return notify;
}

NotifyEquipmentChanges ToNotify(const RequestEquipmentChanges& acRequest)
{
    // Without the sender's whole equipment: copies get that from the owner's snapshots.
    NotifyEquipmentChanges notify;
    notify.ServerId = acRequest.ServerId;
    notify.ItemId = acRequest.ItemId;
    notify.EquipSlotId = acRequest.EquipSlotId;
    notify.Count = acRequest.Count;
    notify.Unequip = acRequest.Unequip;
    notify.IsSpell = acRequest.IsSpell;
    notify.IsShout = acRequest.IsShout;
    return notify;
}

template <class TRequest, class TNotify> TNotify ToNotify(const TRequest& acRequest)
{
    if constexpr (requires { typename TRequest::Payload; })
    {
        static_assert(std::is_same_v<typename TRequest::Payload, typename TNotify::Payload>);
        TNotify notify;
        static_cast<typename TNotify::Payload&>(notify) = acRequest;
        return notify;
    }
    else
        return ToNotify(acRequest);
}
} // namespace

template <class TRequest, class TNotify> void HostBridge::Relay(Delivery aDelivery, uint32_t TRequest::*apActor) noexcept
{
    m_rules[TRequest::Opcode] = [this, aDelivery, apActor](const ClientMessage& acMessage, ConnectionId_t aSender) {
        const auto& cRequest = static_cast<const TRequest&>(acMessage);
        Deliver(ToNotify<TRequest, TNotify>(cRequest), aDelivery, apActor ? cRequest.*apActor : 0, aSender);
    };
}

template <class TRequest> void HostBridge::On(void (HostBridge::*apRule)(const TRequest&, ConnectionId_t) const noexcept) noexcept
{
    m_rules[TRequest::Opcode] = [this, apRule](const ClientMessage& acMessage, ConnectionId_t aSender) {
        (this->*apRule)(static_cast<const TRequest&>(acMessage), aSender);
    };
}

template <class TRequest> void HostBridge::On(void (HostBridge::*apRule)(const TRequest&, ConnectionId_t) noexcept) noexcept
{
    m_rules[TRequest::Opcode] = [this, apRule](const ClientMessage& acMessage, ConnectionId_t aSender) {
        (this->*apRule)(static_cast<const TRequest&>(acMessage), aSender);
    };
}

HostBridge::HostBridge(Server::GameServer& aServer, HostGame aGame) noexcept
    : m_server(aServer)
    , m_game(std::move(aGame))
{
    using enum Delivery;

    // Characters
    On<AssignCharacterRequest>(&HostBridge::OnAssignCharacter);
    On<CancelAssignmentRequest>(&HostBridge::OnCancelAssignment);
    On<RequestRemoveCharacter>(&HostBridge::OnRemoveCharacter);
    On<RequestRespawn>(&HostBridge::OnRespawn);
    On<PlayerRespawnRequest>(&HostBridge::OnPlayerRespawn);
    On<ClientReferencesMoveRequest>(&HostBridge::OnMovement);
    On<RequestFactionsChanges>(&HostBridge::OnFactionsChanges);
    Relay<MountRequest, NotifyMount>(FromAnyone);
    Relay<NewPackageRequest, NotifyNewPackage>(FromAnyone);
    Relay<DialogueRequest, NotifyDialogue>(FromAnyone);
    Relay<SubtitleRequest, NotifySubtitle>(FromAnyone);
    Relay<SyncExperienceRequest, NotifySyncExperience>(FromAnyone); // everyone is in the host's party

    // Inventory and hands
    On<RequestInventoryChanges>(&HostBridge::OnInventoryChanges);
    Relay<RequestEquipmentChanges, NotifyEquipmentChanges>(FromAnyone); // like items: a peer can dress the host's follower
    Relay<RequestEquipmentSnapshot, NotifyEquipmentSnapshot>(FromOwner, &RequestEquipmentSnapshot::ServerId);
    Relay<DrawWeaponRequest, NotifyDrawWeapon>(FromOwner, &DrawWeaponRequest::Id);

    // Actor values: a hit lands in its owner's game, which then reports the resulting values.
    Relay<RequestActorValueChanges, NotifyActorValueChanges>(FromOwner, &RequestActorValueChanges::Id);
    Relay<RequestActorMaxValueChanges, NotifyActorMaxValueChanges>(FromOwner, &RequestActorMaxValueChanges::Id);
    Relay<RequestDeathStateChange, NotifyDeathStateChange>(FromOwner, &RequestDeathStateChange::Id);
    Relay<RequestHealthChangeBroadcast, NotifyHealthChangeBroadcast>(ToOwner, &RequestHealthChangeBroadcast::Id);

    // Magic and combat: an effect landing works like a hit.
    Relay<SpellCastRequest, NotifySpellCast>(FromOwner, &SpellCastRequest::CasterId);
    Relay<InterruptCastRequest, NotifyInterruptCast>(FromOwner, &InterruptCastRequest::CasterId);
    Relay<ProjectileLaunchRequest, NotifyProjectileLaunch>(FromOwner, &ProjectileLaunchRequest::ShooterID);
    Relay<AddTargetRequest, NotifyAddTarget>(ToOwner, &AddTargetRequest::TargetId);
    Relay<RemoveSpellRequest, NotifyRemoveSpell>(FromAnyone);

    m_server.SetFrontDoor([this](const ClientMessage& acMessage, ConnectionId_t aSender) { return Handle(acMessage, aSender); });
    m_server.SetLeaveHandler([this](ConnectionId_t aPlayer) { OnPlayerLeft(aPlayer); });
}

bool HostBridge::Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept
{
    if (!m_server.IsAuthenticated(aSender))
        return false; // the server's own handling until the player is in

    const auto cOpcode = acMessage.GetOpcode();
    if (cOpcode >= m_rules.size() || !m_rules[cOpcode])
        return false;
    m_rules[cOpcode](acMessage, aSender);
    return true;
}

void HostBridge::OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) noexcept
{
    ServerReferencesMoveRequest notify;
    notify.Tick = m_server.GetTick();
    for (const auto& [cServerId, cUpdate] : acRequest.Updates)
    {
        auto it = m_characters.find(cServerId);
        if (it == m_characters.end() || it->second.Owner != aSender)
        {
            spdlog::debug("HostBridge: {:x} moved {:x}, which it does not own", aSender, cServerId);
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

void HostBridge::OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    if (acRequest.UpdateClients)
        Deliver(ToNotify(acRequest), Delivery::FromAnyone, 0, aSender);
}

void HostBridge::OnFactionsChanges(const RequestFactionsChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyFactionsChanges notify;
    for (const auto& [cServerId, cFactions] : acRequest.Changes)
        if (OwnerOf(cServerId) == aSender)
            notify.Changes[cServerId] = cFactions;
    if (!notify.Changes.empty())
        m_server.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
}

void HostBridge::Deliver(const ServerMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept
{
    switch (aDelivery)
    {
    case Delivery::FromOwner:
        if (OwnerOf(aActor) == aSender)
            m_server.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable);
        else
            spdlog::debug("HostBridge: {:x} sent opcode {} for {:x}, which it does not own", aSender, acNotify.GetOpcode(), aActor);
        break;
    case Delivery::FromAnyone: m_server.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable); break;
    case Delivery::ToOwner:
        if (const ConnectionId_t cOwner = OwnerOf(aActor); cOwner != 0 && cOwner != aSender)
            m_server.Send(cOwner, acNotify);
        break;
    }
}

void HostBridge::OnAssignCharacter(const AssignCharacterRequest& acRequest, ConnectionId_t aSender) noexcept
{
    const bool cIsPlayer = acRequest.ReferenceId == GameId{0, 0x14};
    if (!cIsPlayer && aSender != m_server.HostConnection())
    {
        // Under the party tether every actor but a player's own is in the host's game.
        spdlog::warn("HostBridge: {:x} announced {:x}:{:x}, which only the host may", aSender, acRequest.ReferenceId.ModId, acRequest.ReferenceId.BaseId);
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
            SendExistingCharacters(aSender, cServerId);
    }

    AssignCharacterResponse response;
    response.Cookie = acRequest.Cookie;
    response.ServerId = cServerId;
    response.PlayerId = cPlayerId;
    response.Owner = true;
    m_server.Send(aSender, response);

    m_server.SendToOthers(cSpawn, aSender, TiltedPhoques::kReliable);
    spdlog::info("HostBridge: {:x} announced {:x}:{:x} as {:x}", aSender, acRequest.ReferenceId.ModId, acRequest.ReferenceId.BaseId, cServerId);
}

void HostBridge::OnCancelAssignment(const CancelAssignmentRequest& acRequest, ConnectionId_t aSender) noexcept
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

void HostBridge::OnRemoveCharacter(const RequestRemoveCharacter& acRequest, ConnectionId_t aSender) noexcept
{
    if (OwnerOf(acRequest.ServerId) == aSender)
        RemoveCharacter(acRequest.ServerId);
}

void HostBridge::OnRespawn(const RequestRespawn& acRequest, ConnectionId_t aSender) noexcept
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

void HostBridge::OnPlayerRespawn(const PlayerRespawnRequest&, ConnectionId_t aSender) const noexcept
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

void HostBridge::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    Vector<uint32_t> owned;
    for (const auto& [cServerId, cCharacter] : m_characters)
        if (cCharacter.Owner == aPlayer)
            owned.push_back(cServerId);
    for (const uint32_t cServerId : owned)
        RemoveCharacter(cServerId);
}

ConnectionId_t HostBridge::OwnerOf(uint32_t aServerId) const noexcept
{
    const auto it = m_characters.find(aServerId);
    return it != m_characters.end() ? it->second.Owner : 0;
}

std::optional<CharacterSpawnRequest> HostBridge::BuildSpawn(uint32_t aServerId) const noexcept
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

void HostBridge::SendExistingCharacters(ConnectionId_t aPlayer, uint32_t aOwnServerId) const noexcept
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
    spdlog::info("HostBridge: {} characters for the player who joined ({:x})", count, aPlayer);
}

void HostBridge::RemoveCharacter(uint32_t aServerId) noexcept
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
