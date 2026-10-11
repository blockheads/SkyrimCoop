// No game or client World here: tests build it against a headless server (Code/tests/bridge).
#include <Services/Host/HostBridge.h>

#include <GameServer.h>

#include <type_traits>

#include <Messages/AddTargetRequest.h>
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

HostBridge::HostBridge(Server::GameServer& aServer, HostGame aGame) noexcept
    : m_server(aServer)
    , m_game(std::move(aGame))
    , m_characters(*this, aServer, m_game)
    , m_clock(*this, aServer, m_game)
{
    using enum Delivery;

    // Characters (spawning, removal, movement and respawn: HostCharacters)
    On(*this, &HostBridge::OnFactionsChanges);
    Relay<MountRequest, NotifyMount>(FromAnyone);
    Relay<NewPackageRequest, NotifyNewPackage>(FromAnyone);
    Relay<DialogueRequest, NotifyDialogue>(FromAnyone);
    Relay<SubtitleRequest, NotifySubtitle>(FromAnyone);
    Relay<SyncExperienceRequest, NotifySyncExperience>(FromAnyone); // everyone is in the host's party

    // Inventory and hands
    On(*this, &HostBridge::OnInventoryChanges);
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
    m_server.SetLeaveHandler([this](ConnectionId_t aPlayer) { m_characters.OnPlayerLeft(aPlayer); });
}

void HostBridge::Update(uint64_t aTick) noexcept
{
    m_clock.Update(aTick);
}

void HostBridge::PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterServerId) const noexcept
{
    m_clock.SendTime(aPlayer);
    m_characters.SendExistingCharacters(aPlayer, aCharacterServerId);
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

void HostBridge::OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    if (acRequest.UpdateClients)
        Deliver(ToNotify(acRequest), Delivery::FromAnyone, 0, aSender);
}

void HostBridge::OnFactionsChanges(const RequestFactionsChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyFactionsChanges notify;
    for (const auto& [cServerId, cFactions] : acRequest.Changes)
        if (m_characters.OwnerOf(cServerId) == aSender)
            notify.Changes[cServerId] = cFactions;
    if (!notify.Changes.empty())
        m_server.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
}

void HostBridge::Deliver(const ServerMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept
{
    switch (aDelivery)
    {
    case Delivery::FromOwner:
        if (m_characters.OwnerOf(aActor) == aSender)
            m_server.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable);
        else
            spdlog::debug("HostBridge: {:x} sent opcode {} for {:x}, which it does not own", aSender, acNotify.GetOpcode(), aActor);
        break;
    case Delivery::FromAnyone: m_server.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable); break;
    case Delivery::ToOwner:
        if (const ConnectionId_t cOwner = m_characters.OwnerOf(aActor); cOwner != 0 && cOwner != aSender)
            m_server.Send(cOwner, acNotify);
        break;
    }
}

