#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>

struct World;
struct DisconnectedEvent;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct NotifyPlayerLevel;
struct NotifyPlayerPerished;

/**
 * @brief The other players in the session. Everyone connected is in it (there are no parties);
 * the host is the authority, and alone may remove a player (HostService::KickPlayer). Who joins, leaves or goes
 * down is a line in the chat (SessionNoticeEvent).
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
    void OnPlayerPerished(const NotifyPlayerPerished& acMessage) const noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

private:
    entt::dispatcher& m_dispatcher;
    std::unordered_map<uint32_t, Player> m_players;

    std::vector<entt::scoped_connection> m_connections;
};
