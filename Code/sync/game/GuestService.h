#pragma once

#include <core/Listen.h>
#include <Structs/ScriptValue.h>

#include <functional>
#include <memory>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct World;
struct ConnectedEvent;
struct DisconnectedEvent;
struct UpdateEvent;
class IGuestEngine;

/**
 * @brief This game as a guest in someone's session: the host's world applied here (the Guest* files). The
 * parts exist from joining until leaving, never on the host (HostService is its side). They come and go inside the
 * frame's update (the transport's), so they subscribe to no update themselves: this passes each one on.
 */
struct GuestService
{
    // Makes the engine side of a guest's game as it joins (the engine's GuestEngine; the tests' fake).
    using MakeEngine_t = std::function<std::unique_ptr<IGuestEngine>()>;
    GuestService(World& aWorld, entt::dispatcher& aDispatcher, MakeEngine_t aMakeEngine) noexcept;
    ~GuestService() noexcept;

    NO_COPY_MOVE(GuestService);

    [[nodiscard]] bool IsGuest() const noexcept { return m_pGuest != nullptr; }

private:
    struct Guest;

    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    // A native on the player the host's script called for this game's player: runs here.
    void OnPlayerNative(const NotifyRoutedCall& acMessage) noexcept;
    // What it returned, to the host's script waiting on it (None also when it did not run here).
    void SendNativeResult(uint32_t aReplyId, const LocalValue& acResult) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    MakeEngine_t m_makeEngine;
    std::unique_ptr<Guest> m_pGuest;

    std::vector<entt::scoped_connection> m_connections;
};
