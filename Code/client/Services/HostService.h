#pragma once

#include <memory>
#include <spdlog/spdlog.h>

struct World;
struct UpdateEvent;
struct ActivateEvent;
struct ScriptAnimationEvent;
struct ContainerChangeEvent;

struct HostAuthority;
struct HostNetwork;
class HostEngine;

/**
 * @brief Hosting a session: the listener the other players' games join (HostNetwork), the host's
 * rules (HostAuthority) and its game's reads (HostEngine). The host's own game joins in process.
 */
struct HostService
{
    HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~HostService() noexcept;

    TP_NOCOPYMOVE(HostService);

    // Listens on aPort, or the next free one above it.
    bool StartHosting(uint16_t aPort = 10578) noexcept;
    void StopHosting() noexcept;

    [[nodiscard]] bool IsHosting() const noexcept { return m_pNetwork != nullptr; }
    // Null when not hosting.
    [[nodiscard]] HostNetwork* GetNetwork() const noexcept { return m_pNetwork.get(); }

    // Removes a player from the session (hosting only).
    void KickPlayer(uint32_t aPlayerId) noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    // The host's world scripts' events and containers' items for HostWorldState: doors, animations, item counts
    // (docs/design/world-state-sync.md steps 5-6). State (enabled, locks) HostEngine reads from the change funnel itself.
    void OnActivate(const ActivateEvent& acEvent) noexcept;
    void OnScriptAnimation(const ScriptAnimationEvent& acEvent) noexcept;
    void OnContainerChange(const ContainerChangeEvent& acEvent) noexcept;

private:
    World& m_world;
    entt::dispatcher& m_dispatcher;

    // Declared first so they outlive the network, which calls into the authority, which reads the engine.
    std::unique_ptr<HostEngine> m_pEngine;
    std::unique_ptr<HostAuthority> m_pAuthority;
    std::unique_ptr<HostNetwork> m_pNetwork;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_activateConnection;
    entt::scoped_connection m_scriptAnimationConnection;
    entt::scoped_connection m_containerChangeConnection;
};
