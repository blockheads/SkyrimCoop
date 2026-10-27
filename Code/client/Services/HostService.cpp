#include <Events/GameLoadFailedEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Services/HostService.h>
#include <Bridges/IBodyBridge.h>
#include <Services/BodyService.h>
#include <Services/CrimeService.h>
#include <Services/DialogueService.h>
#include <Services/Host/HostConversation.h>

#include <Events/ActivateEvent.h>
#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/GameSavingEvent.h>
#include <Bridges/IActorBridge.h>
#include <Bridges/IJoinBridge.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Events/PlayerNativeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Events/UpdateEvent.h>
#include <World.h>
#include <Services/TransportService.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostNetwork.h>
#include <Services/CharacterService.h>
#include <Bridges/HostEngine.h>
#include <Components.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IWorldBridge.h>
#include <RunningGame.h>
#include <Structs/GameVersions.h>
#include <Messages/Relayed.h>
#include <Papyrus/GlobalCall.h>
#include <Papyrus/GuestScripts.h>
#include <Papyrus/PlayerNatives.h>
#include <Utils.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    Listen<UpdateEvent, &HostService::OnUpdate>(m_connections, m_dispatcher, this);
    Listen<ActivateEvent, &HostService::OnActivate>(m_connections, m_dispatcher, this);
    Listen<ScriptAnimationEvent, &HostService::OnScriptAnimation>(m_connections, m_dispatcher, this);
    Listen<ContainerChangeEvent, &HostService::OnContainerChange>(m_connections, m_dispatcher, this);
    Listen<DifficultyChangeEvent, &HostService::OnDifficultyChange>(m_connections, m_dispatcher, this);
    Listen<GameLoadFailedEvent, &HostService::OnGameLoadFailed>(m_connections, m_dispatcher, this);
    Listen<GameLoadedEvent, &HostService::OnGameLoaded>(m_connections, m_dispatcher, this);
    Listen<GameSavingEvent, &HostService::OnGameSaving>(m_connections, m_dispatcher, this);
    Listen<PlayerNativeEvent, &HostService::OnPlayerNative>(m_connections, m_dispatcher, this);
    Listen<NotifyPlayerNativeResult, &HostService::OnPlayerNativeResult>(m_connections, m_dispatcher, this);
    Listen<NotifyWorldCall, &HostService::OnWorldCall>(m_connections, m_dispatcher, this);
}

void HostService::OnActivate(const ActivateEvent& acEvent) noexcept
{
    if (IsHosting() && acEvent.ByReference)
        m_pEngine->RecordActivation(acEvent.ObjectFormId, acEvent.ActivatorFormId, acEvent.PreActivationOpenStateValue);
}

