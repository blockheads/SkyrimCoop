// The host's side in this process: HostService hosting as in Skyrim (the network, HostAuthority, the host's own
// game joining in process), its reads from the fake engine's world and the real CharacterService; and the systems
// and services CharacterService calls that tier 2 leaves out (animation, face tints, AI state, effects).
#include "FakeEngine.h"

#include <Bridges/HostEngine.h>
#include <Bridges/IActorBridge.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Systems/ModSystem.h>
#include <Events/UpdateEvent.h>
#include <RunningGame.h>
#include <Services/AiStateService.h>
#include <Services/CharacterService.h>
#include <Services/EffectVisualsService.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostConversation.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <Systems/AnimationSystem.h>
#include <Systems/FaceGenSystem.h>
#include <Components.h>
#include <World.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&HostService::OnUpdate>(this);
}

HostService::~HostService() noexcept
{
    StopHosting();
}

// As HostService::StartHosting, the host's game read from the fake engine (HostEngine's part) and the real
// CharacterService (its actors' spawns, its copies).
bool HostService::StartHosting(uint16_t aPort) noexcept
{
    if (IsHosting())
        return true;
    auto pNetwork = std::make_unique<HostNetwork>();
    if (!pNetwork->Listen(aPort))
        return false;
    m_pNetwork = std::move(pNetwork);

    FakeEngine& engine = m_world.ctx().at<FakeEngine&>();
    HostGame game;
    game.OwnSpawns = [this](uint32_t aExcludedSessionId) {
        return m_world.GetCharacterService().BuildSpawnRequests(aExcludedSessionId);
    };
    game.ReadCopy = [this](uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) {
        return m_world.GetCharacterService().ReadCopy(aSessionId, aSpawn);
    };
    game.ReadTime = [] { return TimeModel{}; };
    game.SetHour = [](float) {};
    game.ReadWeather = [] { return std::optional<GameId>{}; };
    game.ReadGlobals = [] { return std::vector<HostGlobal>{}; };
    game.ReadQuests = [] { return std::vector<QuestState>{}; };
    game.TakeObjectChanges = [&engine] { return std::exchange(engine.Recorded, {}); };
    game.ReadCellState = [&engine](const GameId& acCell, const std::vector<ChangedReference>& acChanged) {
        return engine.CellState(acCell, acChanged);
    };
    game.SetLock = [&engine](const GameId& acObject, bool aLocked, uint8_t aLevel) {
        engine.SetLock(FormIdOf(acObject), aLocked, aLevel);
    };
    game.SetItems = [&engine](const GameId& acObject, const GameId& acItem, uint32_t aCount) {
        engine.SetItems(FormIdOf(acObject), FormIdOf(acItem), static_cast<int32_t>(aCount));
    };
    game.ReadDifficulty = [] { return 3u; };
    game.Versions = RunningGame::GetVersions();
    game.Plugins = RunningGame::Plugins(engine);
    m_pAuthority = std::make_unique<HostAuthority>(*m_pNetwork, std::move(game));
    return true;
}

void HostService::StopHosting() noexcept
{
    if (!IsHosting())
        return;
    m_pNetwork.reset();
    m_pAuthority.reset();
}

void HostService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!IsHosting())
        return;
    m_pNetwork->Update();
    m_pAuthority->Update(m_pNetwork->GetTick());
}

// Never made here (the fake engine is the host's reads, and no conversation), but HostService's members name them.
HostEngine::~HostEngine() noexcept = default;
PhysicsRests::~PhysicsRests() noexcept = default;
HostConversation::~HostConversation() noexcept = default;
bool HostConversation::OnStartAsked(uint32_t, bool) noexcept
{
    return false;
}
bool HostConversation::OnTalk(uint32_t) noexcept
{
    return false;
}
uint32_t HostConversation::LineFor(uint32_t) noexcept
{
    return 0;
}
std::optional<std::vector<IDialogueBridge::Option>> HostConversation::OnListBuilt() noexcept
{
    return std::nullopt;
}
void HostConversation::OnOpen(const NotifyDialogueOpen&) noexcept
{
}
void HostConversation::OnPicked(const NotifyDialoguePicked&, uint32_t, uint32_t, bool) noexcept
{
}
void HostConversation::OnClosed() noexcept
{
}
void HostConversation::OnUpdate(bool, uint32_t) noexcept
{
}

// AnimationSystem and FaceGenSystem as their own (their files read the engine too): the components only.
void AnimationSystem::Setup(World& aWorld, entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

// As the real one through the actor bridge: where the actor is and its actions; no graph variables or direction.
void AnimationSystem::Serialize(World& aWorld, ReferencesMoveRequest& aMovementSnapshot, LocalComponent& localComponent,
                                LocalAnimationComponent& animationComponent, FormIdComponent& formIdComponent)
{
    const auto& cActors = aWorld.ctx().at<IActorBridge&>();
    if (!cActors.ActorExists(formIdComponent.Id))
        return;
    auto& update = aMovementSnapshot.Updates[localComponent.Id];
    auto& movement = update.UpdatedMovement;
    if (const uint32_t cCell = cActors.GetActorParentCellFormId(formIdComponent.Id))
        aWorld.GetModSystem().GetServerModId(cCell, movement.CellId.ModId, movement.CellId.BaseId);
    if (const uint32_t cWorldSpace = cActors.GetActorWorldSpaceFormId(formIdComponent.Id))
        aWorld.GetModSystem().GetServerModId(cWorldSpace, movement.WorldSpaceId.ModId, movement.WorldSpaceId.BaseId);
    float x = 0.f, y = 0.f, z = 0.f;
    cActors.GetActorPosition(formIdComponent.Id, x, y, z);
    movement.Position = {x, y, z};
    cActors.GetActorRotation(formIdComponent.Id, x, y, z);
    movement.Rotation.x = x;
    movement.Rotation.y = z;
    for (auto& entry : animationComponent.Actions)
        update.ActionEvents.push_back(entry);
    if (auto latestAction = animationComponent.GetLatestAction())
        localComponent.CurrentAction = std::move(*latestAction);
    animationComponent.Actions.clear();
}

void AnimationSystem::TargetToNetwork(ActionEvent&) noexcept
{
}

void FaceGenSystem::Setup(World& aWorld, entt::entity aEntity, const Tints& acTints) noexcept
{
    if (!acTints.Entries.empty())
        aWorld.emplace_or_replace<FaceGenComponent>(aEntity).FaceTints = acTints;
}

// Left out of tier 2: their state is not in the fake engine.

ActorAiState AiStateService::ReadState(uint32_t) const noexcept
{
    return {};
}

std::vector<GameId> EffectVisualsService::ReadEffects(uint32_t) const noexcept
{
    return {};
}
