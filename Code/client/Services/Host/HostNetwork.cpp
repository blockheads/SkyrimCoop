#include <Services/Host/HostNetwork.h>

#include <Messages/RequestMessageFactory.h>
#include <Messages/Message.h>
#include <network/Packet.h>

#include <spdlog/spdlog.h>

namespace
{
// Updates per second the listener syncs its clock and sends at.
constexpr uint32_t cTickRate = 60;
// Tries this many ports from the one asked for.
constexpr uint16_t cPortsToTry = 16;
} // namespace

bool HostNetwork::Listen(uint16_t aPort) noexcept
{
    for (uint16_t port = aPort; port < aPort + cPortsToTry; ++port)
    {
        if (Host(port, cTickRate))
            return true;
        spdlog::warn("HostNetwork: port {} is in use, trying {}", port, port + 1);
    }
    return false;
}

void HostNetwork::OpenRoute(ConnectionId_t aConnection, Inbox aInbox) noexcept
{
    if (m_routes.empty())
        m_hostConnection = aConnection;
    m_routes[aConnection] = std::move(aInbox);
    OnConnection(aConnection);
}

void HostNetwork::CloseRoute(ConnectionId_t aConnection) noexcept
{
    if (!m_routes.erase(aConnection))
        return;
    if (aConnection == m_hostConnection)
        m_hostConnection = 0;
    OnDisconnection(aConnection, EDisconnectReason::Quit);
}

void HostNetwork::Receive(const void* apData, uint32_t aSize, ConnectionId_t aConnection) noexcept
{
    Wire::Buffer::Reader reader(std::span(static_cast<const uint8_t*>(apData), aSize));
    const auto pMessage = RequestMessageFactory{}.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("HostNetwork: could not read a message from {:x}", aConnection);
        return;
    }
    if (!m_frontDoor || !m_frontDoor(*pMessage, aConnection))
        spdlog::debug("HostNetwork: dropped opcode {} from {:x}", uint32_t(pMessage->GetOpcode()), aConnection);
}

void HostNetwork::Send(ConnectionId_t aConnection, const NotifyMessage& acMessage, Net::EPacketFlags aFlags) const noexcept
{
    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    const auto cRoute = m_routes.find(aConnection);
    if (cRoute != m_routes.end())
    {
        acMessage.Serialize(writer);
        return cRoute->second(buffer.GetWriteData(), static_cast<uint32_t>(writer.Size()));
    }
    // An enet packet starts with a type byte, which the packet fills in.
    writer.WriteBits(0, 8);
    acMessage.Serialize(writer);
    Net::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
    Server::Send(aConnection, &packet, aFlags);
}

void HostNetwork::OnConnection(ConnectionId_t aConnection)
{
    spdlog::info("HostNetwork: connection {:x}", aConnection);
}

void HostNetwork::OnDisconnection(ConnectionId_t aConnection, EDisconnectReason aReason)
{
    spdlog::info("HostNetwork: connection {:x} ended ({})", aConnection, int(aReason));
    if (m_disconnected)
        m_disconnected(aConnection);
}
