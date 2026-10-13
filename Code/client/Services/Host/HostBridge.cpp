// No game or client World here: tests build it against a headless server (Code/tests/bridge).
#include <Services/Host/HostBridge.h>

#include <GameServer.h>

#include <regex>
#include <type_traits>

#include <Messages/ActivateRequest.h>
#include <Messages/AddTargetRequest.h>
#include <Messages/DialogueRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/MountRequest.h>
#include <Messages/NewPackageRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyActiveEffects.h>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/NotifyMount.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/RequestActiveEffects.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/ScriptAnimationRequest.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/SpellCastRequest.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/SyncExperienceRequest.h>

namespace
{
// The relays that change what they pass on; the rest share a payload (Messages/Payloads.h).

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

// Objects: without the cell (under the party tether every game has the host's cells loaded).
NotifyActivate ToNotify(const ActivateRequest& acRequest)
{
    NotifyActivate notify;
    notify.Id = acRequest.Id;
    notify.ActivatorId = acRequest.ActivatorId;
    notify.PreActivationOpenState = acRequest.PreActivationOpenState;
    return notify;
}

NotifyLockChange ToNotify(const LockChangeRequest& acRequest)
{
    NotifyLockChange notify;
    notify.Id = acRequest.Id;
    notify.IsLocked = acRequest.IsLocked;
    notify.LockLevel = acRequest.LockLevel;
    return notify;
}

NotifyScriptAnimation ToNotify(const ScriptAnimationRequest& acRequest)
{
    NotifyScriptAnimation notify;
    notify.FormID = acRequest.FormID;
    notify.Animation = acRequest.Animation;
    notify.EventName = acRequest.EventName;
    return notify;
}

NotifySetWaypoint ToNotify(const RequestSetWaypoint& acRequest)
{
    NotifySetWaypoint notify;
    notify.Position = acRequest.Position;
    notify.WorldSpaceFormID = acRequest.WorldSpaceFormID;
    return notify;
}

NotifyRemoveWaypoint ToNotify(const RequestRemoveWaypoint&)
{
    return {};
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
    , m_session(*this, aServer, m_game)
    , m_characters(*this, aServer, m_game)
    , m_clock(*this, aServer, m_game)
    , m_weather(aServer, m_game)
{
    using enum Delivery;

    // Players
    On(*this, &HostBridge::OnPlayerLevel);

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
    // What shows on an actor (burning, frost): its owner's active effects, played as visuals only.
    Relay<RequestActiveEffects, NotifyActiveEffects>(FromOwner, &RequestActiveEffects::ServerId);

    // Objects (doors, containers, levers): whoever's game saw it, everyone else replays it.
    Relay<ActivateRequest, NotifyActivate>(FromAnyone);
    Relay<LockChangeRequest, NotifyLockChange>(FromAnyone);
    Relay<ScriptAnimationRequest, NotifyScriptAnimation>(FromAnyone);

    // The map and chat
    Relay<RequestSetWaypoint, NotifySetWaypoint>(FromAnyone);
    Relay<RequestRemoveWaypoint, NotifyRemoveWaypoint>(FromAnyone);
    On(*this, &HostBridge::OnChatMessage);

    m_server.SetFrontDoor([this](const ClientMessage& acMessage, ConnectionId_t aSender) { return Handle(acMessage, aSender); });
    m_server.SetLeaveHandler([this](ConnectionId_t aPlayer) { m_characters.OnPlayerLeft(aPlayer); });
}

void HostBridge::Update(uint64_t aTick) noexcept
{
    m_clock.Update(aTick);
    m_weather.Update();
}

void HostBridge::Kick(uint32_t aPlayerId) noexcept
{
    m_server.KickPlayer(aPlayerId);
}

void HostBridge::PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterServerId) const noexcept
{
    m_clock.SendTime(aPlayer);
    m_weather.SendWeather(aPlayer);
    m_characters.SendExistingCharacters(aPlayer, aCharacterServerId);
}

bool HostBridge::Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept
{
    const auto cOpcode = acMessage.GetOpcode();
    // A game that is not a player may only ask to join; the server drops anything else from it.
    if (cOpcode != kAuthenticationRequest && !m_server.IsAuthenticated(aSender))
        return false;

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

void HostBridge::OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    if (acRequest.MessageType == kSystemMessage || acRequest.MessageType > kLocalChat)
    {
        spdlog::warn("HostBridge: {:x} sent a chat message of type {}, which players cannot", aSender, uint32_t(acRequest.MessageType));
        return;
    }
    // The UI shows the text as it is: no markup from players.
    static const std::regex s_markup{"<[^>]+>\\s+(?=<)|<[^>]+>"};
    NotifyChatMessageBroadcast notify;
    notify.MessageType = acRequest.MessageType;
    notify.PlayerName = std::regex_replace(m_server.PlayerNameOf(aSender), s_markup, "");
    notify.ChatMessage = std::regex_replace(acRequest.ChatMessage, s_markup, "");
    for (const ConnectionId_t cPlayer : m_server.PlayerConnections())
        m_server.Send(cPlayer, notify);
}

void HostBridge::OnPlayerLevel(const PlayerLevelRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (acRequest.NewLevel == 0)
    {
        spdlog::warn("HostBridge: {:x} sent level 0", aSender);
        return;
    }
    m_server.SetPlayerLevel(aSender, acRequest.NewLevel);
    NotifyPlayerLevel notify;
    notify.PlayerId = m_server.PlayerIdOf(aSender);
    notify.NewLevel = acRequest.NewLevel;
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
            spdlog::debug("HostBridge: {:x} sent opcode {} for {:x}, which it does not own", aSender, uint32_t(acNotify.GetOpcode()), aActor);
        break;
    case Delivery::FromAnyone: m_server.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable); break;
    case Delivery::ToOwner:
        if (const ConnectionId_t cOwner = m_characters.OwnerOf(aActor); cOwner != 0 && cOwner != aSender)
            m_server.Send(cOwner, acNotify);
        break;
    }
}

