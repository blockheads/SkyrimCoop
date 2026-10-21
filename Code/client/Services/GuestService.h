#pragma once

#include <memory>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;

/**
 * @brief This game as a guest in someone's session: the host's world applied here (Services/Guest/). The
 * parts exist from joining until leaving, never on the host (HostService is its side).
 */
struct GuestService
{
    GuestService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~GuestService() noexcept;

    TP_NOCOPYMOVE(GuestService);

    [[nodiscard]] bool IsGuest() const noexcept { return m_pGuest != nullptr; }

private:
    struct Guest;

    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    std::unique_ptr<Guest> m_pGuest;

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
};
