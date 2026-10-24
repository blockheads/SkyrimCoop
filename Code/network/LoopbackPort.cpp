#include "LoopbackPort.h"

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <winsock2.h>
using SocketLength = int;
#else
#include <netinet/in.h>
#include <sys/socket.h>
using SocketLength = socklen_t;
#endif

namespace Net
{
LoopbackPort::LoopbackPort() noexcept
    : m_socket(ENET_SOCKET_NULL)
{
    ENetInterface::Acquire();
}

LoopbackPort::~LoopbackPort()
{
    Close();
    ENetInterface::Release();
}

bool LoopbackPort::Open() noexcept
{
    Close();
    m_socket = enet_socket_create(ENET_ADDRESS_TYPE_IPV4, ENET_SOCKET_TYPE_DATAGRAM);
    if (m_socket == ENET_SOCKET_NULL)
    {
        spdlog::error("LoopbackPort: no socket");
        return false;
    }
    ENetAddress address{};
    enet_address_build_loopback(&address, ENET_ADDRESS_TYPE_IPV4);
    address.port = 0;
    // The port bound, from the system: enet6's enet_socket_get_address fails for an IPv4 socket on Windows.
    sockaddr_in bound{};
    SocketLength length = sizeof(bound);
    const char* pFailed = enet_socket_bind(m_socket, &address) < 0                                          ? "bind"
                          : getsockname(m_socket, reinterpret_cast<sockaddr*>(&bound), &length) != 0        ? "getsockname"
                          : enet_socket_set_option(m_socket, ENET_SOCKOPT_NONBLOCK, 1) < 0                  ? "nonblock"
                                                                                                            : nullptr;
    if (pFailed)
    {
        spdlog::error("LoopbackPort: {} failed", pFailed);
        Close();
        return false;
    }
    m_port = ntohs(bound.sin_port);
    return true;
}

void LoopbackPort::Close() noexcept
{
    if (m_socket != ENET_SOCKET_NULL)
        enet_socket_destroy(m_socket);
    m_socket = ENET_SOCKET_NULL;
    m_port = 0;
}

std::vector<LoopbackPort::Datagram> LoopbackPort::Receive() noexcept
{
    std::vector<Datagram> datagrams;
    if (m_socket == ENET_SOCKET_NULL)
        return datagrams;
    uint8_t data[ENET_PROTOCOL_MAXIMUM_MTU];
    while (true)
    {
        ENetAddress from{};
        ENetBuffer buffer{};
        buffer.data = data;
        buffer.dataLength = sizeof(data);
        const int cReceived = enet_socket_receive(m_socket, &from, &buffer, 1);
        if (cReceived <= 0)
            break;
        datagrams.push_back({from.port, std::vector<uint8_t>(data, data + cReceived)});
    }
    return datagrams;
}

bool LoopbackPort::SendTo(uint16_t aPort, const void* apData, size_t aSize) noexcept
{
    if (m_socket == ENET_SOCKET_NULL)
        return false;
    ENetAddress to{};
    enet_address_build_loopback(&to, ENET_ADDRESS_TYPE_IPV4);
    to.port = aPort;
    ENetBuffer buffer{};
    buffer.data = const_cast<void*>(apData);
    buffer.dataLength = aSize;
    return enet_socket_send(m_socket, &to, &buffer, 1) >= 0;
}
} // namespace Net
