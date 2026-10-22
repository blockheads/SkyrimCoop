#pragma once

#include <memory>
#include <NoCopyMove.h>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;
struct UpdateEvent;

/**
 * @brief This game as a guest in someone's session: the host's world applied here (Services/Guest/). The
 * parts exist from joining until leaving, never on the host (HostService is its side). They come and go inside the
 * frame's update (the transport's), so they subscribe to no update themselves: this passes each one on.
 */
struct GuestService
{
    GuestService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~GuestService() noexcept;

    NO_COPY_MOVE(GuestService);

    [[nodiscard]] bool IsGuest() const noexcept { return m_pGuest != nullptr; }

private:
    struct Guest;

    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    std::unique_ptr<Guest> m_pGuest;

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_updateConnection;
};
