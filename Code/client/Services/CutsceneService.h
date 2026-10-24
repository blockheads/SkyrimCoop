#pragma once

#include <Events/Listen.h>
#include <NoCopyMove.h>
#include <Messages/RelayedMessages.h>

struct World;
struct TransportService;
struct ConnectedEvent;
struct DisconnectedEvent;
struct PartyNativeEvent;
struct NotifyPlayerLeft;

/**
 * @brief Cutscenes for the whole party (docs/design/cutscenes.md): the calls on the controls and the screen this game's
 * scripts made (fades, camera, shakes, music; PartyNatives) go to the other games, and theirs run here, on this
 * game's player.
 */
struct CutsceneService
{
    CutsceneService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~CutsceneService() noexcept = default;

    NO_COPY_MOVE(CutsceneService);

protected:
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPartyNative(const PartyNativeEvent& acEvent) noexcept;
    void OnNotifyPartyNative(const NotifyPartyNative& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;

private:
    World& m_world;
    TransportService& m_transport;

    std::vector<entt::scoped_connection> m_connections;
};
