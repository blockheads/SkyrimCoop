#include <join/GameLoadFailedEvent.h>
#include <scripts/HostSight.h>
#include <characters/ActorRenamedEvent.h>
#include <join/GameLoadedEvent.h>
#include <game/HostService.h>
#include <items/PartyItems.h>
#include <combat/HostAggro.h>
#include <combat/HostTraps.h>
#include <combat/HostTakeBacks.h>
#include <physics/IBodyBridge.h>
#include <physics/BodyService.h>
#include <crime/CrimeService.h>
#include <dialogue/DialogueService.h>
#include <items/InventoryService.h>
#include <dialogue/HostConversation.h>

#include <world/ActivateEvent.h>
#include <items/ContainerChangeEvent.h>
#include <core/DifficultyChangeEvent.h>
#include <join/GameSavingEvent.h>
#include <join/SaveWrittenEvent.h>
#include <join/SaveLibrary.h>
#include <ports/IActorBridge.h>
#include <join/IJoinBridge.h>
#include <join/JoinService.h>
#include <join/SaveService.h>
#include <animation/ScriptAnimationEvent.h>
#include <core/SessionIds.h>
#include <core/UpdateEvent.h>
#include <core/World.h>
#include <core/TransportService.h>
#include <game/HostAuthority.h>
#include <core/HostNetwork.h>
#include <characters/CharacterService.h>
#include <ports/IHostEngine.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>
#include <core/PlayerComponent.h>
#include <ports/IPlayerBridge.h>
#include <ports/IWorldBridge.h>
#include <tools/IDevToolsBridge.h>
#include <tools/DevCommands.h>
#include <core/RunningGame.h>
#include <Structs/GameVersions.h>
#include <Messages/Relayed.h>
#include <Messages/SummonRequest.h>
#include <core/CreatedIds.h>
#include <core/ScriptValues.h>
#include <scripts/PlayerNatives.h>
#include <core/Profile.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher, MakeEngine_t aMakeEngine) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher), m_makeEngine(std::move(aMakeEngine))
{
    Listen<UpdateEvent, &HostService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<ActivateEvent, &HostService::OnActivate>(m_connections, m_dispatcher, this);
    Listen<ScriptAnimationEvent, &HostService::OnScriptAnimation>(m_connections, m_dispatcher, this);
    Listen<ContainerChangeEvent, &HostService::OnContainerChange>(m_connections, m_dispatcher, this);
    Listen<DifficultyChangeEvent, &HostService::OnDifficultyChange>(m_connections, m_dispatcher, this);
    Listen<GameLoadFailedEvent, &HostService::OnGameLoadFailed>(m_connections, m_dispatcher, this);
    Listen<GameLoadedEvent, &HostService::OnGameLoaded>(m_connections, m_dispatcher, this);
    Listen<GameSavingEvent, &HostService::OnGameSaving>(m_connections, m_dispatcher, this);
    Listen<SaveWrittenEvent, &HostService::OnSaveWritten>(m_connections, m_dispatcher, this);
    Listen<ActorRenamedEvent, &HostService::OnActorRenamed>(m_connections, m_dispatcher, this);
    Listen<NotifyPlayerNativeResult, &HostService::OnPlayerNativeResult>(m_connections, m_dispatcher, this);
    Listen<NotifyRoutedCall, &HostService::OnWorldCall>(m_connections, m_dispatcher, this);
}

void HostService::OnActivate(const ActivateEvent& acEvent) noexcept
{
    if (IsHosting() && acEvent.ByReference)
        m_pEngine->RecordActivation(acEvent.ObjectFormId, acEvent.ActivatorFormId, acEvent.PreActivationOpenStateValue);
    // The host's player's pull of a lever furniture moves only its own graph: the guests' follow from the row.
    if (IsHosting() && !acEvent.FurnitureEvent.empty())
        m_pEngine->RecordAnimation(acEvent.ObjectFormId, {}, acEvent.FurnitureEvent, acEvent.ActivatorFormId);
}

void HostService::OnScriptAnimation(const ScriptAnimationEvent& acEvent) noexcept
{
    if (IsHosting())
        m_pEngine->RecordAnimation(acEvent.FormID, acEvent.Animation, acEvent.EventName, 0);
}

