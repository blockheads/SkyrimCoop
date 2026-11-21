// No game or client World here: tests build it with in-process games (Code/tests/host_authority).
#include <Services/Host/HostAuthority.h>

#include <Services/Host/HostNetwork.h>
#include <RecipeBook.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <type_traits>

#include <Messages/ActivateRequest.h>
#include <Messages/Relayed.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/SendChatMessageRequest.h>
#include <Diagnostics/ReportPaths.h>
#include <Diagnostics/Profile.h>

namespace
{
// The relays that change what they pass on; the rest share a payload (Messages/Payloads.h).

// Objects: without the cell (under the party tether every game has the host's cells loaded).
NotifyActivate ToNotify(const ActivateRequest& acRequest)
{
    NotifyActivate notify;
    notify.Id = acRequest.Id;
    notify.NpcId = acRequest.NpcId;
    notify.ActivatorId = acRequest.ActivatorId;
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
    m_rules[TRequest::Opcode] = [this, aDelivery, apActor](const RequestMessage& acMessage, ConnectionId_t aSender) {
        const auto& cRequest = static_cast<const TRequest&>(acMessage);
        Deliver(ToNotify<TRequest, TNotify>(cRequest), aDelivery, apActor ? cRequest.*apActor : 0, aSender);
    };
}

HostAuthority::HostAuthority(HostNetwork& aNetwork, HostGame aGame) noexcept
    : m_network(aNetwork)
    , m_game(std::move(aGame))
    , m_players(aNetwork)
    , m_transfer(aNetwork, m_game)
    , m_session(*this, aNetwork, m_players, m_characters, m_transfer, m_game)
    , m_characters(*this, aNetwork, m_players, m_game)
    , m_clock(*this, aNetwork, m_players, m_game)
    , m_wait(*this, aNetwork, m_players, m_game)
    , m_crime(*this, aNetwork, m_players, m_characters, m_game)
    , m_purse(m_players, m_characters, m_game)
    , m_partyItems(m_players, m_characters, m_game)
    , m_weather(aNetwork, m_players, m_game)
    , m_worldState(*this, aNetwork, m_players, m_game)
    , m_items(*this, aNetwork, m_players, m_characters, m_purse, m_partyItems, m_game)
    , m_hands(*this, m_players, m_characters, m_game)
    , m_physics(*this, aNetwork, m_players, m_hands, m_game)
    , m_timeScale(*this, aNetwork, m_players)
    , m_dialogue(*this, aNetwork, m_players, m_characters)
    , m_playerNatives(*this, aNetwork, m_players, m_characters)
    , m_records(*this, m_players, m_characters, m_game)
    , m_story(*this, m_players, m_characters, m_game)
    , m_death(*this, aNetwork, m_players, m_characters)
    , m_followers(*this, aNetwork, m_players, m_characters, m_death, m_game)
    , m_devTools(*this, m_players, m_characters, m_game)
{
    using enum Delivery;
    m_records.OnKept = [this](uint32_t aId, const RecordPart& acPart) {
        ActorAiState state;
        if (acPart.Kind == RecordKind::kAiState && FromPartBytes(acPart.Bytes, state))
            m_death.OnAiState(aId, state);
        // A player's character's name is that player's in the party's lists.
        if (acPart.Kind == RecordKind::kName && m_characters.IsPlayerCharacter(aId))
            NamePlayer(m_characters.OwnerOf(aId), acPart.Bytes);
    };

    // Players
    On(*this, &HostAuthority::OnPlayerLevel);
    On(m_transfer, &HostSaveTransfer::OnSaveHeld);

    // A kill move's claim (kill-moves.md, "Claimed pairs"): its NPC victim alive, its graph running (kProcessMe).
    m_characters.Claims().SetRule(Payloads::Claim::kKillMove, {.Allows = [this](const Payloads::Claim& acClaim) {
        return std::ranges::all_of(acClaim.Members, [&](const Payloads::ClaimMember& acMember) {
            return !acMember.Take || !m_game.CanPair || m_game.CanPair(acMember.ActorId);
        });
    }});

    // Characters (spawning, removal, movement and respawn: HostCharacters)
    Relay<DialogueRequest, NotifyDialogue>(FromAnyone);
    Relay<SubtitleRequest, NotifySubtitle>(FromAnyone);

    // Inventory and hands
    On(*this, &HostAuthority::OnInventoryChanges);
    // The graph input stream: ops from each actor's owner; a copy's game asks the owner for a sync point.
    On(*this, &HostAuthority::OnGraphOps);
    Relay<GraphSyncRequest, NotifyGraphSync>(ToOwner, &GraphSyncRequest::Id);
    Relay<GraphSyncPointRequest, NotifyGraphSyncPoint>(FromOwner, &GraphSyncPointRequest::Id);

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
    Relay<EffectFlashRequest, NotifyEffectFlash>(FromOwner, &EffectFlashRequest::SessionId);
    // A script's call one game sends to others (party-calls.md): HostPlayerNatives.
    // A quest one per player, learned by one game (system-quests.md): every other learns it.
    Relay<SystemQuestsRequest, NotifySystemQuests>(FromAnyone);
    // A guest's player-made potions (crafted-potions.md): the host's book, which HostNetwork passes on.
    On(*this, &HostAuthority::OnRecipes);

    // Objects (doors, containers, levers): whoever's game saw it, everyone else replays it.
    On(*this, &HostAuthority::OnActivate);
    On(*this, &HostAuthority::OnMeleeStagger);
    On(*this, &HostAuthority::OnLockChange);
    On(*this, &HostAuthority::OnContainerChange);

    // The map and chat
    Relay<RequestSetWaypoint, NotifySetWaypoint>(FromAnyone);
    Relay<RequestRemoveWaypoint, NotifyRemoveWaypoint>(FromAnyone);
    On(*this, &HostAuthority::OnChatMessage);
    On(*this, &HostAuthority::OnBugReport);

    m_network.SetFrontDoor([this](const RequestMessage& acMessage, ConnectionId_t aSender) { return Handle(acMessage, aSender); });
    // A player's game ended its connection: the others drop it, then its characters go.
    m_network.SetDisconnectionHandler([this](ConnectionId_t aConnection) {
        m_transfer.OnDisconnected(aConnection);
        if (!m_players.IsPlayer(aConnection))
            return;
        m_wait.OnPlayerLeft(aConnection);
        m_crime.Left(aConnection);
        m_session.OnPlayerLeft(aConnection);
        m_playerNatives.OnPlayerLeft(aConnection);
        m_records.OnPlayerLeft(aConnection);
        m_characters.OnPlayerLeft(aConnection);
        m_players.Remove(aConnection);
        m_timeScale.OnPlayerLeft(aConnection);
        m_dialogue.OnPlayerLeft(aConnection);
        m_worldState.OnPlayerLeft(aConnection);
        m_items.OnPlayerLeft(aConnection);
        m_physics.OnPlayerLeft(aConnection);
        m_hands.OnPlayerLeft(aConnection);
        m_death.Left(aConnection);
    });
}

void HostAuthority::Update(uint64_t aTick, double aDelta, uint64_t aFrameTick) noexcept
{
    PROFILE_ZONE("HostAuthority::Update");
    // Only the save transfer runs until the host's game loads it.
    if (m_reloading)
    {
        m_transfer.Update(aTick);
        return;
    }
    m_death.Update(aDelta);
    m_followers.Update(aTick);
    m_wait.Update(aTick);
    m_crime.Update();
    m_clock.Update(aTick);
    m_transfer.Update(aTick);
    m_weather.Update();
    m_worldState.Update(aTick);
    m_items.Update();
    m_hands.Update(aTick);
    if (m_game.TakePlaced)
        for (const auto& [cObject, cWhere] : m_game.TakePlaced())
            m_hands.OnHostPlaced(cObject, cWhere);
    m_physics.Update(aFrameTick ? aFrameTick : aTick);
}

void HostAuthority::DifficultyChanged(uint32_t aDifficulty) const noexcept
{
    m_session.OnDifficultyChange(aDifficulty);
}

void HostAuthority::Kick(uint32_t aPlayerId) noexcept
{
    m_players.Kick(aPlayerId);
}

void HostAuthority::ActorAnnounced(uint32_t aId, const RecordParts& acRecord) noexcept
{
    m_records.Announced(aId, acRecord);
    ActorAiState state;
    if (const RecordPart* cpAi = acRecord.Find(RecordKind::kAiState); cpAi && FromPartBytes(cpAi->Bytes, state))
        m_death.InitialAi(aId, state);
}

void HostAuthority::PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterSessionId) noexcept
{
    m_wait.OnPlayerEntered();
    m_crime.Entered(aPlayer);
    m_clock.SendTime(aPlayer);
    m_weather.SendWeather(aPlayer);
    m_worldState.SendWorldState(aPlayer);
    m_timeScale.SendTimeScale(aPlayer);
    m_characters.SendExistingCharacters(aPlayer, aCharacterSessionId);
    // Its player and every copy carry the party's gold and items, its own brought in replaced.
    m_purse.Send();
    m_partyItems.Send();
}

