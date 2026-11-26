// The engine the plugin's World runs on: Skyrim behind every port, the plugin's own services (UI, debug views,
// Papyrus natives) and the frame. SKSE headers stay in here and the bridges, out of every service.
#include <SkyrimEngine.h>

#include <core/IEngine.h>
#include <core/World.h>
#include <core/WorldGate.h>

#include <Bridges/BodyBridge.h>
#include <Bridges/BugReportBridge.h>
#include <Bridges/CrimeBridge.h>
#include <Bridges/AnimationBody.h>
#include <Bridges/DevToolsBridge.h>
#include <Bridges/DialogueBridge.h>
#include <Bridges/GuestEngine.h>
#include <Bridges/HostEngine.h>
#include <Bridges/JoinBridge.h>
#include <Bridges/SkyrimBridge.h>
#include <Bridges/SteamBridge.h>
#include <Bridges/WaitBridge.h>
#include <Diagnostics/Perf.h>
#include <core/Profile.h>
#include <LoadGameEvents.h>
#include <MenuEvents.h>
#include <ModCompat/BehaviorVar.h>
#include <DebugService.h>
#include <ImguiService.h>
#include <Bridges/PapyrusService.h>
#include <Ui/UiService.h>

namespace
{
class SkyrimEngine final : public IEngine
{
public:
    IActorBridge& Actors() noexcept override { return m_skyrim; }
    IFormBridge& Forms() noexcept override { return m_skyrim; }
    IPlayerBridge& Player() noexcept override { return m_skyrim; }
    IWorldBridge& WorldBridge() noexcept override { return m_skyrim; }
    IDialogueBridge& Dialogue() noexcept override { return m_dialogue; }
    ISteamBridge& Steam() noexcept override { return m_steam; }
    IJoinBridge& Join() noexcept override { return m_join; }
    IWaitBridge& Wait() noexcept override { return m_wait; }
    IBodyBridge& Bodies() noexcept override { return m_bodies; }
    ICrimeBridge& Crime() noexcept override { return m_crime; }
    IBugReportBridge& BugReports() noexcept override { return m_bugReports; }
    IDevToolsBridge& DevTools() noexcept override { return m_devTools; }
    IAnimationBody& AnimationBodies() noexcept override { return m_animationBodies; }
    std::unique_ptr<IGuestEngine> MakeGuestEngine() override { return std::make_unique<GuestEngine>(m_bodies); }
    std::unique_ptr<IHostEngine> MakeHostEngine() override { return std::make_unique<HostEngine>(m_bodies); }

    void AddFirstServices(World& aWorld) override
    {
        aWorld.ctx().emplace<ImguiService>();
        aWorld.ctx().emplace<UiService>(aWorld, Actors());
    }

    void AddServicesAfterCharacters(World& aWorld) override
    {
        aWorld.ctx().emplace<DebugService>(aWorld.GetDispatcher(), aWorld, aWorld.GetTransport(), aWorld.ctx().at<ImguiService>());
        aWorld.ctx().emplace<PapyrusService>(aWorld.GetDispatcher());
    }

    void RemoveServicesAfterCharacters(World& aWorld) noexcept override
    {
        aWorld.ctx().erase<PapyrusService>();
        aWorld.ctx().erase<DebugService>();
    }

    void RemoveFirstServices(World& aWorld) noexcept override
    {
        aWorld.ctx().erase<UiService>();
        aWorld.ctx().erase<ImguiService>();
    }

    void Started(World&) override
    {
        LoadGameEvents::Start();
        BehaviorVar::Get()->Init();
    }

    void Stopping(World&) noexcept override
    {
        LoadGameEvents::Stop();
        MenuEvents::Stop();
    }

    void Frame(World& aWorld) noexcept override
    {
        const auto cNow = std::chrono::high_resolution_clock::now();
        const auto cDelta = cNow - m_lastFrameTime;
        m_lastFrameTime = cNow;

        const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(cDelta).count();

        const double cFrameMs = cDeltaSeconds * 1000.0;
        auto& stats = aWorld.GetFrameStats();
        ++stats.Frames;
        stats.LongestFrameMs = std::max(stats.LongestFrameMs, cFrameMs);
        if (cFrameMs > World::kStallMs && stats.Frames > 1)
        {
            ++stats.Stalls;
            spdlog::warn("Frame stalled: {:.0f}ms since the last one (our last update took {:.1f}ms)", cFrameMs, m_lastUpdateMs);
        }

        // The engine's UI is made after the plugin loads: its menu events from the first frame it is there.
        MenuEvents::Start();

        {
            PROFILE_ZONE("World::Update");
            // The engine's frame, not ours: our update runs at a different point in each frame (S10).
            aWorld.Step(cDeltaSeconds, m_skyrim.GetRealFrameSeconds() * 1000.0);
        }
        Profile::PlotFrame();
        PROFILE_FRAME();

        m_lastUpdateMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - cNow).count();
        stats.LongestUpdateMs = std::max(stats.LongestUpdateMs, m_lastUpdateMs);
        const auto cUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        Diagnostics::Frames().Add(cUnixMs, static_cast<float>(cFrameMs), static_cast<float>(m_lastUpdateMs));
        if (m_lastUpdateMs > World::kStallMs)
            spdlog::warn("Our update took {:.0f}ms", m_lastUpdateMs);
    }

private:
    SkyrimBridge m_skyrim;
    DialogueBridge m_dialogue;
    SteamBridge m_steam;
    JoinBridge m_join;
    WaitBridge m_wait;
    BodyBridge m_bodies;
    CrimeBridge m_crime;
    BugReportBridge m_bugReports;
    DevToolsBridge m_devTools;
    AnimationBody m_animationBodies;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime{std::chrono::high_resolution_clock::now()};
    double m_lastUpdateMs{};
};
} // namespace

void CreateWorld() noexcept
{
    if (entt::locator<World>::has_value())
        return;
    entt::locator<World>::emplace(std::make_unique<SkyrimEngine>());
    World::SetCurrent(&entt::locator<World>::value());
    WorldGate::Open();
}
