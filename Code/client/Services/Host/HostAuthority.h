#pragma once

#include <network/ENetInterface.h>
#include <Messages/Message.h>
#include <Opcodes.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostClock.h>
#include <Services/Host/HostDialogue.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostHands.h>
#include <Services/Host/HostPhysics.h>
#include <Services/Host/HostPlayerNatives.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostRecords.h>
#include <Services/Host/HostSaveTransfer.h>
#include <Services/Host/HostSession.h>
#include <Services/Host/HostTimeScale.h>
#include <Services/Host/HostWait.h>
#include <Services/Host/HostWeather.h>
#include <Services/Host/HostWorldState.h>

#include <array>
#include <functional>
#include <string_view>

#include <ChatMessageTypes.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct LockChangeRequest;
struct ContainerChangeRequest;
struct RequestInventoryChanges;
struct RequestFactionsChanges;
struct SendChatMessageRequest;
struct PlayerLevelRequest;

struct HostNetwork;

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
 * loading), HostPlayerNatives (natives the host's scripts ran for a guest) and HostPhysics (the host's bodies) and
 * HostHands (a guest's player's pushes and holds of them); HostWeather has no rules.
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
    // Every frame.
    void Update(uint64_t aTick) noexcept;

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

    // A player's own character is in the world: everything it must know about the session.
    void PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterSessionId) noexcept;
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

    // Anyone may change an actor's items (a peer handing its follower a sword); the owner's game
    // applies it like every other copy.
    void OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Each actor's factions from its owner (one message carries many actors).
    void OnFactionsChanges(const RequestFactionsChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // A guest's lock change (its player picked it, or a key): a proposal the host's world takes (step 6).
    void OnLockChange(const LockChangeRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A guest's container change (its player took or stored an item): a proposal the host's world takes (step 6).
    void OnContainerChange(const ContainerChangeRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A chat line goes to everyone, the sender too (its chat shows what it sent), under its name.
    void OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A script's party call (cutscenes.md): every other game runs it, told whose game's script it was.
    void OnPartyNative(const PartyNativeRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A player's character levelled up: kept for the player list (late joiners) and sent to the others.
    void OnPlayerLevel(const PlayerLevelRequest& acRequest, ConnectionId_t aSender) noexcept;

    void Deliver(const NotifyMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept;

    HostNetwork& m_network;
    HostGame m_game;
    std::array<Rule, kRequestOpcodeMax> m_rules{};
    // The parts, after the rules they register into, and the players first: the others read them.
    HostPlayers m_players;
    HostSaveTransfer m_transfer;
    HostSession m_session;
    HostCharacters m_characters;
    HostClock m_clock;
    HostWait m_wait;
    HostWeather m_weather;
    HostWorldState m_worldState;
    HostPhysics m_physics;
    HostHands m_hands;
    HostTimeScale m_timeScale;
    HostDialogue m_dialogue;
    HostPlayerNatives m_playerNatives;
    HostRecords m_records;
};

// A player's text as chat shows it: at most kChatMaxLength bytes (whole characters), no control characters.
[[nodiscard]] std::string ChatText(std::string_view aText) noexcept;
