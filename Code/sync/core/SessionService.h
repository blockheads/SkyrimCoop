#pragma once

#include <Messages/RelayedMessages.h>

#include <core/Listen.h>
#include <NoCopyMove.h>

#include <functional>
#include <string>

struct World;
struct DisconnectedEvent;
struct MenuEvent;
struct UpdateEvent;
struct ConnectedEvent;
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

    // The native quit callback is already on the main thread; network exits finish next update.
    void QuitToMainMenu() noexcept;
    void End(std::string aReason = {}) noexcept;
    [[nodiscard]] bool IsEnding() const noexcept { return m_ending; }
    // What ending a session runs, and whether a join is under way: the game's wiring sets them (SessionEnd.h).
    struct Ending
    {
        std::function<bool()> IsJoining;
        std::function<void(const std::string& acReason, bool aReturnToMenu)> Finish;
    };
    void SetEnding(Ending aEnding) noexcept { m_end = std::move(aEnding); }

    // By player id.
    [[nodiscard]] const std::unordered_map<uint32_t, Player>& GetPlayers() const noexcept { return m_players; }
    // A player's name, this game's own too ("?" for one not in the session).
    [[nodiscard]] std::string NameOf(uint32_t aPlayerId) const noexcept;

protected:
    void OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept;
    void OnPlayerName(const NotifyPlayerName& acMessage) noexcept;
    void OnPlayerPerished(const NotifyPlayerPerished& acMessage) const noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

private:
    void OnMenu(const MenuEvent&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnConnected(const ConnectedEvent&) noexcept;
    void Finish(bool aReturnToMenu) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    bool m_ending{};
    bool m_finished{};
    bool m_pending{};
    std::string m_endReason;
    Ending m_end;
    std::unordered_map<uint32_t, Player> m_players;

    std::vector<entt::scoped_connection> m_connections;
};