void HostService::OnContainerChange(const ContainerChangeEvent& acEvent) noexcept
{
    // Actors' items are their owner's (InventoryService); the engine keeps containers' only.
    if (IsHosting())
        m_pEngine->RecordItems(acEvent.ContainerId, acEvent.ItemId);
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

void HostService::OnPlayerNativeResult(const NotifyPlayerNativeResult& acMessage) noexcept
{
    if (!IsHosting())
        return;
    if (!HostSight::IsSightReply(acMessage.ReplyId))
        spdlog::info("HostService: the guest's game answered stack {}", acMessage.ReplyId);
    PlayerNatives::Answer(acMessage.ReplyId, Utils::ToLocalValue(acMessage.Result).value_or(std::monostate{}));
}

void HostService::OnWorldCall(const NotifyRoutedCall& acMessage) noexcept
{
    if (acMessage.To != CallTo::kHost)
        return;
    const uint32_t cGuest = Utils::GetFormIdBySessionId(acMessage.SessionId);
    const auto cCall = Utils::ToLocalCall(acMessage.Call);
    if (!IsHosting() || !cGuest || !cCall)
    {
        spdlog::warn("HostService: {}.{} from a guest's script names a form or character not here", acMessage.Call.Class, acMessage.Call.Function);
        if (IsHosting() && acMessage.ReplyId)
            QueueWorldAnswer(acMessage.ReplyId, acMessage.SessionId, std::monostate{}); // its waiting script gets None
        return;
    }
    spdlog::info("HostService: {}.{} from guest {:X}'s own script runs here, for it", acMessage.Call.Class, acMessage.Call.Function, cGuest);
    // Its script waits (a created reference): what the call returns goes back to it, on the VM's thread.
    std::function<void(LocalValue)> answer;
    if (acMessage.ReplyId)
        answer = [this, cReplyId = acMessage.ReplyId, cSessionId = acMessage.SessionId](LocalValue aResult) {
            QueueWorldAnswer(cReplyId, cSessionId, std::move(aResult));
        };
    if (!m_pEngine->RunFor(cGuest, *cCall, answer) && answer)
        answer(std::monostate{});
}

HostService::~HostService() noexcept
{
    StopHosting();
}

bool HostService::StartHosting(uint16_t aPort) noexcept
{
    if (IsHosting())
        return true;

    auto pNetwork = std::make_unique<HostNetwork>();
    if (!pNetwork->Listen(aPort))
    {
        spdlog::error("HostService: no port to listen on from {}", aPort);
        return false;
    }
    m_pNetwork = std::move(pNetwork);
    m_world.SetHosting(true);

    MakeAuthority();
    spdlog::info("HostService: hosting on port {}", m_pNetwork->GetPort());
    return true;
}

void HostService::MakeAuthority() noexcept
{
    m_pAggro.reset();
    m_pEngine = m_makeEngine();
    m_world.ctx().at<CreatedIds>().SetHosting(true);
    m_pAggro = std::make_unique<HostAggro>(m_world, m_dispatcher, m_world.ctx().at<IActorBridge&>(),
        [this](const auto& acState) { m_pEngine->SetPartyThreats(acState); });
    m_pTraps = std::make_unique<HostTraps>(m_world, m_dispatcher);
    m_pTakeBacks = std::make_unique<HostTakeBacks>(m_world, m_dispatcher, m_world.ctx().at<IActorBridge&>(), [this](uint32_t aId) {
        if (m_pAuthority)
            m_pAuthority->ActorTaken(aId);
    });
    HostGame game;
    game.ReadFollowers = [this] { return m_pEngine->ReadFollowers(); };
    game.TakeFollowerOrders = [this] { return m_pEngine->TakeFollowerOrders(); };
    game.SetFollowerTargets = [this](const auto& acTargets) { m_pEngine->SetFollowerTargets(acTargets); };
    game.FollowerAnchors = [this]() -> Followers::Anchors& { return m_world.ctx().at<SaveService>().GetFollowerAnchors(); };
    game.TakeCrimeConfiscations = [this] { return m_world.ctx().at<CrimeService>().TakeConfiscations(); };
    game.ReadCrimeGold = [this] { return m_world.ctx().at<CrimeService>().ReadGold(); };
    game.TakeCrimeReports = [this] { return m_world.ctx().at<CrimeService>().TakeReports(); };
    game.ReadPartyGold = [this] { return m_world.ctx().at<InventoryService>().GoldOf(m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId()); };
    game.AddPartyGold = [this](int32_t aCount) { m_world.ctx().at<InventoryService>().AddGold(m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId(), aCount); };
    game.ReadPartyItem = [this](const Inventory::Entry& acItem) {
        return m_world.ctx().at<InventoryService>().PartyItemCount(m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId(), acItem);
    };
    game.AddPartyItem = [this](const Inventory::Entry& acItem) {
        m_world.ctx().at<InventoryService>().AddPartyItem(m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId(), acItem);
    };
    game.ReadPartyItems = [this] {
        return m_world.ctx().at<InventoryService>().PartyItemsOf(m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId());
    };
    game.ItemName = [this](const GameId& acItem) { return m_world.ctx().at<InventoryService>().ItemName(acItem); };
    game.ItemFormType = [this](const GameId& acItem) { return m_world.ctx().at<InventoryService>().ItemFormType(acItem); };
    game.JudgeCrime = [this](uint32_t aActor, const CrimeRequest& aCrime) { return m_world.ctx().at<CrimeService>().Judge(aActor, aCrime); };
    game.OwnSpawns = [this](uint32_t aExcludedSessionId) { return m_world.GetCharacterService().BuildSpawnRequests(aExcludedSessionId); };
    game.ReadCopy = [this](uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) { return m_world.GetCharacterService().ReadCopy(aSessionId, aSpawn); };
    game.CopySpawn = [this](uint32_t aSessionId) { return m_world.GetCharacterService().BuildCopySpawn(aSessionId); };
    game.Claimable = [this](uint32_t aSessionId) {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
        return cFormId && !m_world.ctx().at<IActorBridge&>().IsPlayerSummon(cFormId);
    };
    game.CanPair = [this](uint32_t aSessionId) {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
        return cFormId && m_world.ctx().at<IActorBridge&>().ActorCanPair(cFormId);
    };
    game.IsDead = [this](uint32_t aSessionId) {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
        return cFormId && m_world.ctx().at<IActorBridge&>().IsActorDead(cFormId);
    };
    game.RunConsole = [this](const std::string& acLine) { m_world.ctx().at<IDevToolsBridge&>().RunConsole(acLine); };
    game.KillHostiles = [this] { return m_world.ctx().at<IDevToolsBridge&>().KillHostiles(); };
    game.SpawnPartyHorses = [this](uint32_t aSessionId, uint32_t aCount) {
        const uint32_t cFormId = Utils::GetFormIdBySessionId(aSessionId);
        return cFormId ? m_world.ctx().at<IDevToolsBridge&>().SpawnPartyHorses(cFormId, aCount) : 0u;
    };
    game.ConsoleNameOf = [this](uint32_t aSessionId) {
        return DevCommands::ConsoleName(Utils::GetFormIdBySessionId(aSessionId), m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId());
    };
    game.ReadTime = [this] { return m_world.ctx().at<IWorldBridge&>().GetGameTime(); };
    game.SetHour = [this](float aHours) { m_pEngine->SetHour(aHours); };
    game.AdvanceTime = [this](float aHours) { m_pEngine->AdvanceTime(aHours); };
    game.ReadWeather = [this] { return m_pEngine->ReadWeather(); };
    game.RegionWeather = [this](const GameId& acRegion) { return m_pEngine->RegionWeather(acRegion); };
    game.ReadEye = [this](uint32_t aSessionId) { return m_pEngine->ReadEye(aSessionId); };
    game.ReadGlobals = [this] { return m_pEngine->ReadGlobals(); };
    game.TakeGlobalChanges = [this] { return m_pEngine->TakeGlobalChanges(); };
    game.ReadQuests = [this] { return m_pEngine->ReadQuests(); };
    game.TakeQuestChanges = [this] { return m_pEngine->TakeQuestChanges(); };
    game.TakeObjectChanges = [this] { return m_pEngine->TakeObjectChanges(); };
    game.TakePlaced = [this] { return m_pEngine->TakePlaced(); };
    game.TakeBodies = [this](uint64_t aTick) {
        HostBodyReads bodies = m_pEngine->TakeBodies(aTick, [this](uint32_t aFormId) -> uint32_t {
            if (const auto cEntity = m_world.GetActorIndex().ByForm<LocalComponent>(aFormId); cEntity != entt::null)
                return m_world.get<LocalComponent>(cEntity).Id;
            // A copy's too: its corpse the host's player may drag (HostBodies takes it only then).
            const auto cCopy = m_world.GetActorIndex().ByForm<RemoteComponent>(aFormId);
            return cCopy == entt::null ? 0 : m_world.get<RemoteComponent>(cCopy).Id;
        });
        // Its actors' ragdoll rests are their record parts; the references' and a dragged copy's go to HostBodies.
        std::vector<BodyPose> copies;
        std::erase_if(bodies.Rested, [&](const BodyPose& acRest) {
            if (!acRest.Key.Actor || m_world.GetActorIndex().BySession<RemoteComponent>(acRest.Key.Actor) == entt::null)
                return false;
            copies.push_back(acRest);
            return true;
        });
        m_world.ctx().at<BodyService>().DeclareRagdolls(aTick, bodies.Rested, bodies.Awake);
        bodies.Rested.insert(bodies.Rested.end(), copies.begin(), copies.end());
        return bodies;
    };
    // A guest's copy, where the host's game shows it (PlayerComponent: its player id).
    game.ReadPlayerPosition = [this](uint32_t aPlayerId) -> std::optional<glm::vec3> {
        auto& actors = m_world.GetActorBridge();
        for (const auto cEntity : m_world.view<PlayerComponent, FormIdComponent>())
        {
            const uint32_t cFormId = m_world.get<FormIdComponent>(cEntity).Id;
            if (m_world.get<PlayerComponent>(cEntity).Id != aPlayerId || !actors.ActorHas3D(cFormId))
                continue;
            glm::vec3 position{};
            actors.GetActorPosition(cFormId, position.x, position.y, position.z);
            return position;
        }
        return std::nullopt;
    };
    game.TakeTouches = [this] { return m_pEngine->TakeTouches(); };
    game.IsKeyframed = [this](const BodyKey& acKey) {
        const auto cRead = m_world.ctx().at<IBodyBridge&>().Read(acKey);
        return cRead && !cRead->Dynamic;
    };
    game.ReadTouching = [this](const std::vector<BodyKey>& acKeys) { return m_world.ctx().at<IBodyBridge&>().ReadTouching(acKeys); };
    game.EndPlayback = [this](const BodyKey& acKey, uint64_t aUntil) {
        // A guest's states this game played end here; its own physics has the body again, where it is.
        m_world.ctx().at<BodyService>().Forget(acKey, std::max(aUntil, m_world.GetFrameTick()));
        m_world.ctx().at<IBodyBridge&>().Release(acKey);
    };
    game.ReadBodies = [this](const std::vector<BodyKey>& acKeys) {
        std::vector<BodyPose> poses;
        for (const BodyKey& cKey : acKeys)
            if (const auto cRead = m_world.ctx().at<IBodyBridge&>().Read(cKey))
                poses.push_back({.Key = cKey, .Position = cRead->Position, .Rotation = cRead->Rotation});
        return poses;
    };
    game.ReadCellState = [this](const GameId& acCell, const std::vector<ChangedReference>& acChanged) {
        return m_pEngine->ReadCellState(acCell, acChanged);
    };
    game.ReadContents = [this](const GameId& acObject) { return m_pEngine->ReadContents(acObject); };
    game.Activate = [this](const GameId& acObject, uint32_t aNpcId, uint32_t aActivatorId) { m_pEngine->Activate(acObject, aNpcId, aActivatorId); };
    game.Summon = [this](uint32_t aCasterId, const SummonRequest& acSummon) { m_pEngine->Summon(aCasterId, acSummon); };
    game.TouchTrap = [this](const Payloads::TrapContact& acContact) { m_pEngine->TouchTrap(acContact); };
    game.SetLock = [this](const GameId& acObject, bool aLocked, uint8_t aLevel) { m_pEngine->SetLock(acObject, aLocked, aLevel); };
    game.SetItems = [this](const ObjectChange& acItems) { m_pEngine->SetItems(acItems); };
    game.ReadObjectState = [this](const GameId& acObject, ObjectChange::Kind aKind) { return m_pEngine->ReadObjectState(acObject, aKind); };
    game.SetObjectState = [this](const ObjectChange& acState) { m_pEngine->SetObjectState(acState); };
    game.RaiseStoryEvent = [this](const StoryEvent& acEvent) { m_pEngine->RaiseStoryEvent(acEvent); };
    game.CreateItem = [this](const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) {
        return m_pEngine->CreateItem(acItem, acPlacement);
    };
    game.TransferContainer = [this](const ContainerTransferRequest& acRequest, uint32_t aPlayer) { return m_pEngine->TransferContainer(acRequest, aPlayer); };
    game.TakeItem = [this](const GameId& acObject) { return m_pEngine->TakeItem(acObject); };
    game.DropPartyItem = [this](const Inventory::Entry& acItem, uint32_t aPlayer) { return m_pEngine->DropPartyItem(acItem, aPlayer); };
    game.TakeQuestObject = [this](const GameId& acObject) { return m_pEngine->TakeQuestObject(acObject); };
    game.ReadDifficulty = [this] { return m_world.GetPlayerBridge().GetDifficulty(); };
    // The records live in SaveService, which a world tearing down drops before its Steam lobby kicks the guests.
    const auto cRecords = [this]() -> CharacterRecords* {
        auto* pSaves = m_world.ctx().find<SaveService>();
        return pSaves ? &pSaves->GetRecords() : nullptr;
    };
    game.StoreRecord = [cRecords](const PlayerRecord& acRecord) {
        if (auto* pRecords = cRecords())
            pRecords->Put(acRecord, CharacterRecords::Clock::now());
    };
    game.SettleRecord = [cRecords](const std::string& acKey) {
        if (auto* pRecords = cRecords())
            pRecords->Settle(acKey, CharacterRecords::Clock::now());
    };
    game.StoreStorySpell = [cRecords](const GameId& acSpell, bool aAdded) {
        if (auto* pRecords = cRecords())
            pRecords->SetStorySpell(acSpell, aAdded);
    };
    game.HasRecord = [cRecords](const std::string& acKey) {
        const auto* pRecords = cRecords();
        return pRecords && pRecords->Find(acKey) != nullptr;
    };
    BindSaves(game);
    game.StorePlace = [cRecords](const std::string& acKey, const GameId& acWorldSpace, const GameId& acCell,
                                 const std::array<float, 3>& acPosition) {
        if (auto* pRecords = cRecords())
            pRecords->SetPlace(acKey, acWorldSpace, acCell, acPosition);
    };
    game.Versions = RunningGame::GetVersions();
    game.Plugins = RunningGame::Plugins(m_world.GetFormBridge());
    m_pAuthority = std::make_unique<HostAuthority>(*m_pNetwork, std::move(game));
    m_world.ctx().at<InventoryService>().SetArrivalDecider(&PartyItems::ArrivesAsParty);
    m_pConversation = std::make_unique<HostConversation>(m_world, m_dispatcher, m_world.ctx().at<DialogueService>());

    InstallFrontDoor();
}

void HostService::StopHosting() noexcept
{
    m_pAggro.reset();
    m_pTakeBacks.reset();
    m_pTraps.reset();
    m_reloadRequested = m_readCheckpoint = m_loadingSave = false;
    m_checkpoint.reset();
    m_reloadJoins.clear();
    if (!IsHosting())
        return;
    // Our own in-process connection goes through it.
    if (m_world.GetTransport().IsConnectedLocally())
        m_world.GetTransport().Close();
    m_pConversation.reset();
    m_pNetwork.reset();
    m_world.SetHosting(false);
    m_world.ctx().at<InventoryService>().SetArrivalDecider({});
    m_pAuthority.reset();
    m_pEngine.reset();
    m_world.ctx().at<CreatedIds>().SetHosting(false);
    m_pSaves.reset();
    spdlog::info("HostService: session stopped");
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

void HostService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    PROFILE_ZONE("HostService::OnUpdate");
    if (!IsHosting())
        return;
    m_pNetwork->Update();
    m_pAuthority->Update(m_pNetwork->GetTick(), acEvent.Delta, m_world.GetFrameTick());
    SendWorldAnswers();
    UpdateReload();
    if (m_pAggro)
        m_pAggro->Update(acEvent);
    if (m_pTakeBacks)
        m_pTakeBacks->Update(acEvent);
}

// The host's game is the only one that names an NPC for the party; a guest's own quests stay on that guest. An actor the
// host owns is declared by its name part (ActorStateService); another's goes to its owner.
void HostService::OnActorRenamed(const ActorRenamedEvent& acEvent) const noexcept
{
    if (!IsHosting() || acEvent.FormId == m_world.ctx().at<IPlayerBridge&>().GetPlayerFormId())
        return;
    if (const auto cSessionId = Utils::GetSessionIdByFormId(acEvent.FormId))
        m_pAuthority->NameActor(*cSessionId, acEvent.Name);
}

