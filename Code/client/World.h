#pragma once

#include <Bridges/IDialogueBridge.h>
#include <Bridges/IJoinBridge.h>
#include <Bridges/IWaitBridge.h>
#include <Bridges/IBodyBridge.h>
#include <Bridges/ICrimeBridge.h>
#include <Bridges/ISteamBridge.h>

#include <memory>

#include <RecipeBook.h>
#include <Services/RunnerService.h>
#include <Services/TransportService.h>

// Bridge interface forward declarations — do NOT include the bridge headers here.
// SkyrimBridge.h is included only in World.cpp, keeping SKSE headers out of this file.
class IActorBridge;
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

    [[nodiscard]] uint64_t GetTick() const noexcept;

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
    RecipeBook m_recipes;
    RunnerService m_runner;
    TransportService m_transport;

    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    FrameStats m_frameStats;
    double m_lastUpdateMs{};

};
