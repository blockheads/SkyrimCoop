// No game or client World here: tests build it against a headless server (Code/tests/bridge).
#include <Services/HostBridge.h>

#include <GameServer.h>

#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/ServerReferencesMoveRequest.h>

HostBridge::HostBridge(Server::GameServer& aServer, SpawnSource aSpawns) noexcept
    : m_server(aServer)
    , m_spawns(std::move(aSpawns))
{
    m_server.SetFrontDoor([this](const ClientMessage& acMessage, ConnectionId_t aSender) { return Handle(acMessage, aSender); });
    // Late join from the host's live state, not the server's records.
    m_server.SetLateJoinHandler([this](ConnectionId_t aPlayer, uint32_t aCharacterServerId) {
        const auto cSpawns = m_spawns(aCharacterServerId);
        spdlog::info("HostBridge: {} characters for the player who joined ({:x})", cSpawns.size(), aPlayer);
        for (const auto& cSpawn : cSpawns)
            m_server.Send(aPlayer, cSpawn);
    });
}

bool HostBridge::Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept
{
    if (!m_server.IsAuthenticated(aSender))
        return false; // the server's own handling until the player is in

    switch (acMessage.GetOpcode())
    {
    case kClientReferencesMoveRequest: OnMovement(static_cast<const ClientReferencesMoveRequest&>(acMessage), aSender); return true;
    case kRequestInventoryChanges: OnInventoryChanges(static_cast<const RequestInventoryChanges&>(acMessage), aSender); return true;
    case kRequestEquipmentChanges: OnEquipmentChanges(static_cast<const RequestEquipmentChanges&>(acMessage), aSender); return true;
    case kRequestEquipmentSnapshot: OnEquipmentSnapshot(static_cast<const RequestEquipmentSnapshot&>(acMessage), aSender); return true;
    case kDrawWeaponRequest: OnDrawWeapon(static_cast<const DrawWeaponRequest&>(acMessage), aSender); return true;
    case kRequestActorValueChanges: OnActorValues(static_cast<const RequestActorValueChanges&>(acMessage), aSender); return true;
    case kRequestActorMaxValueChanges: OnActorMaxValues(static_cast<const RequestActorMaxValueChanges&>(acMessage), aSender); return true;
    case kRequestDeathStateChange: OnDeathState(static_cast<const RequestDeathStateChange&>(acMessage), aSender); return true;
    case kRequestHealthChangeBroadcast: OnHealthChange(static_cast<const RequestHealthChangeBroadcast&>(acMessage), aSender); return true;
    default: return false;
    }
}

void HostBridge::OnMovement(const ClientReferencesMoveRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    ServerReferencesMoveRequest notify;
    notify.Tick = m_server.GetTick();
    for (const auto& [cServerId, cUpdate] : acRequest.Updates)
    {
        if (m_server.OwnerConnection(cServerId) == aSender)
            notify.Updates[cServerId] = cUpdate;
        else
            spdlog::debug("HostBridge: {:x} moved {:x}, which it does not own", aSender, cServerId);
    }
    if (!notify.Updates.empty())
        m_server.SendToOthers(notify, aSender, TiltedPhoques::kUnreliable);
}

void HostBridge::OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    if (!acRequest.UpdateClients)
        return;

    NotifyInventoryChanges notify;
    notify.ServerId = acRequest.ServerId;
    notify.Item = acRequest.Item;
    notify.Drop = false; // Dropped items are not synced (the server's experimental bEnableItemDrops, off).
    m_server.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
}

void HostBridge::OnEquipmentChanges(const RequestEquipmentChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyEquipmentChanges notify;
    notify.ServerId = acRequest.ServerId;
    notify.ItemId = acRequest.ItemId;
    notify.EquipSlotId = acRequest.EquipSlotId;
    notify.Count = acRequest.Count;
    notify.Unequip = acRequest.Unequip;
    notify.IsSpell = acRequest.IsSpell;
    notify.IsShout = acRequest.IsShout;
    m_server.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
}

void HostBridge::OnEquipmentSnapshot(const RequestEquipmentSnapshot& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyEquipmentSnapshot notify;
    notify.ServerId = acRequest.ServerId;
    notify.Equipment = acRequest.Equipment;
    RelayFromOwner(acRequest.ServerId, notify, aSender);
}

void HostBridge::OnDrawWeapon(const DrawWeaponRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyDrawWeapon notify;
    notify.Id = acRequest.Id;
    notify.IsWeaponDrawn = acRequest.IsWeaponDrawn;
    RelayFromOwner(acRequest.Id, notify, aSender);
}

void HostBridge::OnActorValues(const RequestActorValueChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyActorValueChanges notify;
    notify.Id = acRequest.Id;
    notify.Values = acRequest.Values;
    RelayFromOwner(acRequest.Id, notify, aSender);
}

void HostBridge::OnActorMaxValues(const RequestActorMaxValueChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyActorMaxValueChanges notify;
    notify.Id = acRequest.Id;
    notify.Values = acRequest.Values;
    RelayFromOwner(acRequest.Id, notify, aSender);
}

void HostBridge::OnDeathState(const RequestDeathStateChange& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyDeathStateChange notify;
    notify.Id = acRequest.Id;
    notify.IsDead = acRequest.IsDead;
    RelayFromOwner(acRequest.Id, notify, aSender);
}

void HostBridge::OnHealthChange(const RequestHealthChangeBroadcast& acRequest, ConnectionId_t aSender) const noexcept
{
    const ConnectionId_t cOwner = m_server.OwnerConnection(acRequest.Id);
    if (cOwner == 0 || cOwner == aSender)
        return;

    NotifyHealthChangeBroadcast notify;
    notify.Id = acRequest.Id;
    notify.DeltaHealth = acRequest.DeltaHealth;
    notify.HasAttacker = acRequest.HasAttacker;
    notify.AttackerId = acRequest.AttackerId;
    m_server.Send(cOwner, notify);
}

void HostBridge::RelayFromOwner(uint32_t aServerId, const ServerMessage& acNotify, ConnectionId_t aSender) const noexcept
{
    if (m_server.OwnerConnection(aServerId) != aSender)
    {
        spdlog::debug("HostBridge: {:x} sent opcode {} for {:x}, which it does not own", aSender, acNotify.GetOpcode(), aServerId);
        return;
    }
    m_server.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable);
}
