#pragma once

#include <core/Listen.h>
#include <Messages/RelayedMessages.h>
#include <NoCopyMove.h>
#include <tools/DevCommands.h>

#include <string>

struct World;
struct TransportService;
class IDevToolsBridge;

/**
 * @brief The dev chat commands (docs/design/dev-commands.md): a typed one goes to the host, which runs a world
 * command and sends the line to every game; each runs a player command on its own player. Alone, all of it here.
 */
struct DevToolsService
{
    DevToolsService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport,
                    IDevToolsBridge& aBridge) noexcept;
    ~DevToolsService() noexcept = default;

    NO_COPY_MOVE(DevToolsService);

    // A line from the chat that IsDevCommand named.
    void Run(const std::string& acLine) noexcept;

  protected:
    void OnNotifyDevCommand(const NotifyDevCommand& acMessage) noexcept;

  private:
    void Apply(const DevCommands::Command& acCommand) noexcept;

    World& m_world;
    TransportService& m_transport;
    IDevToolsBridge& m_bridge;

    std::vector<entt::scoped_connection> m_connections;
};
