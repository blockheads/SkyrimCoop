#include <Events/GameLoadFailedEvent.h>
#include <Events/GameLoadedEvent.h>
// The host's side in this process: HostService hosting as in Skyrim (the network, HostAuthority, the host's own
// game joining in process), its reads from the fake engine's world and the real CharacterService; and the systems
// and services CharacterService calls that tier 2 leaves out (animation, face tints, AI state, effects).
#include "FakeEngine.h"
#include <Messages/ContainerTransferRequest.h>
#include <Games/Overrides.h>

#include <Bridges/HostEngine.h>
#include <Bridges/IActorBridge.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/Relayed.h>
#include <ScriptValues.h>
#include <FormIds.h>
#include <SessionIds.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/GameSavingEvent.h>
#include <SessionIds.h>
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
#include <Services/CrimeService.h>
#include <Services/InventoryService.h>
#include <Bridges/IPlayerBridge.h>
#include <Systems/AnimationSystem.h>
#include <Systems/FaceGenSystem.h>
#include <Components.h>
#include <World.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    Listen<UpdateEvent, &HostService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<DifficultyChangeEvent, &HostService::OnDifficultyChange>(m_connections, m_dispatcher, this);
    Listen<NotifyRoutedCall, &HostService::OnWorldCall>(m_connections, m_dispatcher, this);
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
    game.ReadFollowers = [&engine]() -> std::optional<std::vector<Followers::State>> {
        auto followers = engine.PartyFollowers;
        for (auto& follower : followers)
        {
            const auto it = engine.Actors.find(follower.Reference);
            if (it == engine.Actors.end())
                continue;
            follower.Session = Utils::GetSessionIdByFormId(follower.Reference).value_or(0);
            follower.At = {it->second.WorldSpace, it->second.Cell, {it->second.Position[0], it->second.Position[1], it->second.Position[2]}};
            follower.Waiting = engine.GetActorValue(follower.Reference, 95) != 0.f;
            follower.Dead = engine.IsActorDead(follower.Reference);
        }
        return followers;
    };
    game.TakeFollowerOrders = [&engine] { return std::exchange(engine.FollowerOrders, {}); };
    game.SetFollowerTargets = [&engine](const auto& acTargets) { engine.FollowerTargets = acTargets; };
    game.FollowerAnchors = [this]() -> Followers::Anchors& { return m_world.ctx().at<SaveService>().GetFollowerAnchors(); };
    game.TakeCrimeConfiscations = [this] { return m_world.ctx().at<CrimeService>().TakeConfiscations(); };
    game.ReadCrimeGold = [this] { return m_world.ctx().at<CrimeService>().ReadGold(); };
    game.ReadPartyGold = [this] { return m_world.ctx().at<InventoryService>().GoldOf(m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId()); };
    game.AddPartyGold = [this](int32_t aCount) { m_world.ctx().at<InventoryService>().AddGold(m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId(), aCount); };
    game.JudgeCrime = [this](uint32_t aActor, const CrimeRequest& aCrime) { return m_world.ctx().at<CrimeService>().Judge(aActor, aCrime); };
    game.OwnSpawns = [this](uint32_t aExcludedSessionId) {
        return m_world.GetCharacterService().BuildSpawnRequests(aExcludedSessionId);
    };
    game.ReadCopy = [this](uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) {
        return m_world.GetCharacterService().ReadCopy(aSessionId, aSpawn);
    };
    game.AdvanceTime = [&engine](float aHours) {
        MainLoopTasks::Queue([&engine, aHours] {
            DateTime time(engine.Calendar);
            const float cScale = time.m_timeModel.TimeScale;
            time.m_timeModel.TimeScale = 3600.f;
            time.Update(static_cast<uint64_t>(aHours * 1000.f));
            time.m_timeModel.TimeScale = cScale;
            engine.Calendar = time.m_timeModel;
        });
    };
    game.ReadTime = [&engine] { return engine.Calendar; };
    game.SetHour = [&engine](float aHours) { engine.Calendar.Time = aHours; };
    game.ReadWeather = [&engine] {
        return engine.Weather.BaseId ? std::optional<GameId>(engine.Weather) : std::optional<GameId>{};
    };
    game.ReadGlobals = [&engine] {
        std::vector<HostGlobal> globals;
        for (const auto& [cId, cValue] : engine.Globals)
            if (!engine.SystemQuestList.IsGlobal(cId)) // as HostEngine: each player's own
                globals.push_back({.Id = GameId{0, cId}, .Value = cValue});
        return globals;
    };
    game.TakeGlobalChanges = [&engine] {
        std::vector<HostGlobal> globals;
        for (const uint32_t cId : std::exchange(engine.GlobalWrites, {}))
            if (!engine.SystemQuestList.IsGlobal(cId))
                globals.push_back({.Id = GameId{0, cId}, .Value = engine.Globals.at(cId)});
        return globals;
    };
    game.ReadQuests = [&engine] {
        std::vector<QuestState> quests;
        for (const auto& [cId, cQuest] : engine.Quests)
            if (!engine.SystemQuestList.Contains(cId))
                quests.push_back(cQuest);
        return quests;
    };
    game.TakeQuestChanges = [&engine] {
        std::vector<QuestState> quests;
        for (const uint32_t cId : engine.QuestWrites.Take())
            if (const auto it = engine.Quests.find(cId); it != engine.Quests.end() && !engine.SystemQuestList.Contains(cId))
                quests.push_back(it->second);
        return quests;
    };
    game.TakeObjectChanges = [&engine] { return std::exchange(engine.Recorded, {}); };
    game.ReadCellState = [&engine](const GameId& acCell, const std::vector<ChangedReference>& acChanged) {
        return engine.CellState(acCell, acChanged);
    };
    game.RaiseStoryEvent = [&engine](const StoryEvent& acEvent) { engine.StoriesRaised.push_back(acEvent); };
    game.SetLock = [&engine](const GameId& acObject, bool aLocked, uint8_t aLevel) {
        engine.SetLock(FormIdOf(acObject), aLocked, aLevel);
    };
    game.SetItems = [&engine](const ObjectChange& acItems) {
        engine.SetItems(FormIdOf(acItems.Object), FormIdOf(acItems.Item), static_cast<int32_t>(acItems.Value), acItems.Stacks);
    };
    game.TransferContainer = [&engine](const ContainerTransferRequest& acRequest, uint32_t aPlayer) {
        Inventory::Entry moved = acRequest.Item;
        const uint32_t cPlayer = Utils::GetFormIdBySessionId(aPlayer);
        const uint32_t cContainer = acRequest.ContainerActorId ? Utils::GetFormIdBySessionId(acRequest.ContainerActorId) : FormIdOf(acRequest.Container);
        const uint32_t cItem = FormIdOf(moved.BaseId);
        const bool cTake = moved.Count > 0;
        const int32_t cAvailable = cTake ? (acRequest.ContainerActorId ? engine.CountOf(cContainer, cItem) : engine.References.at(cContainer).Items[cItem]) : engine.CountOf(cPlayer, cItem);
        moved.Count = std::min(std::abs(moved.Count), cAvailable) * (cTake ? 1 : -1);
        if (moved.Count)
        {
            ScopedInventoryOverride _;
            engine.GetsItem(cPlayer, moved);
            Inventory::Entry other = moved;
            other.Count = -other.Count;
            if (acRequest.ContainerActorId)
                engine.GetsItem(cContainer, other);
            else
                engine.SetItems(cContainer, cItem, engine.References.at(cContainer).Items[cItem] - moved.Count);
        }
        return moved;
    };
    game.CreateItem = [&engine](const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) {
        return engine.CreateLoose(acItem, FormIdOf(acPlacement.Cell));
    };
    game.TakeItem = [&engine](const GameId& acObject) { return engine.TakeLoose(acObject); };
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
    game.FreeBody = [&engine, this](const BodyFrame& acFrame, uint64_t aReleasedAt) {
        m_world.ctx().at<BodyService>().Forget(acFrame.Key, aReleasedAt);
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
    game.StoreStorySpell = [this](const GameId& acSpell, bool aAdded) {
        m_world.ctx().at<SaveService>().GetRecords().SetStorySpell(acSpell, aAdded);
    };
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
    m_reloadRequested = m_readCheckpoint = m_loadingSave = false;
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

// As HostService::OnWorldCall: a guest's script's call made here for it (GlobalCall::QueueCall in a stack run for it).
void HostService::OnWorldCall(const NotifyRoutedCall& acMessage) noexcept
{
    if (acMessage.To != CallTo::kHost || !IsHosting())
        return;
    const uint32_t cGuest = Utils::GetFormIdBySessionId(acMessage.SessionId);
    if (const auto cCall = Utils::ToLocalCall(acMessage.Call); cGuest && cCall)
        m_world.ctx().at<FakeEngine&>().Queue(*cCall, cGuest);
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
