// No game or client World here: tests build it against a headless server (Code/tests/bridge).
#include <Services/HostBridge.h>

#include <GameServer.h>

#include <Messages/AddTargetRequest.h>
#include <Messages/ClientReferencesMoveRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/SpellCastRequest.h>
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
    case ClientReferencesMoveRequest::Opcode: OnMovement(static_cast<const ClientReferencesMoveRequest&>(acMessage), aSender); return true;
    case RequestInventoryChanges::Opcode: OnInventoryChanges(static_cast<const RequestInventoryChanges&>(acMessage), aSender); return true;
    case RequestEquipmentChanges::Opcode: OnEquipmentChanges(static_cast<const RequestEquipmentChanges&>(acMessage), aSender); return true;
    case RequestEquipmentSnapshot::Opcode: OnEquipmentSnapshot(static_cast<const RequestEquipmentSnapshot&>(acMessage), aSender); return true;
    case DrawWeaponRequest::Opcode: OnDrawWeapon(static_cast<const DrawWeaponRequest&>(acMessage), aSender); return true;
    case RequestActorValueChanges::Opcode: OnActorValues(static_cast<const RequestActorValueChanges&>(acMessage), aSender); return true;
    case RequestActorMaxValueChanges::Opcode: OnActorMaxValues(static_cast<const RequestActorMaxValueChanges&>(acMessage), aSender); return true;
    case RequestDeathStateChange::Opcode: OnDeathState(static_cast<const RequestDeathStateChange&>(acMessage), aSender); return true;
    case RequestHealthChangeBroadcast::Opcode: OnHealthChange(static_cast<const RequestHealthChangeBroadcast&>(acMessage), aSender); return true;
    case SpellCastRequest::Opcode: OnSpellCast(static_cast<const SpellCastRequest&>(acMessage), aSender); return true;
    case InterruptCastRequest::Opcode: OnInterruptCast(static_cast<const InterruptCastRequest&>(acMessage), aSender); return true;
    case ProjectileLaunchRequest::Opcode: OnProjectileLaunch(static_cast<const ProjectileLaunchRequest&>(acMessage), aSender); return true;
    case AddTargetRequest::Opcode: OnAddTarget(static_cast<const AddTargetRequest&>(acMessage), aSender); return true;
    case RemoveSpellRequest::Opcode: OnRemoveSpell(static_cast<const RemoveSpellRequest&>(acMessage), aSender); return true;
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
    NotifyHealthChangeBroadcast notify;
    notify.Id = acRequest.Id;
    notify.DeltaHealth = acRequest.DeltaHealth;
    notify.HasAttacker = acRequest.HasAttacker;
    notify.AttackerId = acRequest.AttackerId;
    SendToOwner(acRequest.Id, notify, aSender);
}

void HostBridge::OnSpellCast(const SpellCastRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifySpellCast notify;
    notify.CasterId = acRequest.CasterId;
    notify.SpellFormId = acRequest.SpellFormId;
    notify.CastingSource = acRequest.CastingSource;
    notify.IsDualCasting = acRequest.IsDualCasting;
    notify.DesiredTarget = acRequest.DesiredTarget;
    RelayFromOwner(acRequest.CasterId, notify, aSender);
}

void HostBridge::OnInterruptCast(const InterruptCastRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyInterruptCast notify;
    notify.CasterId = acRequest.CasterId;
    notify.CastingSource = acRequest.CastingSource;
    RelayFromOwner(acRequest.CasterId, notify, aSender);
}

void HostBridge::OnProjectileLaunch(const ProjectileLaunchRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyProjectileLaunch notify;
    notify.ShooterID = acRequest.ShooterID;
    notify.OriginX = acRequest.OriginX;
    notify.OriginY = acRequest.OriginY;
    notify.OriginZ = acRequest.OriginZ;
    notify.ProjectileBaseID = acRequest.ProjectileBaseID;
    notify.WeaponID = acRequest.WeaponID;
    notify.AmmoID = acRequest.AmmoID;
    notify.ZAngle = acRequest.ZAngle;
    notify.XAngle = acRequest.XAngle;
    notify.YAngle = acRequest.YAngle;
    notify.ParentCellID = acRequest.ParentCellID;
    notify.SpellID = acRequest.SpellID;
    notify.CastingSource = acRequest.CastingSource;
    notify.Area = acRequest.Area;
    notify.Power = acRequest.Power;
    notify.Scale = acRequest.Scale;
    notify.AlwaysHit = acRequest.AlwaysHit;
    notify.NoDamageOutsideCombat = acRequest.NoDamageOutsideCombat;
    notify.AutoAim = acRequest.AutoAim;
    notify.DeferInitialization = acRequest.DeferInitialization;
    notify.ForceConeOfFire = acRequest.ForceConeOfFire;
    notify.UnkBool1 = acRequest.UnkBool1;
    notify.UnkBool2 = acRequest.UnkBool2;
    RelayFromOwner(acRequest.ShooterID, notify, aSender);
}

void HostBridge::OnAddTarget(const AddTargetRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyAddTarget notify;
    notify.TargetId = acRequest.TargetId;
    notify.CasterId = acRequest.CasterId;
    notify.SpellId = acRequest.SpellId;
    notify.EffectId = acRequest.EffectId;
    notify.Magnitude = acRequest.Magnitude;
    notify.IsDualCasting = acRequest.IsDualCasting;
    notify.ApplyHealPerkBonus = acRequest.ApplyHealPerkBonus;
    notify.ApplyStaminaPerkBonus = acRequest.ApplyStaminaPerkBonus;
    SendToOwner(acRequest.TargetId, notify, aSender);
}

void HostBridge::OnRemoveSpell(const RemoveSpellRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyRemoveSpell notify;
    notify.TargetId = acRequest.TargetId;
    notify.SpellId = acRequest.SpellId;
    m_server.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
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

void HostBridge::SendToOwner(uint32_t aServerId, const ServerMessage& acNotify, ConnectionId_t aSender) const noexcept
{
    const ConnectionId_t cOwner = m_server.OwnerConnection(aServerId);
    if (cOwner != 0 && cOwner != aSender)
        m_server.Send(cOwner, acNotify);
}
