#pragma once

struct World;
struct DisconnectedEvent;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct NotifyPlayerLevel;

/**
 * @brief The other players in the session. Everyone connected is in it (there are no parties);
 * the host is the authority, and alone may remove a player (HostService::KickPlayer).
 */
struct SessionService
{
    struct Player
    {
        std::string Name;
        bool IsHost{};
        // Its character's level.
        uint16_t Level{};
    };

    SessionService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~SessionService() = default;

    NO_COPY_MOVE(SessionService);

    // By player id.
    [[nodiscard]] const std::unordered_map<uint32_t, Player>& GetPlayers() const noexcept { return m_players; }

protected:
    void OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

private:
    std::unordered_map<uint32_t, Player> m_players;

    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_disconnectedConnection;
};
