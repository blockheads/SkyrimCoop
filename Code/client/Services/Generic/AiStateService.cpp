#include <Services/AiStateService.h>
#include <Services/SceneRefresh.h>
#include <Events/UpdateEvent.h>
#include <Services/TransportService.h>

#include <Bridges/IActorBridge.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Components/RemoteComponent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/CopyAiStateEvent.h>
#include <Services/CopyRecords.h>
#include <Events/DisconnectedEvent.h>
#include <Messages/Relayed.h>
#include <SessionIds.h>
#include <World.h>
#include <FormIds.h>
#include <CreatedIds.h>
#include <Events/ReferenceLoadedEvent.h>
#include <Events/Copy3DEvent.h>
#include <Diagnostics/Profile.h>

namespace
{
// The player's reference in every game (PlayerCharacter).
constexpr uint32_t cPlayerFormId = 0x14;
} // namespace

AiStateService::AiStateService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IActorBridge& aActorBridge, CopyRecords& aRecords) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
    , m_actorBridge(aActorBridge)
    , m_records(aRecords)
{
    m_records.Register(
        RecordKind::kAiState, [this](uint32_t aSessionId, const std::string& acBytes) { return ApplyToCopy(aSessionId, acBytes); },
        [this](uint32_t aFormId) -> std::optional<std::string> { return ToPartBytes(ReadState(aFormId)); });
    Listen<UpdateEvent, &AiStateService::OnUpdate>(m_connections, aDispatcher, this);
    Listen<AiStateChangeEvent, &AiStateService::OnAiStateChange>(m_connections, aDispatcher, this);
    Listen<DisconnectedEvent, &AiStateService::OnDisconnected>(m_connections, aDispatcher, this);
    Listen<ReferenceLoadedEvent, &AiStateService::OnReferenceLoaded>(m_connections, aDispatcher, this);
    Listen<Copy3DEvent, &AiStateService::OnCopy3D>(m_connections, aDispatcher, this);
    Listen<&AiStateService::OnLocalRemoved>(m_connections, m_world.on_destroy<LocalComponent>(), this);
    Listen<&AiStateService::OnSessionActor>(m_connections, m_world.on_construct<LocalComponent>(), this);
    Listen<&AiStateService::OnSessionActor>(m_connections, m_world.on_construct<RemoteComponent>(), this);
}

void AiStateService::OnReferenceLoaded(const ReferenceLoadedEvent& acEvent) noexcept
{
    RelinkSeats(acEvent.FormId);
}

void AiStateService::OnCopy3D(const Copy3DEvent& acEvent) noexcept
{
    RelinkSeats(acEvent.FormId);
}

void AiStateService::RelinkSeats(uint32_t aFormId) noexcept
{
    m_records.ForEach(RecordKind::kAiState, [&](uint32_t aSessionId, const std::string& acBytes) {
        const uint32_t cCopy = Utils::GetFormIdBySessionId(aSessionId);
        ActorAiState state;
        if (!cCopy || !m_actorBridge.IsActorCopy(cCopy) || !FromPartBytes(acBytes, state) || state.Furniture == AiTarget{})
            return;
        if (cCopy == aFormId || ToFormId(state.Furniture) == aFormId)
            m_actorBridge.SetCopyAiFields(cCopy, ToFields(state));
    });
}

void AiStateService::OnSessionActor(entt::registry&, entt::entity) noexcept
{
    m_newSession = true;
}

AiStateService::~AiStateService() noexcept
{
    m_records.Register(RecordKind::kAiState, {});
}

void AiStateService::OnUpdate(const UpdateEvent&) noexcept
{
    PROFILE_ZONE("AiStateService::OnUpdate");
    if (!m_transport.IsConnected()) return;
    // A look or fight at an actor with no session id yet read as nobody: owners read again (sent only if changed).
    if (std::exchange(m_newSession, false))
        OnAiStateChange(AiStateChangeEvent{0});
    // Scene stop (24140) sends its event before clearing the actors.
    const auto changes = SceneRefresh::ReadChanges(m_sent, [this](uint32_t aSessionId) -> std::optional<ActorAiState> {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
        if (!cFormId || !m_actorBridge.ActorExists(cFormId) || m_actorBridge.IsActorCopy(cFormId))
            return std::nullopt;
        return ReadState(cFormId);
    });
    for (const auto& change : changes)
        SendOwned(Utils::GetFormIdBySessionId(change.first), change.first);
}

void AiStateService::OnLocalRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    // A later ownership term publishes its state again, even when it matches this game's last term.
    m_sent.erase(aRegistry.get<LocalComponent>(aEntity).Id);
}

