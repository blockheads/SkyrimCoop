#pragma once

#include <network/ENetInterface.h>
#include <Messages/Message.h>
#include <Opcodes.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostLeash.h>
#include <Services/Host/HostClock.h>
#include <Services/Host/HostDevTools.h>
#include <Services/Host/HostDeath.h>
#include <Services/Host/HostDialogue.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostFollowers.h>
#include <Services/Host/HostBodies.h>
#include <Services/Host/HostItems.h>
#include <Services/Host/HostPlayerNatives.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostRecords.h>
#include <Services/Host/HostSaveTransfer.h>
#include <Services/Host/HostSession.h>
#include <Services/Host/HostStory.h>
#include <Services/Host/HostTimeScale.h>
#include <Services/Host/HostWait.h>
#include <Services/Host/HostCrime.h>
#include <Services/Host/HostPartyItems.h>
#include <Services/Host/HostWeather.h>
#include <Services/Host/HostEye.h>
#include <Services/Host/HostWorldState.h>

#include <array>
#include <optional>
#include <vector>
#include <functional>
#include <string_view>

#include <ChatMessageTypes.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>
#include <Messages/NotifySetWaypoint.h>

struct LockChangeRequest;
struct RequestSetWaypoint;
struct RequestRemoveWaypoint;
struct ContainerChangeRequest;
struct RequestInventoryChanges;
struct SendChatMessageRequest;
struct PlayerLevelRequest;

struct HostNetwork;
struct ActivateRequest;
struct SummonRequest;

/**
 * @brief The host's side of the session: handles what players send (docs/design/host-authority-migration.md).
 *
 * Every message the host's network (HostNetwork) receives comes here; one with no rule is dropped. The host is a
 * player like the others: what it sends comes through here in-process, and what is forwarded to it reaches its
 * copies through the in-process route.
 *
 * HostAuthority is the front door and the plain relays: most rules pass a request on as its
 * notification, which carries the same payload (Messages/Payloads.h), and differ only in who
 * receives it (Delivery). The parts that keep state register their own rules: HostSession (who
 * gets in), HostCharacters (ids, owners, spawns), HostClock (the session's time), HostTimeScale
 * (how fast everyone's clock runs), HostDialogue (the party's conversation), HostWorldState (a guest's cell
 * loading), HostItems (a guest's drops and pickups), HostPlayerNatives (the script calls one game sends to others),
 * HostStory (a guest's story events),
 * HostBodies (who simulates each physics body, and the states each game gets); HostWeather and HostEye (a guest's
 * stealth eye) have no rules, and HostPartyItems (the party's gold) takes a player's gold out of the item relay.
 */
struct HostAuthority
{
    using ConnectionId_t = Net::ConnectionId_t;

    // Installs itself on aNetwork (front door, leaving players), which calls into it from then on:
    // the network goes first.
    HostAuthority(HostNetwork& aNetwork, HostGame aGame) noexcept;

    NO_COPY_MOVE(HostAuthority);

    // True when the message was handled here.
    bool Handle(const RequestMessage& acMessage, ConnectionId_t aSender) noexcept;
    // Every frame: the session's tick (milliseconds) and the game time it took (seconds, none while paused).
    // aFrameTick: the frame clock (World::GetFrameTick), what physics frames are stamped with (S10); 0: aTick.
    void Update(uint64_t aTick, double aDelta, uint64_t aFrameTick = 0) noexcept;
    // A party reload (HostServiceReload.cpp): the selected save to every guest, delivered once every guest has it and
    // has left to load it; a failed reload's reason to every player.
    void SendCheckpoint(const std::string& acSave) noexcept;
    // The host's game made a save (its image's id): ahead to every guest, for a later load of it.
    void OfferSave(const std::string& acSave) noexcept;
    [[nodiscard]] bool HasGuests() const noexcept { return m_players.Count() > 1; }
    [[nodiscard]] bool CheckpointDelivered() const noexcept { return m_transfer.Empty() && m_players.Count() <= 1; }
    void FailReload(const std::string& acError) noexcept { m_death.FailReload(acError); }
    // This game's engine renamed an actor another game owns (HostService): its owner names its actor, and its name part
    // carries that on.
    void NameActor(uint32_t aSessionId, const std::string& acName) const noexcept;

    // For the parts: TRequest is handled by aOwner's member function.
    template <class TOwner, class TRequest> void On(TOwner& aOwner, void (TOwner::*apRule)(const TRequest&, ConnectionId_t) noexcept) noexcept
    {
        m_rules[TRequest::Opcode] = [&aOwner, apRule](const RequestMessage& acMessage, ConnectionId_t aSender) {
            (aOwner.*apRule)(static_cast<const TRequest&>(acMessage), aSender);
        };
    }
    template <class TOwner, class TRequest> void On(TOwner& aOwner, void (TOwner::*apRule)(const TRequest&, ConnectionId_t) const noexcept) noexcept
    {
        m_rules[TRequest::Opcode] = [&aOwner, apRule](const RequestMessage& acMessage, ConnectionId_t aSender) {
            (aOwner.*apRule)(static_cast<const TRequest&>(acMessage), aSender);
        };
    }
    // For the parts: also told of TRequest before its rule runs (another part's, or a relay).
    template <class TOwner, class TRequest> void Watch(TOwner& aOwner, void (TOwner::*apWatcher)(const TRequest&, ConnectionId_t) noexcept) noexcept
    {
        m_watchers[TRequest::Opcode].push_back([&aOwner, apWatcher](const RequestMessage& acMessage, ConnectionId_t aSender) {
            (aOwner.*apWatcher)(static_cast<const TRequest&>(acMessage), aSender);
        });
    }

