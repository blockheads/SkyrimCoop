#pragma once

#include <memory>

#include <RecipeBook.h>
#include <Services/CopyClock.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>

// Forward declarations only: no bridge or feature header here, so a bridge's interface can change without rebuilding
// everything that includes this file.
class IActorBridge;
class IBodyBridge;
class ICrimeBridge;
class IDialogueBridge;
class IJoinBridge;
class ISteamBridge;
class IWaitBridge;
class IBugReportBridge;
class IDevToolsBridge;
struct ActionClaims;
struct GraphStreams;
class IFormBridge;
class IPlayerBridge;
class IWorldBridge;
class IEngine;

struct SessionService;
struct TravelService;
struct CharacterService;
struct MagicService;
struct HostService;
struct Hosting;

struct World : entt::registry
{
    // Builds every service on that engine (IEngine.h).
    explicit World(std::unique_ptr<IEngine> apEngine);
    ~World();

    // A frame, as the engine runs it (IEngine::Frame); it calls Step.
    void Update() noexcept;
    // The frame's update: the clocks, then every service's (UpdateEvent). aEngineFrameMs: the engine's own frame.
    void Step(double aDeltaSeconds, double aEngineFrameMs) noexcept;
    [[nodiscard]] IEngine& GetEngine() noexcept { return *m_pEngine; }

    // Frames that stalled (over kStallMs), and how much of the worst ones was our own update: the
    // rest is the engine (or Wine) between our updates.
    static constexpr double kStallMs = 100.0;
    struct FrameStats
    {
        uint64_t Frames{};
        uint32_t Stalls{};
        double LongestFrameMs{};
        double LongestUpdateMs{};
    };
    [[nodiscard]] const FrameStats& GetFrameStats() const noexcept { return m_frameStats; }
    [[nodiscard]] FrameStats& GetFrameStats() noexcept { return m_frameStats; }

    RunnerService& GetRunner() noexcept;
    TransportService& GetTransport() noexcept;

    SessionService& GetSessionService() noexcept;
    const SessionService& GetSessionService() const noexcept;
    TravelService& GetTravelService() noexcept;
    const TravelService& GetTravelService() const noexcept;
    CharacterService& GetCharacterService() noexcept;
    const CharacterService& GetCharacterService() const noexcept;
    MagicService& GetMagicService() noexcept;
    const MagicService& GetMagicService() const noexcept;
    // Hosting as the features use it (HostService).
    Hosting& GetHosting() noexcept;
    HostService& GetHostService() noexcept;
    const HostService& GetHostService() const noexcept;

    // Bridge accessors — interfaces injected into EnTT context by World.cpp
    IActorBridge& GetActorBridge() noexcept;
    IFormBridge& GetFormBridge() noexcept;
    IPlayerBridge& GetPlayerBridge() noexcept;
    IWorldBridge& GetWorldBridge() noexcept;

    auto& GetDispatcher() noexcept { return m_dispatcher; }
    // The player-made potions' recipes this game knows (ItemIds).
    RecipeBook& GetRecipes() noexcept { return m_recipes; }
    // The copies' playback time, stepped once a frame before UpdateEvent.
    CopyClock& GetCopyClock() noexcept { return m_copyClock; }
    // The session's time at this frame (FrameClock): what owners stamp their snapshots and actions with.
    [[nodiscard]] uint64_t GetFrameTick() const noexcept { return m_frameClock.Now(); }
    // The graph input stream's recorders and replayers (graph-input-stream.md), on the graphs' threads too.
    GraphStreams& GetGraphStreams() noexcept { return *m_pGraphStreams; }
    // The host's answers to action claims (ClaimService writes; AnimationSystem and held blows read).
    ActionClaims& GetActionClaims() noexcept { return *m_pActionClaims; }

    [[nodiscard]] uint64_t GetTick() const noexcept;
    // This game hosts the session (HostService sets it as it starts and stops hosting).
    [[nodiscard]] bool IsHosting() const noexcept { return m_hosting; }
    void SetHosting(bool aHosting) noexcept { m_hosting = aHosting; }

    // The game this code runs in: the plugin's one World; in the tests, the game whose turn it is.
    [[nodiscard]] static World& Get() noexcept;
    [[nodiscard]] static bool Exists() noexcept;
    static void SetCurrent(World* apWorld) noexcept;

private:
    entt::dispatcher m_dispatcher;
    // First: every service is built on it, and outlives none of them.
    std::unique_ptr<IEngine> m_pEngine;
    RecipeBook m_recipes;
    FrameClock m_frameClock;
    CopyClock m_copyClock;
    std::unique_ptr<GraphStreams> m_pGraphStreams;
    std::unique_ptr<ActionClaims> m_pActionClaims;
    RunnerService m_runner;
    TransportService m_transport;

    FrameStats m_frameStats;
    bool m_hosting{};

};
