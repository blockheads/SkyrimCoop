#pragma once

#include <Structs/ScriptValue.h>

#include <memory>
#include <NoCopyMove.h>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;
struct UpdateEvent;
struct NotifyPlayerNative;
struct WorldCallEvent;

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
    // A native on the player the host's script called for this game's player: runs here.
    void OnPlayerNative(const NotifyPlayerNative& acMessage) noexcept;
    // What it returned, to the host's script waiting on it (None also when it did not run here).
    void SendNativeResult(uint32_t aReplyId, const LocalValue& acResult) noexcept;
    // This game's own script changed the world (a quest's stage): the host's game makes the call.
    void OnWorldCall(const WorldCallEvent& acEvent) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    std::unique_ptr<Guest> m_pGuest;

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerNativeConnection;
    entt::scoped_connection m_worldCallConnection;
};
