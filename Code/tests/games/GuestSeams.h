#pragma once

#include <Messages/RelayedMessages.h>

#include <entt/entt.hpp>
#include <Messages/RelayedMessages.h>

#include <memory>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;

// GuestService's part here: a game joining as a guest gets its guest parts (GuestWorldState on GuestEngine's seam),
// leaving drops them.
struct GuestSeam
{
    explicit GuestSeam(World& aWorld) noexcept;
    ~GuestSeam() noexcept;

    // Each frame, as GuestService::OnUpdate.
    void Update() noexcept;

  private:
    struct Guest;

    void OnConnected(const ConnectedEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    // As GuestService::OnPlayerNative: the host's script's call for this game's player runs here.
    void OnPlayerNative(const NotifyRoutedCall& acMessage) noexcept;

    World& m_world;
    std::unique_ptr<Guest> m_pGuest;
    entt::scoped_connection m_connected;
    entt::scoped_connection m_disconnected;
    entt::scoped_connection m_playerNative;
};