bool HostAuthority::Handle(const RequestMessage& acMessage, ConnectionId_t aSender) noexcept
{
    const auto cOpcode = acMessage.GetOpcode();
    // What a game holds is heard from any game a save was meant for, a joining one too, and during a reload.
    const bool cHeld = cOpcode == kSaveHeldRequest && (m_players.IsPlayer(aSender) || m_transfer.IsRecipient(aSender));
    if (m_reloading && !cHeld)
        return false;
    // A game that is not a player may only ask to join; anything else from it is dropped.
    if (cOpcode != kAuthenticationRequest && !cHeld && !m_players.IsPlayer(aSender))
        return false;

    if (cOpcode >= m_rules.size() || !m_rules[cOpcode])
        return false;
    for (const Rule& cWatcher : m_watchers[cOpcode])
        cWatcher(acMessage, aSender);
    m_rules[cOpcode](acMessage, aSender);
    return true;
}

void HostAuthority::OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept
{
    // An actor's items reach its copies as its owner's part (HostRecords, kItems); the party's gold and items here.
    if (m_purse.Change(acRequest, aSender) || m_partyItems.Change(acRequest, aSender))
        return;
    // The host's script changed another game's actor (a quest taking a guest's claw): its owner runs it, and its part follows.
    if (aSender == m_players.Host() && acRequest.UpdateClients && m_characters.OwnerOf(acRequest.SessionId) != aSender)
    {
        NotifyInventoryChanges notify;
        notify.SessionId = acRequest.SessionId;
        notify.Item = acRequest.Item;
        Deliver(notify, Delivery::ToOwner, acRequest.SessionId, aSender);
    }
}

