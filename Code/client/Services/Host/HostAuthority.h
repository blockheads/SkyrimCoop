#pragma once

#include <ENetInterface.hpp>
#include <Messages/Message.h>
#include <Opcodes.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostClock.h>
#include <Services/Host/HostDialogue.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostPlayers.h>
#include <Services/Host/HostSession.h>
#include <Services/Host/HostTimeScale.h>
#include <Services/Host/HostWeather.h>
#include <Services/Host/HostWorldState.h>

#include <array>
#include <functional>
#include <string_view>

#include <ChatMessageTypes.h>

struct ActivateRequest;
struct ScriptAnimationRequest;
struct RequestInventoryChanges;
struct RequestFactionsChanges;
struct SendChatMessageRequest;
struct PlayerLevelRequest;

namespace Server
{
struct GameServer;
}

/**
 * @brief The host's side of the session: handles what players send, in place of the embedded
 * server's services (docs/design/host-authority-migration.md).
 *
 * It sees every message first. A message it has a rule for is handled here; anything else still
 * goes to the embedded server's services while the migration is under way. The host is a player
 * like the others: what it sends comes through here in-process, and what is forwarded to it
 * reaches its copies through the in-process route.
 *
 * HostAuthority is the front door and the plain relays: most rules pass a request on as its
 * notification, which carries the same payload (Messages/Payloads.h), and differ only in who
 * receives it (Delivery). The parts that keep state register their own rules: HostSession (who
 * gets in), HostCharacters (ids, owners, spawns), HostClock (the session's time), HostTimeScale
 * (how fast everyone's clock runs) and HostDialogue (the party's conversation); HostWeather and
 * HostWorldState (globals, quests) have no rules.
 */
struct HostAuthority
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // Installs itself on aServer (front door, leaving players), which calls into it from then on:
    // it must outlive the server.
    HostAuthority(Server::GameServer& aServer, HostGame aGame) noexcept;

    TP_NOCOPYMOVE(HostAuthority);

    // True when the message was handled here.
    bool Handle(const ClientMessage& acMessage, ConnectionId_t aSender) noexcept;
    // Every frame.
    void Update(uint64_t aTick) noexcept;

    // For the parts: TRequest is handled by aOwner's member function.
    template <class TOwner, class TRequest> void On(TOwner& aOwner, void (TOwner::*apRule)(const TRequest&, ConnectionId_t) noexcept) noexcept
    {
        m_rules[TRequest::Opcode] = [&aOwner, apRule](const ClientMessage& acMessage, ConnectionId_t aSender) {
            (aOwner.*apRule)(static_cast<const TRequest&>(acMessage), aSender);
        };
    }
    template <class TOwner, class TRequest> void On(TOwner& aOwner, void (TOwner::*apRule)(const TRequest&, ConnectionId_t) const noexcept) noexcept
    {
        m_rules[TRequest::Opcode] = [&aOwner, apRule](const ClientMessage& acMessage, ConnectionId_t aSender) {
            (aOwner.*apRule)(static_cast<const TRequest&>(acMessage), aSender);
        };
    }

    // A player's own character is in the world: everything it must know about the session.
    void PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterServerId) noexcept;
    // The host removes a player from the session.
    void Kick(uint32_t aPlayerId) noexcept;

private:
    enum class Delivery
    {
        FromOwner,  // the actor's owner says it; everyone else replays it
        FromAnyone, // whoever saw it happen; everyone else replays it
        ToOwner,    // something done to another's actor: its owner alone applies it
    };

    using Rule = std::function<void(const ClientMessage&, ConnectionId_t aSender)>;

    // TRequest passed on as TNotify (the same payload); apActor names the actor Delivery checks.
    template <class TRequest, class TNotify> void Relay(Delivery aDelivery, uint32_t TRequest::*apActor = nullptr) noexcept;

    // Anyone may change an actor's items (a peer handing its follower a sword); the owner's game
    // applies it like every other copy.
    void OnInventoryChanges(const RequestInventoryChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // Each actor's factions from its owner (one message carries many actors).
    void OnFactionsChanges(const RequestFactionsChanges& acRequest, ConnectionId_t aSender) const noexcept;
    // An activation reaches everyone else; one by a reference (a script's Activate) only from the host,
    // whose world scripts are the only ones that run (docs/design/world-state-sync.md step 5).
    void OnActivate(const ActivateRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A script's animation on an object (a claw door's ring turning): only the host's scripts play them.
    void OnScriptAnimation(const ScriptAnimationRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A chat line goes to everyone, the sender too (its chat shows what it sent), under its name.
    void OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept;
    // A player's character levelled up: kept for the player list (late joiners) and sent to the others.
    void OnPlayerLevel(const PlayerLevelRequest& acRequest, ConnectionId_t aSender) noexcept;

    void Deliver(const ServerMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept;

    Server::GameServer& m_server;
    HostGame m_game;
    std::array<Rule, kClientOpcodeMax> m_rules{};
    // The parts, after the rules they register into, and the players first: the others read them.
    HostPlayers m_players;
    HostSession m_session;
    HostCharacters m_characters;
    HostClock m_clock;
    HostWeather m_weather;
    HostWorldState m_worldState;
    HostTimeScale m_timeScale;
    HostDialogue m_dialogue;
};

// A player's text as chat shows it: at most kChatMaxLength bytes (whole characters), no control characters.
[[nodiscard]] TiltedPhoques::String ChatText(std::string_view aText) noexcept;
