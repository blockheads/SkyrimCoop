#include <Services/HostService.h>

#include <Events/ActivateEvent.h>
#include <Events/ContainerChangeEvent.h>
#include <Events/ScriptAnimationEvent.h>
#include <Events/UpdateEvent.h>
#include <World.h>
#include <Services/TransportService.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostNetwork.h>
#include <Services/CharacterService.h>
#include <Bridges/HostEngine.h>
#include <Bridges/IPlayerBridge.h>
#include <Bridges/IWorldBridge.h>
#include <RunningGame.h>
#include <Structs/GameVersions.h>

HostService::HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&HostService::OnUpdate>(this);
    m_activateConnection = m_dispatcher.sink<ActivateEvent>().connect<&HostService::OnActivate>(this);
    m_scriptAnimationConnection = m_dispatcher.sink<ScriptAnimationEvent>().connect<&HostService::OnScriptAnimation>(this);
    m_containerChangeConnection = m_dispatcher.sink<ContainerChangeEvent>().connect<&HostService::OnContainerChange>(this);
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

    m_pEngine = std::make_unique<HostEngine>(m_world.GetModSystem());
    HostGame game;
    game.OwnSpawns = [this](uint32_t aExcludedSessionId) { return m_world.GetCharacterService().BuildSpawnRequests(aExcludedSessionId); };
    game.ReadCopy = [this](uint32_t aSessionId, NotifyCharacterSpawn& aSpawn) { return m_world.GetCharacterService().ReadCopy(aSessionId, aSpawn); };
    game.ReadTime = [this] { return m_world.ctx().at<IWorldBridge&>().GetGameTime(); };
    game.SetHour = [this](float aHours) { m_pEngine->SetHour(aHours); };
    game.ReadWeather = [this] { return m_pEngine->ReadWeather(); };
    game.ReadGlobals = [this] { return m_pEngine->ReadGlobals(); };
    game.ReadQuests = [this] { return m_pEngine->ReadQuests(); };
    game.TakeObjectChanges = [this] { return m_pEngine->TakeObjectChanges(); };
    game.ReadCellState = [this](const GameId& acCell, const TiltedPhoques::Vector<ChangedReference>& acChanged) {
        return m_pEngine->ReadCellState(acCell, acChanged);
    };
    game.SetLock = [this](const GameId& acObject, bool aLocked, uint8_t aLevel) { m_pEngine->SetLock(acObject, aLocked, aLevel); };
    game.SetItems = [this](const GameId& acObject, const GameId& acItem, uint32_t aCount) { m_pEngine->SetItems(acObject, acItem, aCount); };
    game.ReadDifficulty = [this] { return m_world.GetPlayerBridge().GetDifficulty(); };
    game.Versions = RunningGame::GetVersions();
    game.Plugins = RunningGame::Plugins(m_world.GetFormBridge());
    m_pAuthority = std::make_unique<HostAuthority>(*m_pNetwork, std::move(game));

    spdlog::info("HostService: hosting on port {}", m_pNetwork->GetPort());
    return true;
}

void HostService::StopHosting() noexcept
{
    if (!IsHosting())
        return;
    // Our own in-process connection goes through it.
    if (m_world.GetTransport().IsConnectedLocally())
        m_world.GetTransport().Close();
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

void HostService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!IsHosting())
        return;
    m_pNetwork->Update();
    m_pAuthority->Update(m_pNetwork->GetTick());
}