void HostAuthority::OnMeleeStagger(const MeleeStaggerRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    if (m_characters.OwnerOf(acRequest.HitterId) != aSender || !m_characters.IsPlayerCharacter(acRequest.HitterId) ||
        !std::isfinite(acRequest.Magnitude) || acRequest.Magnitude <= 0.f)
        return;
    NotifyMeleeStagger notify;
    static_cast<Payloads::MeleeStagger&>(notify) = acRequest;
    Deliver(notify, Delivery::ToOwner, acRequest.HitteeId, aSender);
}

void HostAuthority::OnActivate(const ActivateRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // An object: the host's game runs it with the guest's copy; what changed reaches everyone as its rows.
    if (!acRequest.NpcId)
    {
        if (aSender != m_players.Host() && m_characters.OwnerOf(acRequest.ActivatorId) == aSender)
            m_network.Send(m_players.Host(), ToNotify(acRequest));
        return;
    }
    if (m_characters.OwnerOf(acRequest.ActivatorId) != aSender || m_characters.IsPlayerCharacter(acRequest.NpcId))
        return;
    const auto cOwner = m_characters.OwnerOf(acRequest.NpcId);
    if (cOwner == m_players.Host() && cOwner != aSender)
        m_network.Send(cOwner, ToNotify(acRequest));
}

void HostAuthority::OnRecipes(const RecipesRequest& acRequest, ConnectionId_t) const noexcept
{
    if (RecipeBook* pBook = m_network.GetRecipes())
        for (const Recipe& cRecipe : acRequest.Known)
            pBook->Learn(cRecipe);
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
    if (aSender == m_players.Host() || !m_game.SetItems)
        return;
    ObjectChange items{ObjectChange::Kind::kItems, acRequest.Container};
    items.Item = acRequest.Item;
    items.Value = acRequest.Count;
    items.Stacks = acRequest.Stacks;
    m_game.SetItems(items);
}

