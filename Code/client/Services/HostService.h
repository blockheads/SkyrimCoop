#pragma once

#include <Events/Listen.h>
#include <memory>
#include <string>
#include <network/ENetInterface.h>
#include <Bridges/IJoinBridge.h>
#include <Messages/AuthenticationRequest.h>
#include <spdlog/spdlog.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct World;
struct UpdateEvent;
struct ActivateEvent;
struct ScriptAnimationEvent;
struct ContainerChangeEvent;
struct DifficultyChangeEvent;
struct GameSavingEvent;
struct GameLoadedEvent;
struct GameLoadFailedEvent;

struct HostAuthority;
struct HostNetwork;
class HostEngine;
struct HostConversation;

/**
 * @brief Hosting a session: the listener the other players' games join (HostNetwork), the host's
 * rules (HostAuthority) and its game's reads (HostEngine). The host's own game joins in process.
 */
struct HostService
{
    HostService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~HostService() noexcept;

    NO_COPY_MOVE(HostService);

    // Listens on aPort, or the next free one above it.
    bool StartHosting(uint16_t aPort = 10578) noexcept;
    void StopHosting() noexcept;
    void AnnounceEnd() const noexcept;

    [[nodiscard]] bool IsHosting() const noexcept { return m_pNetwork != nullptr; }
    // Null when not hosting.
    [[nodiscard]] HostNetwork* GetNetwork() const noexcept { return m_pNetwork.get(); }

    // A host quickload or menu load: the chosen save goes through the party reload.
    void LoadSave(const std::string& acName) noexcept;

    // Removes a player from the session (hosting only).
    void KickPlayer(uint32_t aPlayerId) noexcept;
    // A session id for one of this game's actors while hosting, else 0.
    [[nodiscard]] uint32_t NewCharacterId() noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    // The host's world scripts' events and containers' items for HostWorldState: doors, animations, item counts
    // (docs/design/world-state-sync.md steps 5-6). State (enabled, locks) HostEngine reads from the change funnel itself.
    void OnActivate(const ActivateEvent& acEvent) noexcept;
    void OnScriptAnimation(const ScriptAnimationEvent& acEvent) noexcept;
    void OnContainerChange(const ContainerChangeEvent& acEvent) noexcept;
    void OnDifficultyChange(const DifficultyChangeEvent& acEvent) const noexcept;
    // The host's save is being written: the guests' records take their characters' places first.
    void OnGameSaving(const GameSavingEvent&) const noexcept;
    // That guest's game answered one returning a value: the script waiting on it goes on.
    void OnPlayerNativeResult(const NotifyPlayerNativeResult& acMessage) noexcept;
    // A guest's own script changed the world (a quest's stage): this game makes the call, for that guest (rule 2).
    void OnWorldCall(const NotifyRoutedCall& acMessage) noexcept;

private:
    void MakeAuthority() noexcept;
    // The listener's front door: a join waits while a party reload runs, everything else goes to the host's rules.
    void InstallFrontDoor() noexcept;

    // A party reload (HostServiceReload.cpp): the selected save (or a wipe's previous save) goes to the guests,
    // then this game loads it, and its rules are made again on the same listener, taking the joins that waited.
    void UpdateReload() noexcept;
    void OnGameLoaded(const GameLoadedEvent&) noexcept;
    void OnGameLoadFailed(const GameLoadFailedEvent&) noexcept;
    bool m_reloadRequested{}; // a host load or the whole party down
    bool m_readCheckpoint{};  // the save read (m_checkpoint, or none to load)
    bool m_loadingSave{};     // delivered to the guests, loading here
    std::optional<IJoinBridge::SaveFiles> m_checkpoint;
    std::map<Net::ConnectionId_t, AuthenticationRequest> m_reloadJoins; // joins (the guests again) that came meanwhile

    World& m_world;
    entt::dispatcher& m_dispatcher;

    // Declared first so they outlive the network, which calls into the authority, which reads the engine.
    std::unique_ptr<HostEngine> m_pEngine;
    std::unique_ptr<HostAuthority> m_pAuthority;
    std::unique_ptr<HostNetwork> m_pNetwork;
    // The host's game in the party's conversation.
    std::unique_ptr<HostConversation> m_pConversation;

    std::vector<entt::scoped_connection> m_connections;
};
