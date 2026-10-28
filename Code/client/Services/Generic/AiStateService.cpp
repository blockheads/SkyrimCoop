#include <Services/AiStateService.h>
#include <Services/TransportService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Messages/Relayed.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <SessionIds.h>
#include <World.h>
#include <FormIds.h>

namespace
{
// The player's reference in every game (PlayerCharacter).
constexpr uint32_t cPlayerFormId = 0x14;
} // namespace

AiStateService::AiStateService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
{
    Listen<AiStateChangeEvent, &AiStateService::OnAiStateChange>(m_connections, aDispatcher, this);
    Listen<NotifyAiState, &AiStateService::OnNotifyAiState>(m_connections, aDispatcher, this);
    Listen<NotifyCharacterSpawn, &AiStateService::OnCharacterSpawn>(m_connections, aDispatcher, this);
    Listen<NotifyRemoveCharacter, &AiStateService::OnRemoveCharacter>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &AiStateService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<&AiStateService::OnLocalRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
}

void AiStateService::OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    // A later ownership term publishes its state again, even when it matches this game's last term.
    m_sent.erase(aRegistry.get<LocalComponent>(aEntity).Id);
}

ActorAiState AiStateService::ReadState(uint32_t aFormId) const noexcept
{
    return ToState(m_actorBridge.GetActorAiFields(aFormId));
}

void AiStateService::OnAiStateChange(const AiStateChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;
    auto view = m_world.view<FormIdComponent, LocalComponent>();
    for (const auto cEntity : view)
    {
        const uint32_t cFormId = view.get<FormIdComponent>(cEntity).Id;
        if (!acEvent.ActorId || acEvent.ActorId == cFormId)
            SendOwned(cFormId, view.get<LocalComponent>(cEntity).Id);
    }
}

void AiStateService::SendOwned(uint32_t aFormId, uint32_t aSessionId) noexcept
{
    if (!m_actorBridge.ActorExists(aFormId))
        return;
    const ActorAiState cState = ReadState(aFormId);
    const auto cLast = m_sent.find(aSessionId);
    if (cLast != m_sent.end() && cLast->second == cState)
        return;
    m_sent[aSessionId] = cState;

    AiStateRequest request;
    request.SessionId = aSessionId;
    request.State = cState;
    m_transport.Send(request);
    spdlog::debug("AI state of {:X} (session {}): package {:X}:{:X}, scene {:X}:{:X}, combat {} on {}", aFormId, aSessionId,
                  cState.Package.ModId, cState.Package.BaseId, cState.Scene.ModId, cState.Scene.BaseId, cState.Combat,
                  cState.CombatTarget.SessionId);
}

void AiStateService::OnNotifyAiState(const NotifyAiState& acMessage) noexcept
{
    m_owners[acMessage.SessionId] = acMessage.State;
    ApplyToCopy(acMessage.SessionId);
}

void AiStateService::OnCharacterSpawn(const NotifyCharacterSpawn& acMessage) noexcept
{
    m_owners[acMessage.SessionId] = acMessage.AiState;
    // After every handler of the spawn: CharacterService makes the copy in its own.
    m_world.GetRunner().Queue([this, cSessionId = acMessage.SessionId]() { ApplyToCopy(cSessionId); });
}

void AiStateService::OnRemoveCharacter(const NotifyRemoveCharacter& acMessage) noexcept
{
    if (m_owners.erase(acMessage.SessionId))
        CountCopiesFightingPlayer();
}

void AiStateService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_sent.clear();
    m_owners.clear();
    m_actorBridge.SetCopiesFightingPlayer(0);
}

void AiStateService::ApplyToCopy(uint32_t aSessionId) noexcept
{
    const auto cOwner = m_owners.find(aSessionId);
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
    if (cOwner == m_owners.end() || !cFormId || !m_actorBridge.IsActorCopy(cFormId))
        return;
    m_actorBridge.SetCopyAiFields(cFormId, ToFields(cOwner->second));
    CountCopiesFightingPlayer();
}

void AiStateService::CountCopiesFightingPlayer() noexcept
{
    uint32_t count = 0;
    for (const auto& [cSessionId, cState] : m_owners)
        if (cState.Combat && ToFields(cState).CombatTargetId == cPlayerFormId && Utils::GetFormIdBySessionId(cSessionId))
            ++count;
    m_actorBridge.SetCopiesFightingPlayer(count);
}

ActorAiState AiStateService::ToState(const ActorAiFields& acFields) const noexcept
{
    ActorAiState state;
    // A created form (an engine-made package) has no id the other games share: none.
    state.Package = ToSession(acFields.PackageId).value_or(GameId{});
    state.Scene = ToSession(acFields.SceneId).value_or(GameId{});
    state.Combat = acFields.CombatState;
    // An actor in the session goes by its session id (each game has its own form id for it), else by its form.
    if (const auto cSessionId = Utils::GetSessionIdByFormId(acFields.CombatTargetId))
        state.CombatTarget.SessionId = *cSessionId;
    else
        state.CombatTarget.RefId = ToSession(acFields.CombatTargetId).value_or(GameId{});
    return state;
}

ActorAiFields AiStateService::ToFields(const ActorAiState& acState) const noexcept
{
    ActorAiFields fields;
    if (acState.Package != GameId{})
        fields.PackageId = ToGame(acState.Package);
    if (acState.Scene != GameId{})
        fields.SceneId = ToGame(acState.Scene);
    fields.CombatState = acState.Combat;
    if (acState.CombatTarget.SessionId)
        fields.CombatTargetId = Utils::GetFormIdBySessionId(acState.CombatTarget.SessionId);
    else if (acState.CombatTarget.RefId != GameId{})
        fields.CombatTargetId = ToGame(acState.CombatTarget.RefId);
    return fields;
}
