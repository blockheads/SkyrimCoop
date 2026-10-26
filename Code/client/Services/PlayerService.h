#pragma once

#include <Events/Listen.h>
#include <optional>
#include <NoCopyMove.h>

class IPlayerBridge;
class IFormBridge;

struct World;
struct TransportService;

struct ConnectedEvent;
struct DisconnectedEvent;
struct NotifySettingsChange;
struct DifficultyChangeEvent;
struct PlayerFormChangeEvent;
struct MenuEvent;

/**
 * @brief This game's player's settings and looks in a session: the host's difficulty on a guest, its level and race
 * to the others. Going down and coming back is DeathService's.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport, IPlayerBridge& aPlayerBridge,
                  IFormBridge& aFormBridge) noexcept;
    ~PlayerService() noexcept = default;

    NO_COPY_MOVE(PlayerService);

protected:
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
    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    IPlayerBridge& m_playerBridge;
    IFormBridge& m_formBridge;

    // The host's difficulty while joined, and this game's own from before joining.
    std::optional<uint32_t> m_hostDifficulty;
    std::optional<uint32_t> m_previousDifficulty;

    uint16_t m_lastLevel = 0;
    uint32_t m_lastRaceFormId = 0;
    bool m_inRaceMenu = false;

    std::vector<entt::scoped_connection> m_connections;
};
