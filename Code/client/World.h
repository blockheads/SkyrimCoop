#pragma once

#include <memory>

#include <RecipeBook.h>
#include <Services/CopyClock.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>

// Forward declarations only: no bridge or feature header here. World.cpp (the plugin) and the tests' Seams.cpp build
// them, so a bridge's interface can change without rebuilding everything that includes this file.
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
class SkyrimBridge;

struct SessionService;
struct TravelService;
struct CharacterService;
struct DebugService;
struct MagicService;
struct HostService;
struct Hosting;

struct World : entt::registry
{
    World();
    ~World();

    void Update() noexcept;

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

    RunnerService& GetRunner() noexcept;
    TransportService& GetTransport() noexcept;

    SessionService& GetSessionService() noexcept;
    const SessionService& GetSessionService() const noexcept;
    TravelService& GetTravelService() noexcept;
    const TravelService& GetTravelService() const noexcept;
    CharacterService& GetCharacterService() noexcept;
    const CharacterService& GetCharacterService() const noexcept;
    DebugService& GetDebugService() noexcept;
    const DebugService& GetDebugService() const noexcept;
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

    static void Create() noexcept;
    [[nodiscard]] static World& Get() noexcept;

private:
    entt::dispatcher m_dispatcher;
    // Must exist before TransportService looks up its bridge references.
    std::unique_ptr<SkyrimBridge> m_pSkyrimBridge;
    std::unique_ptr<IDialogueBridge> m_pDialogueBridge;
    std::unique_ptr<ISteamBridge> m_pSteamBridge;
    std::unique_ptr<IJoinBridge> m_pJoinBridge;
    std::unique_ptr<IWaitBridge> m_pWaitBridge;
    std::unique_ptr<IBodyBridge> m_pBodyBridge;
    std::unique_ptr<ICrimeBridge> m_pCrimeBridge;
    std::unique_ptr<IBugReportBridge> m_pBugReportBridge;
    std::unique_ptr<IDevToolsBridge> m_pDevToolsBridge;
    RecipeBook m_recipes;
    FrameClock m_frameClock;
    CopyClock m_copyClock;
    std::unique_ptr<GraphStreams> m_pGraphStreams;
    std::unique_ptr<ActionClaims> m_pActionClaims;
    RunnerService m_runner;
    TransportService m_transport;

    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    FrameStats m_frameStats;
    double m_lastUpdateMs{};
    bool m_hosting{};

};
