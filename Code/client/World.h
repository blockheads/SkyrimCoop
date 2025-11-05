#pragma once

#include <Services/RunnerService.h>
#include <Services/TransportService.h>
#include <Services/PartyService.h>
#include <Services/CharacterService.h>
#include <Services/OverlayService.h>
#include <Services/CharacterService.h>
#include <Services/MagicService.h>
#include <Services/DebugService.h>

#include <Systems/ModSystem.h>

#include <Structs/ServerSettings.h>

class NetworkClient;

struct World : entt::registry
{
    World();
    ~World();

    void Update() noexcept;

    // P2P: Connection methods
    bool ConnectToHost(const TiltedPhoques::String& aHostAddress, uint16_t aPort = 10578) noexcept;
    void Disconnect() noexcept;
    [[nodiscard]] bool IsConnected() const noexcept;

    // P2P: Network client access
    NetworkClient* GetNetworkClient() noexcept { return m_pNetworkClient.get(); }
    const NetworkClient* GetNetworkClient() const noexcept { return m_pNetworkClient.get(); }

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

    auto& GetDispatcher() noexcept { return m_dispatcher; }

    const ServerSettings& GetServerSettings() const noexcept { return m_serverSettings; }
    void SetServerSettings(ServerSettings aServerSettings) noexcept { m_serverSettings = aServerSettings; }

    [[nodiscard]] uint64_t GetTick() const noexcept;

    // P2P: Is this the host's World or a client's World?
    [[nodiscard]] bool IsHost() const noexcept { return m_isHost; }
    void SetIsHost(bool isHost) noexcept { m_isHost = isHost; }

    static void Create() noexcept;
    [[nodiscard]] static World& Get() noexcept;

private:
    entt::dispatcher m_dispatcher;
    RunnerService m_runner;
    TransportService m_transport;
    ModSystem m_modSystem;
    ServerSettings m_serverSettings{};

    std::chrono::high_resolution_clock::time_point m_lastFrameTime;

    // P2P: True if this is the host's World (authoritative), false if client's World (replica)
    bool m_isHost{false};

    // P2P: Network client for connecting to host (replaces TransportService eventually)
    std::unique_ptr<NetworkClient> m_pNetworkClient;
};