void HostService::OnScriptAnimation(const ScriptAnimationEvent& acEvent) noexcept
{
    if (IsHosting())
        m_pEngine->RecordAnimation(acEvent.FormID, acEvent.Animation, acEvent.EventName);
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

void HostService::OnPlayerNative(const PlayerNativeEvent& acEvent) noexcept
{
    const auto cGuest = Utils::GetSessionIdByFormId(acEvent.GuestFormId);
    if (!IsHosting() || !cGuest)
        return;
    PlayerNativeRequest request;
    request.SessionId = *cGuest;
    request.Call = Utils::ToScriptCall(acEvent.Call);
    request.ReplyId = acEvent.ReplyId;
    spdlog::info("HostService: {}.{} from a script run for guest {:X} goes to its game", request.Call.Class, request.Call.Function, acEvent.GuestFormId);
    m_world.GetTransport().Send(request);
}

void HostService::OnPlayerNativeResult(const NotifyPlayerNativeResult& acMessage) noexcept
{
    if (!IsHosting())
        return;
    spdlog::info("HostService: the guest's game answered stack {}", acMessage.ReplyId);
    PlayerNatives::Answer(acMessage.ReplyId, Utils::ToLocalValue(acMessage.Result).value_or(std::monostate{}));
}

void HostService::OnWorldCall(const NotifyWorldCall& acMessage) noexcept
{
    const uint32_t cGuest = Utils::GetFormIdBySessionId(acMessage.SessionId);
    const auto cCall = Utils::ToLocalCall(acMessage.Call);
    if (!IsHosting() || !cGuest || !cCall)
    {
        spdlog::warn("HostService: {}.{} from a guest's script names a form or character not here", acMessage.Call.Class, acMessage.Call.Function);
        return;
    }
    spdlog::info("HostService: {}.{} from guest {:X}'s own script runs here, for it", acMessage.Call.Class, acMessage.Call.Function, cGuest);
    const GuestScripts::ScopedCause cCause(cGuest); // the call runs for that guest (rule 2)
    GlobalCall::QueueCall(*cCall);
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

    MakeAuthority();
    spdlog::info("HostService: hosting on port {}", m_pNetwork->GetPort());
    return true;
}

void HostService::MakeAuthority() noexcept
{
    m_pEngine = std::make_unique<HostEngine>(m_world.ctx().at<IBodyBridge&>());
    HostGame game;
    game.TakeCrimeConfiscations = [this] { return m_world.ctx().at<CrimeService>().TakeConfiscations(); };
    game.ReadCrimeGold = [this] { return m_world.ctx().at<CrimeService>().ReadGold(); };
    game.JudgeCrime = [this](uint32_t aActor, const CrimeRequest& aCrime) { return m_world.ctx().at<CrimeService>().Judge(aActor, aCrime); };
    game.OwnSpawns = [this](uint32_t aExcludedSessionId) { return m_world.GetCharacterService().BuildSpawnRequests(aExcludedSessionId); };
    game.ReadCopy = [this](uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) { return m_world.GetCharacterService().ReadCopy(aSessionId, aSpawn); };
    game.ReadTime = [this] { return m_world.ctx().at<IWorldBridge&>().GetGameTime(); };
    game.SetHour = [this](float aHours) { m_pEngine->SetHour(aHours); };
    game.ReadWeather = [this] { return m_pEngine->ReadWeather(); };
    game.ReadGlobals = [this] { return m_pEngine->ReadGlobals(); };
    game.TakeGlobalChanges = [this] { return m_pEngine->TakeGlobalChanges(); };
    game.ReadQuests = [this] { return m_pEngine->ReadQuests(); };
    game.TakeQuestChanges = [this] { return m_pEngine->TakeQuestChanges(); };
    game.TakeObjectChanges = [this] { return m_pEngine->TakeObjectChanges(); };
    game.TakeBodies = [this] {
        return m_pEngine->TakeBodies([this](uint32_t aFormId) -> uint32_t {
            for (const auto cEntity : m_world.view<LocalComponent, FormIdComponent>())
                if (m_world.get<FormIdComponent>(cEntity).Id == aFormId)
                    return m_world.get<LocalComponent>(cEntity).Id;
            return 0;
        });
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
    game.PushBody = [this](const BodyPush& acPush) { return m_world.ctx().at<IBodyBridge&>().Push(acPush); };
    game.FreeBody = [this](const BodyFrame& acFrame) {
        // The holder's frames this game played end here (BodyService); its own physics has the body again.
        m_world.ctx().at<BodyService>().Forget(acFrame.Key);
        return m_world.ctx().at<IBodyBridge&>().Free(acFrame);
    };
    game.ReadBodies = [this](const std::vector<BodyKey>& acKeys) {
        std::vector<BodyPose> poses;
        for (const BodyKey& cKey : acKeys)
            if (const auto cPose = m_world.ctx().at<IBodyBridge&>().ReadPose(cKey))
                poses.push_back(*cPose);
        return poses;
    };
    game.ReadCellState = [this](const GameId& acCell, const std::vector<ChangedReference>& acChanged) {
        return m_pEngine->ReadCellState(acCell, acChanged);
    };
    game.SetLock = [this](const GameId& acObject, bool aLocked, uint8_t aLevel) { m_pEngine->SetLock(acObject, aLocked, aLevel); };
    game.SetItems = [this](const GameId& acObject, const GameId& acItem, uint32_t aCount) { m_pEngine->SetItems(acObject, acItem, aCount); };
    game.ReadDifficulty = [this] { return m_world.GetPlayerBridge().GetDifficulty(); };
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
    game.Plugins = RunningGame::Plugins(m_world.GetFormBridge());
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
    // Our own in-process connection goes through it.
    if (m_world.GetTransport().IsConnectedLocally())
        m_world.GetTransport().Close();
    m_pConversation.reset();
    m_pNetwork.reset();
    m_pAuthority.reset();
    m_pEngine.reset();
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
    if (!IsHosting())
        return;
    m_pNetwork->Update();
    m_pAuthority->Update(m_pNetwork->GetTick(), acEvent.Delta);
    UpdateReload();
}
