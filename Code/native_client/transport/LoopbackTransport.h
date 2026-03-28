#pragma once

#include <cstdint>
#include <functional>
#include <TiltedCore/Meta.hpp>

struct ClientMessage;
namespace Server { struct GameServer; }

struct LoopbackTransport
{
    TP_NOCOPYMOVE(LoopbackTransport);

    // Match TiltedPhoques::ConnectionId_t = uintptr_t
    using ConnectionId_t = uintptr_t;

    // Reserved ConnectionId for host player -- must not collide with enet6 peer handles.
    // enet6 assigns ConnectionId from peer pointer (small integers). Use a large constant.
    static constexpr ConnectionId_t kHostConnectionId = 0xFFFF'FFFF'0000'0001ULL;

    explicit LoopbackTransport(Server::GameServer& aServer);

    // Serialize a ClientMessage and inject into GameServer::OnConsume
    void SendToServer(const ClientMessage& acMessage);

    // Callback type for receiving ServerMessages from the GameServer
    using ServerMessageCallback = std::function<void(const void*, uint32_t)>;
    void SetServerMessageCallback(ServerMessageCallback aCallback);

    // Called by GameServer::Send when target is kHostConnectionId
    void DeliverToHost(const void* apData, uint32_t aSize);

private:
    Server::GameServer& m_server;
    ServerMessageCallback m_onServerMessage;
};
