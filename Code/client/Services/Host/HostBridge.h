#pragma once

#include <ENetInterface.hpp>
#include <Messages/Message.h>
#include <Opcodes.h>
#include <Services/Host/HostCharacters.h>
#include <Services/Host/HostClock.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostWeather.h>

#include <array>
#include <functional>

#include <ChatMessageTypes.h>

struct RequestInventoryChanges;
struct RequestFactionsChanges;
struct SendChatMessageRequest;

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
 * The bridge is the front door and the plain relays: most rules pass a request on as its
 * notification, which carries the same payload (Messages/Payloads.h), and differ only in who
 * receives it (Delivery). The parts that keep state register their own rules: HostCharacters
 * (ids, owners, spawns) and HostClock (the session's time); HostWeather has no rules.
 */
struct HostBridge
{
    using ConnectionId_t = TiltedPhoques::ConnectionId_t;

    // Installs itself on aServer (front door, leaving players), which calls into it from then on:
    // the bridge must outlive the server.
    HostBridge(Server::GameServer& aServer, HostGame aGame) noexcept;

    TP_NOCOPYMOVE(HostBridge);

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
    void PlayerEntered(ConnectionId_t aPlayer, uint32_t aCharacterServerId) const noexcept;
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
    // A chat line goes to everyone, the sender too (its chat shows what it sent), under its name.
    void OnChatMessage(const SendChatMessageRequest& acRequest, ConnectionId_t aSender) const noexcept;

    void Deliver(const ServerMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept;

    Server::GameServer& m_server;
    HostGame m_game;
    std::array<Rule, kClientOpcodeMax> m_rules{};
    // The parts, after the rules they register into.
    HostCharacters m_characters;
    HostClock m_clock;
    HostWeather m_weather;
};
