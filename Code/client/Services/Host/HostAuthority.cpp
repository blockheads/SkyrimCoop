// No game or client World here: tests build it with in-process games (Code/tests/bridge).
#include <Services/Host/HostAuthority.h>

#include <Services/Host/HostNetwork.h>

#include <algorithm>
#include <string_view>
#include <type_traits>

#include <Messages/ActivateRequest.h>
#include <Messages/AddTargetRequest.h>
#include <Messages/DialogueRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/ContainerChangeRequest.h>
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
#include <Messages/NotifyMount.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifyRemoveWaypoint.h>
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
    notify.Drop = false; // Dropped items are not synced this way (upstream's experimental item drops).
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

template <class TRequest, class TNotify> void HostAuthority::Relay(Delivery aDelivery, uint32_t TRequest::*apActor) noexcept
{
    m_rules[TRequest::Opcode] = [this, aDelivery, apActor](const ClientMessage& acMessage, ConnectionId_t aSender) {
        const auto& cRequest = static_cast<const TRequest&>(acMessage);
        Deliver(ToNotify<TRequest, TNotify>(cRequest), aDelivery, apActor ? cRequest.*apActor : 0, aSender);
    };
}

HostAuthority::HostAuthority(HostNetwork& aNetwork, HostGame aGame) noexcept
    : m_network(aNetwork)
    , m_game(std::move(aGame))
    , m_players(aNetwork)
    , m_session(*this, aNetwork, m_players, m_game)
    , m_characters(*this, aNetwork, m_players, m_game)
    , m_clock(*this, aNetwork, m_players, m_game)
    , m_weather(aNetwork, m_players, m_game)
    , m_worldState(*this, aNetwork, m_players, m_game)
    , m_timeScale(*this, aNetwork, m_players)
    , m_dialogue(*this, aNetwork, m_players, m_characters)
{
    using enum Delivery;

    // Players
    On(*this, &HostAuthority::OnPlayerLevel);

    // Characters (spawning, removal, movement and respawn: HostCharacters)
    On(*this, &HostAuthority::OnFactionsChanges);
    Relay<MountRequest, NotifyMount>(FromAnyone);
    Relay<NewPackageRequest, NotifyNewPackage>(FromAnyone);
    Relay<DialogueRequest, NotifyDialogue>(FromAnyone);
    Relay<SubtitleRequest, NotifySubtitle>(FromAnyone);
    Relay<SyncExperienceRequest, NotifySyncExperience>(FromAnyone); // everyone is in the host's party

    // Inventory and hands
    On(*this, &HostAuthority::OnInventoryChanges);
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
    On(*this, &HostAuthority::OnLockChange);
    On(*this, &HostAuthority::OnContainerChange);

    // The map and chat
    Relay<RequestSetWaypoint, NotifySetWaypoint>(FromAnyone);
    Relay<RequestRemoveWaypoint, NotifyRemoveWaypoint>(FromAnyone);
    On(*this, &HostAuthority::OnChatMessage);

    m_network.SetFrontDoor([this](const ClientMessage& acMessage, ConnectionId_t aSender) { return Handle(acMessage, aSender); });
    // A player's game ended its connection: the others drop it, then its characters go.
    m_network.SetDisconnectionHandler([this](ConnectionId_t aConnection) {
        if (!m_players.IsPlayer(aConnection))
            return;
        m_session.OnPlayerLeft(aConnection);
        m_characters.OnPlayerLeft(aConnection);
        m_players.Remove(aConnection);
        m_timeScale.OnPlayerLeft(aConnection);
        m_dialogue.OnPlayerLeft(aConnection);
        m_worldState.OnPlayerLeft(aConnection);
    });
}

void HostAuthority::Update(uint64_t aTick) noexcept
{
    m_session.Update();
    m_clock.Update(aTick);
    m_weather.Update();
    m_worldState.Update(aTick);
}

void HostAuthority::Kick(uint32_t aPlayerId) noexcept
{
    m_players.Kick(aPlayerId);
}

void HostAuthority::PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterServerId) noexcept
{
    m_clock.SendTime(aPlayer);
    m_weather.SendWeather(aPlayer);
    m_worldState.SendWorldState(aPlayer);
    m_timeScale.SendTimeScale(aPlayer);
    m_characters.SendExistingCharacters(aPlayer, aCharacterServerId);
}

