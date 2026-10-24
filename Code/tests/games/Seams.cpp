// What the real client links from engine files, for games in this process: World (only the services a flow needs, on
// the fake engine) and RunningGame (the host's game; the plugins the fake engine lists).
#include "FakeDialogue.h"
#include "FakeEngine.h"
#include <Games/DeathActions.h>

#include <Bridges/IActorBridge.h>
#include "GuestSeams.h"

#include <Events/UpdateEvent.h>
#include <RunningGame.h>
#include <Components/RemoteAnimationComponent.h>
#include <Components/FormIdComponent.h>
#include <Services/ActorValueService.h>
#include <Services/AiStateService.h>
#include <Services/EffectVisualsService.h>
#include <Services/CharacterService.h>
#include <Services/CombatService.h>
#include <Services/CutsceneService.h>
#include <Services/DialogueService.h>
#include <Services/HostService.h>
#include <Services/InventoryService.h>
#include <Services/MagicService.h>
#include <Services/MapService.h>
#include <Services/ObjectService.h>
#include <Services/TimeScaleService.h>
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
    : m_pSkyrimBridge(CreateBridgeContext(*this)), m_pDialogueBridge(std::make_unique<FakeDialogue>()),
      m_runner(m_dispatcher), m_transport(*this, m_dispatcher)
{
    m_dispatcher.sink<UpdateEvent>().connect<&RunnerService::OnUpdate>(m_runner);
    // In the real World's order: TravelService before CharacterService.
    ctx().emplace<TravelService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<CharacterService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>(),
                                    ctx().at<IPlayerBridge&>());
    ctx().emplace<ObjectService>(*this, m_dispatcher, m_transport, ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>());
    ctx().emplace<TimeScaleService>(*this, m_dispatcher, m_transport);
    ctx().emplace<ActorValueService>(*this, m_dispatcher, m_transport);
    ctx().emplace<InventoryService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<MagicService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>(), ctx().at<IFormBridge&>());
    ctx().emplace<EffectVisualsService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<AiStateService>(*this, m_dispatcher, m_transport, ctx().at<IActorBridge&>());
    ctx().emplace<CutsceneService>(*this, m_dispatcher, m_transport);
    ctx().emplace<FakeDialogue&>(static_cast<FakeDialogue&>(*m_pDialogueBridge));
    ctx().emplace<DialogueService>(*this, m_dispatcher, m_transport, *m_pDialogueBridge);
    ctx().emplace<PlayerService>(*this, m_dispatcher, m_transport, ctx().at<IPlayerBridge&>(), ctx().at<IFormBridge&>(),
                                 ctx().at<IActorBridge&>());
    ctx().emplace<CombatService>(*this, m_transport, m_dispatcher, ctx().at<IFormBridge&>(), ctx().at<IActorBridge&>(),
                                 ctx().at<IPlayerBridge&>());
    ctx().emplace<MapService>(*this, m_dispatcher, m_transport);
    ctx().emplace<HostService>(*this, m_dispatcher);
    ctx().emplace<GuestSeam>(*this);
}

// As the real World: the services go first, newest first, while the dispatcher and the engine they use still live.
World::~World()
{
    ctx().erase<GuestSeam>();
    ctx().erase<HostService>();
    ctx().erase<MapService>();
    ctx().erase<CombatService>();
    ctx().erase<PlayerService>();
    ctx().erase<DialogueService>();
    ctx().erase<CutsceneService>();
    ctx().erase<AiStateService>();
    ctx().erase<EffectVisualsService>();
    ctx().erase<MagicService>();
    ctx().erase<InventoryService>();
    ctx().erase<ActorValueService>();
    ctx().erase<TimeScaleService>();
    ctx().erase<ObjectService>();
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
    // The real AnimationSystem replays a dying owner's action 300 ms behind its tick.
    auto& engine = ctx().at<FakeEngine&>();
    const uint64_t cTick = GetTransport().GetTick();
    for (auto&& [cEntity, animation, form] : view<RemoteAnimationComponent, FormIdComponent>().each())
    {
        for (auto it = animation.TimePoints.begin(); it != animation.TimePoints.end();)
        {
            if (DeathActions::IsDying(it->State1))
            {
                if (engine.HoldDeathActions || cTick < it->Tick + 300)
                {
                    ++it;
                    continue;
                }
                engine.ReplayDeathAction(form.Id, *it);
            }
            it = animation.TimePoints.erase(it);
        }
    }
    ctx().at<GuestSeam>().Update();
    ctx().at<FakeEngine&>().Frame();
}

RunnerService& World::GetRunner() noexcept
{
    return m_runner;
}

IActorBridge& World::GetActorBridge() noexcept
{
    return ctx().at<IActorBridge&>();
}

// As the real World: the session's clock (the host's network's, or the guest's estimate of it).
uint64_t World::GetTick() const noexcept
{
    return m_transport.GetTick();
}

TransportService& World::GetTransport() noexcept
{
    return m_transport;
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

// Skyrim 1.6.1170 with SKSE 2.2.6; Skyrim.esm, Update.esm, a light ccBGSSSE001-Fish.esm (HostAuthorityTests' host).
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
