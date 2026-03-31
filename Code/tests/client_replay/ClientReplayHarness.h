#pragma once

/// ClientReplayHarness — headless test wrapper for the client bridge layer.
///
/// Uses a bare entt::registry (NOT World) to avoid pulling in SKSE headers
/// via World.cpp. The registry's ctx() is used to register bridge interfaces,
/// mirroring exactly how World.cpp injects them at runtime.
///
/// Pattern: same as ServerReplayHarness (server_replay_harness.h).

#include <memory>
#include <string>

#include <entt/entt.hpp>

// Bridge interfaces (forward declarations)
class IActorBridge;
class IFormBridge;
class IPlayerBridge;
class IEventBridge;
class IWorldBridge;
class ReplayBridge;

class ClientReplayHarness
{
public:
    ClientReplayHarness();
    ~ClientReplayHarness();

    /// Load an NDJSON fixture file into the ReplayBridge.
    void LoadFixture(const std::string& aPath);

    /// Fire a synthetic UpdateEvent through the bridge (no real services run).
    void Tick(double aDelta = 0.016);

    /// Access the underlying registry (contains bridge ctx() entries).
    entt::registry& GetRegistry() noexcept;

    /// Direct access to the ReplayBridge for test assertions.
    ReplayBridge& GetReplayBridge() noexcept;

private:
    std::unique_ptr<ReplayBridge> m_pBridge;
    entt::registry m_registry;
};
