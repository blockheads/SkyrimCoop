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
struct ServerSettings;
struct PlayerFormChangeEvent;


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
    void OnServerSettingsReceived(const ServerSettings& acSettings) noexcept;
    // The level reaches the others; a beast race respawns the player's copies (BeastFormChangeEvent).
    void OnPlayerFormChange(const PlayerFormChangeEvent& acEvent) noexcept;

private:
    /**
     * @brief Run the respawn timer, and if it hits 0, respawn the player.
     */
    void RunRespawnUpdates(const double acDeltaTime) noexcept;
    void RunPostDeathUpdates(const double acDeltaTime) noexcept;
    /**
     * @brief Make sure difficulty doesn't get changed while connected
     */
    void RunDifficultyUpdates() const noexcept;

    void ToggleDeathSystem(bool aSet) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IPlayerBridge& m_playerBridge;
    IFormBridge& m_formBridge;
    IActorBridge& m_actorBridge;

    double m_respawnTimer = 0.0;
    bool m_respawnStarted = false; // the player is down and its respawn timer runs
    // The host's difficulty while in a session (a friend's game), and the game's own from before it.
    std::optional<uint32_t> m_serverDifficulty;
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

    std::vector<entt::scoped_connection> m_connections;
};
