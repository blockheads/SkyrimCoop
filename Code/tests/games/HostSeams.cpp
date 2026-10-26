#include <Events/GameLoadFailedEvent.h>
#include <Events/GameLoadedEvent.h>
// The host's side in this process: HostService hosting as in Skyrim (the network, HostAuthority, the host's own
// game joining in process), its reads from the fake engine's world and the real CharacterService; and the systems
// and services CharacterService calls that tier 2 leaves out (animation, face tints, AI state, effects).
#include "FakeEngine.h"

#include <Bridges/HostEngine.h>
#include <Bridges/IActorBridge.h>
#include <Messages/ReferencesMoveRequest.h>
#include <FormIds.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/GameSavingEvent.h>
#include <Bridges/IJoinBridge.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Events/UpdateEvent.h>
#include <RunningGame.h>
#include <Services/AiStateService.h>
#include <Services/CharacterService.h>
#include <Services/DialogueService.h>
#include <Services/EffectVisualsService.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostConversation.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <Services/BodyService.h>
#include <Systems/AnimationSystem.h>
#include <Systems/FaceGenSystem.h>
#include <Components.h>
#include <World.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    Listen<UpdateEvent, &HostService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<DifficultyChangeEvent, &HostService::OnDifficultyChange>(m_connections, m_dispatcher, this);
    Listen<GameLoadFailedEvent, &HostService::OnGameLoadFailed>(m_connections, m_dispatcher, this);
    Listen<GameLoadedEvent, &HostService::OnGameLoaded>(m_connections, m_dispatcher, this);
    Listen<GameSavingEvent, &HostService::OnGameSaving>(m_connections, m_dispatcher, this);
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

    MakeAuthority();
    return true;
}

void HostService::MakeAuthority() noexcept
{
    FakeEngine& engine = m_world.ctx().at<FakeEngine&>();
    HostGame game;
    game.OwnSpawns = [this](uint32_t aExcludedSessionId) {
        return m_world.GetCharacterService().BuildSpawnRequests(aExcludedSessionId);
    };
    game.ReadCopy = [this](uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) {
        return m_world.GetCharacterService().ReadCopy(aSessionId, aSpawn);
    };
    game.ReadTime = [&engine] { return engine.Calendar; };
    game.SetHour = [&engine](float aHours) { engine.Calendar.Time = aHours; };
    game.ReadWeather = [&engine] {
        return engine.Weather.BaseId ? std::optional<GameId>(engine.Weather) : std::optional<GameId>{};
    };
    game.ReadGlobals = [&engine] {
        std::vector<HostGlobal> globals;
        for (const auto& [cId, cValue] : engine.Globals)
            globals.push_back({.Id = GameId{0, cId}, .Value = cValue});
        return globals;
    };
    game.TakeGlobalChanges = [&engine] {
        std::vector<HostGlobal> globals;
        for (const uint32_t cId : std::exchange(engine.GlobalWrites, {}))
            globals.push_back({.Id = GameId{0, cId}, .Value = engine.Globals.at(cId)});
        return globals;
    };
    game.ReadQuests = [&engine] {
        std::vector<QuestState> quests;
        for (const auto& [cId, cQuest] : engine.Quests)
            quests.push_back(cQuest);
        return quests;
    };
    game.TakeQuestChanges = [&engine] {
        std::vector<QuestState> quests;
        for (const uint32_t cId : engine.QuestWrites.Take())
            if (const auto it = engine.Quests.find(cId); it != engine.Quests.end())
                quests.push_back(it->second);
        return quests;
    };
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
    // An actor's ragdoll goes by its session id, as HostEngine::TakeBodies keys it; only this game's own actors' (a
    // copy's is keyed by session id here, and played).
    game.TakeBodies = [&engine, this] {
        const auto cSessionOf = [this](uint32_t aFormId) -> uint32_t {
            for (const auto cEntity : m_world.view<LocalComponent, FormIdComponent>())
                if (m_world.get<FormIdComponent>(cEntity).Id == aFormId)
                    return m_world.get<LocalComponent>(cEntity).Id;
            return 0;
        };
        HostBodies bodies = engine.TakeBodies(m_pNetwork->GetTick(), [&](const BodyKey& acKey) { return !acKey.Actor || cSessionOf(acKey.Actor); });
        for (auto& frame : bodies.Awake)
            if (frame.Key.Actor)
                frame.Key.Actor = cSessionOf(frame.Key.Actor);
        for (auto& rest : bodies.Rested)
            if (rest.Key.Actor)
                rest.Key.Actor = cSessionOf(rest.Key.Actor);
        return bodies;
    };
    game.PushBody = [&engine](const BodyPush& acPush) { return engine.Push(acPush); };
    game.FreeBody = [&engine, this](const BodyFrame& acFrame) {
        m_world.ctx().at<BodyService>().Forget(acFrame.Key);
        return engine.Free(acFrame);
    };
    game.ReadBodies = [&engine](const std::vector<BodyKey>& acKeys) {
        std::vector<BodyPose> poses;
        for (const BodyKey& cKey : acKeys)
            if (const auto it = engine.Bodies.find(cKey); it != engine.Bodies.end())
                poses.push_back({cKey, it->second.Flight.Body.Position, it->second.Flight.Body.Rotation});
        return poses;
    };
    game.ReadDifficulty = [&engine] { return engine.GetDifficulty(); };
    game.StoreRecord = [this](const PlayerRecord& acRecord) { m_world.ctx().at<SaveService>().GetRecords().Put(acRecord); };
    game.HasRecord = [this](const std::string& acKey) { return m_world.ctx().at<SaveService>().GetRecords().Find(acKey) != nullptr; };
    game.SaveWorld = [this]() -> std::optional<std::vector<NotifySaveChunk>> {
        auto files = m_world.ctx().at<IJoinBridge&>().SaveNow(JoinService::kTransferSave);
        if (!files)
            return std::nullopt;
        return SaveTransfer::Split(SaveTransfer::Pack(*files));
    };
    game.StorePlace = [this](const std::string& acKey, const GameId& acWorldSpace, const GameId& acCell,
                             const std::array<float, 3>& acPosition) {
        m_world.ctx().at<SaveService>().GetRecords().SetPlace(acKey, acWorldSpace, acCell, acPosition);
    };
    game.PartyWipe = [this] { m_reloadRequested = true; };
    game.Versions = RunningGame::GetVersions();
    game.Plugins = RunningGame::Plugins(engine);
    m_pAuthority = std::make_unique<HostAuthority>(*m_pNetwork, std::move(game));
    m_pConversation = std::make_unique<HostConversation>(m_world, m_dispatcher, m_world.ctx().at<DialogueService>());
    InstallFrontDoor();
}

