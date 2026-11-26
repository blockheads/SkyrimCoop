#pragma once

#include <Messages/Message.h>
#include <NoCopyMove.h>
#include <Opcodes.h>
#include <network/ENetInterface.h>

#include <array>
#include <functional>
#include <type_traits>
#include <vector>

struct HostNetwork;
struct HostPlayers;

// The host's table of rules: which part handles each request a game sends. The parts (Host* files) register their
// own rules here; it knows none of them. HostAuthority owns it and decides who may send at all.
struct HostRouter
{
    using ConnectionId_t = Net::ConnectionId_t;
    // Who owns an actor (HostCharacters::OwnerOf), 0 for none.
    using OwnerOf = std::function<ConnectionId_t(uint32_t aActor)>;

    // Who receives a request passed on as its notification.
    enum class Delivery
    {
        FromOwner,  // the actor's owner says it; everyone else replays it
        FromAnyone, // whoever saw it happen; everyone else replays it
        ToOwner,    // something done to another's actor: its owner alone applies it
    };

    HostRouter(HostNetwork& aNetwork, const HostPlayers& acPlayers) noexcept;

    NO_COPY_MOVE(HostRouter);

    void SetOwners(OwnerOf aOwnerOf) noexcept { m_ownerOf = std::move(aOwnerOf); }

    // TRequest is handled by aOwner's member function.
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
    // Also told of TRequest before its rule runs (another part's, or a relay).
    template <class TOwner, class TRequest> void Watch(TOwner& aOwner, void (TOwner::*apWatcher)(const TRequest&, ConnectionId_t) noexcept) noexcept
    {
        m_watchers[TRequest::Opcode].push_back([&aOwner, apWatcher](const RequestMessage& acMessage, ConnectionId_t aSender) {
            (aOwner.*apWatcher)(static_cast<const TRequest&>(acMessage), aSender);
        });
    }
    // TRequest passed on as TNotify, which carries the same payload (Messages/Payloads.h); apActor names the actor
    // Delivery checks.
    template <class TRequest, class TNotify> void Relay(Delivery aDelivery, uint32_t TRequest::*apActor = nullptr) noexcept
    {
        static_assert(std::is_same_v<typename TRequest::Payload, typename TNotify::Payload>);
        m_rules[TRequest::Opcode] = [this, aDelivery, apActor](const RequestMessage& acMessage, ConnectionId_t aSender) {
            const auto& cRequest = static_cast<const TRequest&>(acMessage);
            TNotify notify;
            static_cast<typename TNotify::Payload&>(notify) = cRequest;
            Deliver(notify, aDelivery, apActor ? cRequest.*apActor : 0, aSender);
        };
    }

    void Deliver(const NotifyMessage& acNotify, Delivery aDelivery, uint32_t aActor, ConnectionId_t aSender) const noexcept;
    // Runs the request's watchers, then its rule. False: no rule, the request is dropped.
    bool Dispatch(const RequestMessage& acMessage, ConnectionId_t aSender) const noexcept;

  private:
    using Rule = std::function<void(const RequestMessage&, ConnectionId_t aSender)>;

    HostNetwork& m_network;
    const HostPlayers& m_players;
    OwnerOf m_ownerOf;
    std::array<Rule, kRequestOpcodeMax> m_rules{};
    std::array<std::vector<Rule>, kRequestOpcodeMax> m_watchers{};
};