ActorAiState AiStateService::ReadState(uint32_t aFormId) const noexcept
{
    ActorAiState state = ToState(m_actorBridge.GetActorAiFields(aFormId));
    const CombatGroupRead cGroup = m_actorBridge.GetCombatGroup(aFormId);
    const auto cOwnPlayer = Utils::GetSessionIdByFormId(cPlayerFormId);
    if (!cGroup.Exists || !cOwnPlayer)
        return state;
    state.CombatGroup = (uint64_t{*cOwnPlayer} << 32) | cGroup.Id;
    // The session's actors: an NPC only this game holds is no target its copies' games could name.
    for (const uint32_t cTarget : cGroup.Targets)
        if (const auto cSessionId = Utils::GetSessionIdByFormId(cTarget))
            state.GroupTargets.push_back(*cSessionId);
    return state;
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

    m_records.Declare(aSessionId, RecordKind::kAiState, ToPartBytes(cState));
    spdlog::debug("AI state of {:X} (session {}): package {:X}:{:X}, scene {:X}:{:X}, combat {} on {}, group {:X} of {}",
                  aFormId, aSessionId, cState.Package.ModId, cState.Package.BaseId, cState.Scene.ModId, cState.Scene.BaseId,
                  cState.Combat, cState.CombatTarget.SessionId, cState.CombatGroup, cState.GroupTargets.size());
}

void AiStateService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_sent.clear();
}

std::vector<uint32_t> AiStateService::ApplyToCopy(uint32_t aSessionId, const std::string& acBytes) noexcept
{
    const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
    ActorAiState state;
    if (!cFormId || !m_actorBridge.IsActorCopy(cFormId) || !FromPartBytes(acBytes, state))
        return {};
    m_actorBridge.SetCopyAiFields(cFormId, ToFields(state));
    m_actorBridge.SetCopyCombat(cFormId, ToCombat(state));
    m_world.GetDispatcher().trigger(CopyAiStateEvent{aSessionId, state});
    return NotHere(state);
}

std::vector<uint32_t> AiStateService::NotHere(const ActorAiState& acState) noexcept
{
    std::vector<uint32_t> missing;
    const auto cCheck = [&](const AiTarget& acTarget) {
        if (acTarget.SessionId && !Utils::GetFormIdBySessionId(acTarget.SessionId))
            missing.push_back(acTarget.SessionId);
    };
    cCheck(acState.CombatTarget);
    for (const uint32_t cSessionId : acState.GroupTargets)
        cCheck(AiTarget{cSessionId});
    std::ranges::for_each(acState.HeadTrack, cCheck);
    return missing;
}

CopyCombatFields AiStateService::ToCombat(const ActorAiState& acState) noexcept
{
    CopyCombatFields fields;
    fields.State = acState.Combat;
    fields.TargetId = ToFormId(acState.CombatTarget);
    fields.GroupKey = acState.CombatGroup;
    for (const uint32_t cSessionId : acState.GroupTargets)
        if (const uint32_t cFormId = Utils::GetFormIdBySessionId(cSessionId))
            fields.GroupTargetIds.push_back(cFormId);
    return fields;
}

ActorAiState AiStateService::ToState(const ActorAiFields& acFields) const noexcept
{
    ActorAiState state;
    // A created form (an engine-made package) has no id the other games share: none.
    state.Package = ToSession(acFields.PackageId).value_or(GameId{});
    state.Scene = ToSession(acFields.SceneId).value_or(GameId{});
    state.Combat = acFields.CombatState;
    state.CombatTarget = ToTarget(acFields.CombatTargetId);
    std::ranges::transform(acFields.HeadTrackIds, state.HeadTrack.begin(), &AiStateService::ToTarget);
    state.Furniture = ToTarget(acFields.FurnitureId);
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
    fields.CombatTargetId = ToFormId(acState.CombatTarget);
    std::ranges::transform(acState.HeadTrack, fields.HeadTrackIds.begin(), &AiStateService::ToFormId);
    fields.FurnitureId = ToFormId(acState.Furniture);
    return fields;
}

AiTarget AiStateService::ToTarget(uint32_t aFormId) noexcept
{
    AiTarget target;
    if (!aFormId)
        return target;
    if (const auto cSessionId = Utils::GetSessionIdByFormId(aFormId))
        target.SessionId = *cSessionId;
    else if (IsCreatedForm(aFormId)) // a created reference by the id the others know it by (a host's furniture)
        target.RefId = World::Get().ctx().at<CreatedIds>().ToSession(aFormId).value_or(GameId{});
    else
        target.RefId = ToSession(aFormId).value_or(GameId{});
    return target;
}

uint32_t AiStateService::ToFormId(const AiTarget& acTarget) noexcept
{
    if (acTarget.SessionId)
        return Utils::GetFormIdBySessionId(acTarget.SessionId);
    if (IsCreatedId(acTarget.RefId))
        return World::Get().ctx().at<CreatedIds>().ToGame(acTarget.RefId);
    return acTarget.RefId != GameId{} ? ToGame(acTarget.RefId) : 0;
}