void HostService::StopHosting() noexcept
{
    m_reloadRequested = m_readCheckpoint = m_loadingPrevious = false;
    m_checkpoint.reset();
    m_reloadJoins.clear();
    if (!IsHosting())
        return;
    // As the real one: our own in-process connection goes through it.
    if (m_world.GetTransport().IsConnectedLocally())
        m_world.GetTransport().Close();
    m_pConversation.reset();
    m_pNetwork.reset();
    m_pAuthority.reset();
}

void HostService::KickPlayer(uint32_t aPlayerId) noexcept
{
    if (m_pAuthority)
        m_pAuthority->Kick(aPlayerId);
}

uint32_t HostService::NewCharacterId() noexcept
{
    return m_pAuthority ? m_pAuthority->NewCharacterId() : 0;
}

void HostService::OnDifficultyChange(const DifficultyChangeEvent& acEvent) const noexcept
{
    if (IsHosting())
        m_pAuthority->DifficultyChanged(acEvent.Difficulty);
}

void HostService::OnGameSaving(const GameSavingEvent&) const noexcept
{
    if (IsHosting())
        m_pAuthority->Saving();
}

void HostService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!IsHosting())
        return;
    m_pNetwork->Update();
    m_pAuthority->Update(m_pNetwork->GetTick(), acEvent.Delta);
    UpdateReload();
}

// Never made here (the fake engine is the host's reads), but HostService's members name them.
HostEngine::~HostEngine() noexcept = default;
PhysicsBodies::~PhysicsBodies() noexcept = default;
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
        movement.CellId = ToSession(cCell).value_or(GameId{});
    if (const uint32_t cWorldSpace = cActors.GetActorWorldSpaceFormId(formIdComponent.Id))
        movement.WorldSpaceId = ToSession(cWorldSpace).value_or(GameId{});
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

// CombatService's engine upkeep (Actor.cpp): no remote-player anger or hang watch in the fake.
void UpdateRemotePlayerAnger() noexcept
{
}

void RefreshHangLockWatch() noexcept
{
}

void FaceGenSystem::Setup(World& aWorld, entt::entity aEntity, const Tints& acTints) noexcept
{
    if (!acTints.Entries.empty())
        aWorld.emplace_or_replace<FaceGenComponent>(aEntity).FaceTints = acTints;
}