    // A player's own character is in the world: everything it must know about the session, from every store.
    void PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterSessionId) noexcept;
    // What a joiner gets, in order (one-simulator.md piece 9): every store holding session state is on it.
    struct JoinerStore
    {
        std::string_view Name;
        std::function<void(ConnectionId_t aPlayer, uint32_t aCharacterSessionId)> Send;
    };
    [[nodiscard]] const std::vector<JoinerStore>& JoinerStores() const noexcept { return m_joinerStores; }
    // A player's character is in the host's world, and the record a character arrived with (its AI state counts for
    // death).
    void PlayerBodyEntered(ConnectionId_t aPlayer, uint32_t aId) noexcept { m_death.Entered(aPlayer, aId); }
    void ActorAnnounced(uint32_t aId, const RecordParts& acRecord) noexcept;
    // A character left the session: its record goes.
    void ActorRemoved(uint32_t aId) noexcept { m_records.Forget(aId); }
    // The host's game took back a claimed actor given back (HostClaims::Returning ends).
    void ActorTaken(uint32_t aId) noexcept { m_characters.Claims().Taken(aId); }
    // A character's record as its owner last declared it, for a spawn of it.
    [[nodiscard]] RecordParts RecordOf(uint32_t aId) const noexcept { return m_records.PartsOf(aId); }
    // The host removes a player from the session.
    void Kick(uint32_t aPlayerId) noexcept;
    // The host's game set its difficulty (DifficultyChangeEvent).
    void DifficultyChanged(uint32_t aDifficulty) const noexcept;
    // The host's game is saving: the guests' records take their characters' places (HostRecords).
    void Saving() const noexcept { m_records.OnSaving(); }
    // A session id for one of the host's actors (HostCharacters).
    [[nodiscard]] uint32_t NewCharacterId() noexcept { return m_characters.NewCharacterId(); }

private:
    enum class Delivery
    {
        FromOwner,  // the actor's owner says it; everyone else replays it
        FromAnyone, // whoever saw it happen; everyone else replays it
        ToOwner,    // something done to another's actor: its owner alone applies it
    };

    using Rule = std::function<void(const RequestMessage&, ConnectionId_t aSender)>;

    // TRequest passed on as TNotify (the same payload); apActor names the actor Delivery checks.
    template <class TRequest, class TNotify> void Relay(Delivery aDelivery, uint32_t TRequest::*apActor = nullptr) noexcept;
    [[nodiscard]] std::vector<ConnectionId_t> Guests() const noexcept;

    // Anyone may change an actor's items (a peer handing its follower a sword); the owner's game
    // applies it like every other copy.
    void OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) noexcept;
    void OnMeleeStagger(const MeleeStaggerRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void OnProjectileLaunch(const ProjectileLaunchRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void OnActivate(const ActivateRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A guest's player cast a summon on itself: the host's game puts it on that player's copy (the host creates).
    void OnSummon(const SummonRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // An owner game's graph ops (docs/design/graph-input-stream.md): each actor's run goes to the others if the sender
    // owns that actor.
    void OnGraphOps(const GraphOpsRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A guest's lock change (its player picked it, or a key): a proposal the host's world takes (step 6).
    // A trap touched a guest's player, from its own game; a trap's hit on a guest's player, from the host's.
    void OnTrapContact(const TrapContactRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void OnTrapHit(const TrapHitRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void OnLockChange(const LockChangeRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void OnRecipes(const RecipesRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A guest's container change (its player took or stored an item): a proposal the host's world takes (step 6).
    void OnContainerChange(const ContainerChangeRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A chat line goes to everyone, the sender too (its chat shows what it sent), under its name.
    void OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept;
    void OnBugReport(const BugReportRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A player's character levelled up: kept for the player list (late joiners) and sent to the others.
    void OnPlayerLevel(const PlayerLevelRequest& acRequest, ConnectionId_t aSender) noexcept;
    // The party's map marker (every game shows the last one set), kept for a joiner.
    void OnSetWaypoint(const RequestSetWaypoint& acRequest, ConnectionId_t aSender) noexcept;
    void OnRemoveWaypoint(const RequestRemoveWaypoint& acRequest, ConnectionId_t aSender) noexcept;
    // A player's character's name part changed: the party's lists take it (printable text only).
    void NamePlayer(ConnectionId_t aPlayer, const std::string& acName) noexcept;

    void Deliver(const NotifyMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept;

    HostNetwork& m_network;
    HostGame m_game;
    std::array<Rule, kRequestOpcodeMax> m_rules{};
    std::array<std::vector<Rule>, kRequestOpcodeMax> m_watchers{};
    // The parts, after the rules they register into, and the players first: the others read them.
    HostPlayers m_players;
    HostSaveTransfer m_transfer;
    HostSession m_session;
    HostCharacters m_characters;
    HostClock m_clock;
    HostWait m_wait;
    HostCrime m_crime;
    HostPartyItems m_partyItems;
    HostWeather m_weather;
    HostEye m_eye;
    HostWorldState m_worldState;
    HostItems m_items;
    HostBodies m_bodies;
    HostTimeScale m_timeScale;
    HostDialogue m_dialogue;
    HostPlayerNatives m_playerNatives;
    HostRecords m_records;
    HostStory m_story;
    HostDeath m_death;
    HostLeash m_leash;
    HostFollowers m_followers;
    HostDevTools m_devTools;
    std::vector<JoinerStore> m_joinerStores;
    std::optional<NotifySetWaypoint> m_waypoint;
    bool m_reloading{};
};

// A player's text as chat shows it: at most kChatMaxLength bytes (whole characters), no control characters.
[[nodiscard]] std::string ChatText(std::string_view aText) noexcept;
