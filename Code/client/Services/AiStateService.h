#pragma once

#include <Events/Listen.h>
#include <Structs/ActorAiState.h>

#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

class IActorBridge;
struct ActorAiFields;

struct World;
struct TransportService;
struct AiStateChangeEvent;
struct DisconnectedEvent;
struct NotifyCharacterSpawn;
struct NotifyRemoveCharacter;

/**
 * @brief What actors' AI is doing (package, scene, combat), as every game reads it (docs/design/copy-contract.md,
 * "AI state").
 *
 * Only an actor's owner simulates it. The owner sends an actor's AI state when the engine says it changed
 * (AiStateEvents) and its spawn carries it; each copy holds its owner's (IActorBridge::SetCopyAiFields).
 */
struct AiStateService
{
    AiStateService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept;
    ~AiStateService() noexcept = default;

    NO_COPY_MOVE(AiStateService);

    // One of this game's actors' AI state as the session has it (a spawn of it carries it).
    [[nodiscard]] ActorAiState ReadState(uint32_t aFormId) const noexcept;

protected:
    void OnAiStateChange(const AiStateChangeEvent& acEvent) noexcept;
    void OnNotifyAiState(const NotifyAiState& acMessage) noexcept;
    void OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

private:
    // One of this game's actors: its state goes out when it differs from the last sent.
    void SendOwned(uint32_t aFormId, uint32_t aSessionId) noexcept;
    // The copy of that session id, if this game has it, takes its owner's state.
    void ApplyToCopy(uint32_t aSessionId) noexcept;
    void CountCopiesFightingPlayer() noexcept;
    // This game's fields as the session's state, and back.
    [[nodiscard]] ActorAiState ToState(const ActorAiFields& acFields) const noexcept;
    [[nodiscard]] ActorAiFields ToFields(const ActorAiState& acState) const noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;

    // Per owned actor (session id): the state last sent.
    std::unordered_map<uint32_t, ActorAiState> m_sent;
    // Per copy (session id): its owner's state.
    std::unordered_map<uint32_t, ActorAiState> m_owners;

    std::vector<entt::scoped_connection> m_connections;
};
