#pragma once

#include <Events/Listen.h>
#include <optional>
#include <NoCopyMove.h>

class IPlayerBridge;
class IFormBridge;
class IActorBridge;

struct World;
struct TransportService;

struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct NotifySettingsChange;
struct DifficultyChangeEvent;
struct PlayerFormChangeEvent;
struct MenuEvent;


/**
 * @brief Handles logic related to the local player.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                  IPlayerBridge& aPlayerBridge, IFormBridge& aFormBridge, IActorBridge& aActorBridge) noexcept;
    ~PlayerService() noexcept = default;

    NO_COPY_MOVE(PlayerService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnDifficultyChange(const NotifySettingsChange& acMessage) noexcept;
    // A guest holds the host's difficulty even if its settings menu changes it.
    void OnDifficultySet(const DifficultyChangeEvent& acEvent) const noexcept;
    // The level reaches the others; a new race respawns the player's copies (PlayerLooksChangeEvent).
    void OnPlayerFormChange(const PlayerFormChangeEvent& acEvent) noexcept;
    // The race menu closing respawns them once, for all it changed.
    void OnMenu(const MenuEvent& acEvent) noexcept;

private:
    /**
     * @brief Run the respawn timer, and if it hits 0, respawn the player.
     */
    void RunRespawnUpdates(const double acDeltaTime) noexcept;
    void RunPostDeathUpdates(const double acDeltaTime) noexcept;

    void ToggleDeathSystem(bool aSet) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IPlayerBridge& m_playerBridge;
    IFormBridge& m_formBridge;
    IActorBridge& m_actorBridge;

    double m_respawnTimer = 0.0;
    bool m_respawnStarted = false; // the player is down and its respawn timer runs
    // The host's difficulty while joined, and this game's own from before joining.
    std::optional<uint32_t> m_hostDifficulty;
    std::optional<uint32_t> m_previousDifficulty;

    bool m_isDeathSystemEnabled = true;

    bool m_knockdownStart = false;
    double m_knockdownTimer = 0.0;

    bool m_godmodeStart = false;
    double m_godmodeTimer = 0.0;

    uint32_t m_cachedMainSpellId = 0;
    uint32_t m_cachedSecondarySpellId = 0;
    uint32_t m_cachedPowerId = 0;

    uint16_t m_lastLevel = 0;
    uint32_t m_lastRaceFormId = 0;
    bool m_inRaceMenu = false;

    std::vector<entt::scoped_connection> m_connections;
};