void HostAuthority::OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    // The chat box shows text as it is (RmlUi text, no markup parsed).
    NotifyChatMessageBroadcast notify;
    notify.MessageType = kPlayerChat;
    notify.PlayerName = ChatText(m_players.PlayerNameOf(aSender));
    notify.ChatMessage = ChatText(acRequest.ChatMessage);
    for (const ConnectionId_t cPlayer : m_players.Connections())
        m_network.Send(cPlayer, notify);
}

// A player's /bug: named here, so every game writes its part under the same report.
void HostAuthority::OnBugReport(const BugReportRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyBugReport notify;
    notify.Text = ChatText(acRequest.Text);
    notify.Id = Diagnostics::NewReportId();
    notify.By = ChatText(m_players.PlayerNameOf(aSender));
    notify.SessionTick = m_network.GetTick();
    spdlog::info("Bug report {} from {}: {}", notify.Id, notify.By, notify.Text);
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
    m_players.SendToOthers(notify, aSender, Net::kReliable);
}

void HostAuthority::NamePlayer(ConnectionId_t aPlayer, const std::string& acName) noexcept
{
    const std::string cName = ChatText(acName);
    if (cName.empty() || cName == m_players.PlayerNameOf(aPlayer))
        return;
    m_players.SetName(aPlayer, cName);
    m_network.NameConnection(aPlayer, cName);
    NotifyPlayerName notify;
    notify.PlayerId = m_players.PlayerIdOf(aPlayer);
    notify.Name = cName;
    m_players.SendToOthers(notify, aPlayer, Net::kReliable);
}

void HostAuthority::NameActor(uint32_t aSessionId, const std::string& acName) const noexcept
{
    const ConnectionId_t cOwner = m_characters.OwnerOf(aSessionId);
    if (cOwner == m_players.Host())
        return;
    NotifyActorName notify;
    notify.ActorId = aSessionId;
    notify.Name = acName;
    m_network.Send(cOwner, notify);
}

void HostAuthority::OnGraphOps(const GraphOpsRequest& acRequest, ConnectionId_t aSender) const noexcept
{
    NotifyGraphOps notify;
    notify.Ops.Frames = acRequest.Ops.Frames;
    for (const auto& cRun : acRequest.Ops.Runs)
        if (m_characters.OwnerOf(cRun.ActorId) == aSender)
            notify.Ops.Runs.push_back(cRun);
    if (!notify.Ops.Runs.empty())
        m_players.SendToOthers(notify, aSender, Net::kReliable);
}

void HostAuthority::Deliver(const NotifyMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept
{
    switch (aDelivery)
    {
    case Delivery::FromOwner:
        if (m_characters.OwnerOf(aActor) == aSender)
            m_players.SendToOthers(acNotify, aSender, Net::kReliable);
        else
            spdlog::debug("HostAuthority: {:x} sent opcode {} for {:x}, which it does not own", aSender, uint32_t(acNotify.GetOpcode()), aActor);
        break;
    case Delivery::FromAnyone: m_players.SendToOthers(acNotify, aSender, Net::kReliable); break;
    case Delivery::ToOwner:
        if (const ConnectionId_t cOwner = m_characters.OwnerOf(aActor); cOwner != 0 && cOwner != aSender)
            m_network.Send(cOwner, acNotify);
        break;
    }
}

std::string ChatText(std::string_view aText) noexcept
{
    // A character the limit cuts through is left out whole (UTF-8 continuation bytes are 10xxxxxx).
    size_t length = std::min(aText.size(), kChatMaxLength);
    while (length && length < aText.size() && (static_cast<unsigned char>(aText[length]) & 0xC0) == 0x80)
        --length;
    std::string text;
    for (const char c : aText.substr(0, length))
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F)
            text += c;
    return text;
}

std::vector<HostAuthority::ConnectionId_t> HostAuthority::Guests() const noexcept
{
    std::vector<ConnectionId_t> guests;
    m_players.ForEach([&](ConnectionId_t aConnection, const HostPlayers::Player& acPlayer) {
        if (!acPlayer.IsHost)
            guests.push_back(aConnection);
    });
    return guests;
}

void HostAuthority::SendCheckpoint(const std::string& acSave) noexcept
{
    m_reloading = true;
    m_transfer.Checkpoint(acSave, Guests());
}

void HostAuthority::OfferSave(const std::string& acSave) noexcept
{
    if (!m_reloading && !m_death.Wiped())
        m_transfer.Offer(acSave, Guests());
}
