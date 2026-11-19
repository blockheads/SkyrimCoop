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
struct UpdateEvent;
struct NotifyRemoveCharacter;
struct CopyRecords;

/**
 * @brief What actors' AI is doing (package, scene, combat), as every game reads it (docs/design/copy-contract.md,
 * "AI state").
 *
 * Only an actor's owner simulates it. The owner declares an actor's AI state as a part of its record (CopyRecords)
 * when the engine says it changed (AiStateEvents); each copy holds its owner's (IActorBridge::SetCopyAiFields),
 * whenever the copy or an actor the state names appears.
 */
struct AiStateService
{
    AiStateService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge, CopyRecords& aRecords) noexcept;
    ~AiStateService() noexcept;

    NO_COPY_MOVE(AiStateService);

    // One of this game's actors' AI state as the session has it.
    [[nodiscard]] ActorAiState ReadState(uint32_t aFormId) const noexcept;

protected:
    void OnAiStateChange(const AiStateChangeEvent& acEvent) noexcept;
    void OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept;
    // One of this game's actors: its state goes out when it differs from the last sent.
    void SendOwned(uint32_t aFormId, uint32_t aSessionId) noexcept;
    // The copy of that session id, if this game has it, takes its owner's state (the record's part). Returns the
    // actors the state names that are not here yet.
    [[nodiscard]] std::vector<uint32_t> ApplyToCopy(uint32_t aSessionId, const std::string& acBytes) noexcept;
    void CountCopiesFightingPlayer() noexcept;
    // This game's fields as the session's state, and back.
    [[nodiscard]] ActorAiState ToState(const ActorAiFields& acFields) const noexcept;
    [[nodiscard]] ActorAiFields ToFields(const ActorAiState& acState) const noexcept;
    // The session ids acState names whose actors this game does not have.
    [[nodiscard]] static std::vector<uint32_t> NotHere(const ActorAiState& acState) noexcept;
    // A reference: an actor in the session by its session id (each game has its own form id for it), else its form.
    [[nodiscard]] static AiTarget ToTarget(uint32_t aFormId) noexcept;
    [[nodiscard]] static uint32_t ToFormId(const AiTarget& acTarget) noexcept;

    World& m_world;
    TransportService& m_transport;
    IActorBridge& m_actorBridge;
    CopyRecords& m_records;

    // Per owned actor (session id): the state last sent.
    std::unordered_map<uint32_t, ActorAiState> m_sent;

    std::vector<entt::scoped_connection> m_connections;
};
