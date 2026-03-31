#pragma once

#include <memory>

#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <Services/PartyService.h>
#include <Services/CharacterService.h>
#include <Services/OverlayService.h>
#include <Services/CharacterService.h>
#include <Services/MagicService.h>
#include <Services/DebugService.h>
#include <Services/HostService.h>

#include <Systems/ModSystem.h>

#include <Structs/ServerSettings.h>

// Bridge interface forward declarations — do NOT include the bridge headers here.
// SkyrimBridge.h is included only in World.cpp, keeping SKSE headers out of this file.
class IActorBridge;
class IFormBridge;
class IPlayerBridge;
class IEventBridge;
class IWorldBridge;
class SkyrimBridge;

struct World : entt::registry
{
    World();
    ~World();

    void Update() noexcept;

    RunnerService& GetRunner() noexcept;
    TransportService& GetTransport() noexcept;
    ModSystem& GetModSystem() noexcept;

    PartyService& GetPartyService() noexcept { return ctx().at<PartyService>(); }
    const PartyService& GetPartyService() const noexcept { return ctx().at<const PartyService>(); }
    CharacterService& GetCharacterService() noexcept { return ctx().at<CharacterService>(); }
    const CharacterService& GetCharacterService() const noexcept { return ctx().at<const CharacterService>(); }
    OverlayService& GetOverlayService() noexcept { return ctx().at<OverlayService>(); }
    const OverlayService& GetOverlayService() const noexcept { return ctx().at<const OverlayService>(); }
    DebugService& GetDebugService() noexcept { return ctx().at<DebugService>(); }
    const DebugService& GetDebugService() const noexcept { return ctx().at<const DebugService>(); }
    MagicService& GetMagicService() noexcept { return ctx().at<MagicService>(); }
    const MagicService& GetMagicService() const noexcept { return ctx().at<const MagicService>(); }
    HostService& GetHostService() noexcept { return ctx().at<HostService>(); }
    const HostService& GetHostService() const noexcept { return ctx().at<const HostService>(); }

    // Bridge accessors — interfaces injected into EnTT context by World.cpp
    IActorBridge& GetActorBridge() noexcept;
    IFormBridge& GetFormBridge() noexcept;
    IPlayerBridge& GetPlayerBridge() noexcept;
    IEventBridge& GetEventBridge() noexcept;
    IWorldBridge& GetWorldBridge() noexcept;

    auto& GetDispatcher() noexcept { return m_dispatcher; }

    const ServerSettings& GetServerSettings() const noexcept { return m_serverSettings; }
    void SetServerSettings(ServerSettings aServerSettings) noexcept { m_serverSettings = aServerSettings; }

    [[nodiscard]] uint64_t GetTick() const noexcept;

    static void Create() noexcept;
    [[nodiscard]] static World& Get() noexcept;

private:
    entt::dispatcher m_dispatcher;
    RunnerService m_runner;
    TransportService m_transport;
    ModSystem m_modSystem;
    ServerSettings m_serverSettings{};

    std::chrono::high_resolution_clock::time_point m_lastFrameTime;

    // Owns the single SkyrimBridge instance; interfaces registered into ctx() in ctor.
    std::unique_ptr<SkyrimBridge> m_pSkyrimBridge;
};