bool HostAuthority::Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept
{
    const auto cOpcode = acMessage.GetOpcode();
    // A game that is not a player may only ask to join; anything else from it is dropped.
    if (cOpcode != kAuthenticationRequest && !m_players.IsPlayer(aSender))
        return false;

    if (cOpcode >= m_rules.size() || !m_rules[cOpcode])
        return false;
    m_rules[cOpcode](acMessage, aSender);
    return true;
}

void HostAuthority::OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    if (acRequest.UpdateClients)
        Deliver(ToNotify(acRequest), Delivery::FromAnyone, 0, aSender);
}

void HostAuthority::OnLockChange(const LockChangeRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // The host's own locks go out as its object changes (HostWorldState).
    if (aSender != m_players.Host() && m_game.SetLock)
        m_game.SetLock(acRequest.Id, acRequest.IsLocked, acRequest.LockLevel);
}

void HostAuthority::OnContainerChange(const ContainerChangeRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // The host's own containers go out as its object changes (HostWorldState).
    if (aSender != m_players.Host() && m_game.SetItems)
        m_game.SetItems(acRequest.Container, acRequest.Item, acRequest.Count);
}

void HostAuthority::OnFactionsChanges(const RequestFactionsChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyFactionsChanges notify;
    for (const auto& [cServerId, cFactions] : acRequest.Changes)
        if (m_characters.OwnerOf(cServerId) == aSender)
            notify.Changes[cServerId] = cFactions;
    if (!notify.Changes.empty())
        m_players.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
}

void HostAuthority::OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    if (acRequest.MessageType == kSystemMessage || acRequest.MessageType > kLocalChat)
    {
        spdlog::warn("HostAuthority: {:x} sent a chat message of type {}, which players cannot", aSender, uint32_t(acRequest.MessageType));
        return;
    }
    // The chat box shows text as it is (RmlUi text, no markup parsed).
    NotifyChatMessageBroadcast notify;
    notify.MessageType = acRequest.MessageType;
    notify.PlayerName = ChatText(m_players.PlayerNameOf(aSender));
    notify.ChatMessage = ChatText(acRequest.ChatMessage);
    for (const ConnectionId_t cPlayer : m_players.Connections())
        m_network.Send(cPlayer, notify);
}

void HostAuthority::OnPlayerLevel(const PlayerLevelRequest& acRequest, ConnectionId_t aSender) noexcept
{
    if (acRequest.NewLevel == 0)
    {
        spdlog::warn("HostAuthority: {:x} sent level 0", aSender);
        return;
    }
    m_players.SetLevel(aSender, acRequest.NewLevel);
    NotifyPlayerLevel notify;
    notify.PlayerId = m_players.PlayerIdOf(aSender);
    notify.NewLevel = acRequest.NewLevel;
    m_players.SendToOthers(notify, aSender, TiltedPhoques::kReliable);
}

void HostAuthority::Deliver(const ServerMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept
{
    switch (aDelivery)
    {
    case Delivery::FromOwner:
        if (m_characters.OwnerOf(aActor) == aSender)
            m_players.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable);
        else
            spdlog::debug("HostAuthority: {:x} sent opcode {} for {:x}, which it does not own", aSender, uint32_t(acNotify.GetOpcode()), aActor);
        break;
    case Delivery::FromAnyone: m_players.SendToOthers(acNotify, aSender, TiltedPhoques::kReliable); break;
    case Delivery::ToOwner:
        if (const ConnectionId_t cOwner = m_characters.OwnerOf(aActor); cOwner != 0 && cOwner != aSender)
            m_network.Send(cOwner, acNotify);
        break;
    }
}

TiltedPhoques::String ChatText(std::string_view aText) noexcept
{
    // A character the limit cuts through is left out whole (UTF-8 continuation bytes are 10xxxxxx).
    size_t length = std::min(aText.size(), kChatMaxLength);
    while (length && length < aText.size() && (static_cast<unsigned char>(aText[length]) & 0xC0) == 0x80)
        --length;
    TiltedPhoques::String text;
    for (const char c : aText.substr(0, length))
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
            text += c;
    return text;
}
