// What the real client links from engine files, for games in this process: World (only the services a flow needs, on
// the fake engine) and RunningGame (the host's game; the plugins the fake engine lists).
#include "FakeEngine.h"

#include <Bridges/IActorBridge.h>
#include "GuestSeams.h"

#include <Events/UpdateEvent.h>
#include <RunningGame.h>
#include <Services/ActorValueService.h>
#include <Services/CharacterService.h>
#include <Services/CutsceneService.h>
#include <Services/HostService.h>
#include <Services/PlayerService.h>
#include <Services/TravelService.h>
#include <Services/RunnerService.h>
#include <Structs/GameVersions.h>
#include <Structs/Mods.h>
#include <World.h>

// The engine behind the bridges: the fake.
class SkyrimBridge : public FakeEngine
{
};

namespace
{
World* s_pCurrent = nullptr;

std::unique_ptr<SkyrimBridge> CreateBridgeContext(World& aWorld)
{
    auto bridge = std::make_unique<SkyrimBridge>();
    FakeEngine& engine = *bridge;
    aWorld.ctx().emplace<FakeEngine&>(engine);
    aWorld.ctx().emplace<IActorBridge&>(static_cast<IActorBridge&>(engine));
    aWorld.ctx().emplace<IFormBridge&>(static_cast<IFormBridge&>(engine));
    aWorld.ctx().emplace<IPlayerBridge&>(static_cast<IPlayerBridge&>(engine));
    aWorld.ctx().emplace<IWorldBridge&>(static_cast<IWorldBridge&>(engine));
    return bridge;
}
} // namespace

void SetCurrentWorld(World* apWorld) noexcept
{
    s_pCurrent = apWorld;
}

World::World()
    : m_pSkyrimBridge(CreateBridgeContext(*this)), m_runner(m_dispatcher), m_transport(*this, m_dispatcher),
      m_modSystem(m_dispatcher, ctx().at<IFormBridge&>())
{
    m_dispatcher.sink<UpdateEvent>().connect<&RunnerService::OnUpdate>(m_runner);
    // In the real World's order: TravelService before CharacterService.
    ctx().emplace<TravelService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                    ctx().at<IPlayerBridge&>());
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<CutsceneService>(*this, m_dispatcher, m_transport);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport, ctx().at<IPlayerBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IActorBridge&>());
    ctx().emplace<HostService>(*this, m_dispatcher);
    ctx().emplace<GuestSeam>(*this);
}

// As the real World: the services go first, newest first, while the dispatcher and the engine they use still live.
World::~World()
{
    ctx().erase<GuestSeam>();
    ctx().erase<HostService>();
    ctx().erase<PlayerService>();
    ctx().erase<CutsceneService>();
    ctx().erase<ActorValueService>();
    ctx().erase<CharacterService>();
    ctx().erase<TravelService>();
}

World& World::Get() noexcept
{
    return *s_pCurrent;
}

void World::Update() noexcept
{
    m_dispatcher.trigger(UpdateEvent(1.0 / 60));
    ctx().at<GuestSeam>().Update();
    ctx().at<FakeEngine&>().Frame();
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
}

ModSystem& World::GetModSystem() noexcept
{
    return m_modSystem;
}

IFormBridge& World::GetFormBridge() noexcept
{
    return ctx().at<IFormBridge&>();
}

TravelService& World::GetTravelService() noexcept
{
    return ctx().at<TravelService>();
}

CharacterService& World::GetCharacterService() noexcept
{
    return ctx().at<CharacterService>();
}

HostService& World::GetHostService() noexcept
{
    return ctx().at<HostService>();
}

// Skyrim 1.6.1170 with SKSE 2.2.6; Skyrim.esm, Update.esm, a light ccBGSSSE001-Fish.esm (BridgeTests' host).
namespace RunningGame
{
const GameVersions& GetVersions()
{
    static const GameVersions s_versions{GameVersions::Pack(1, 6, 1170), GameVersions::Pack(2, 2, 6)};
    return s_versions;
}

// As RunningGame::Plugins: the loaded plugins the bridge lists.
Mods Plugins(const IFormBridge& acForms)
{
    Mods plugins;
    for (const auto& cMod : acForms.GetLoadedMods())
        plugins.ModList.push_back({cMod.Filename, cMod.Id, cMod.IsLite});
    return plugins;
}
} // namespace RunningGame
