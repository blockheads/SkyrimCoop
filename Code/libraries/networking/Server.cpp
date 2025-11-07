#include "Server.hpp"
#include "SteamInterface.hpp"
#include <thread>
#include <algorithm>
#include <Buffer.hpp>
#include <StackAllocator.hpp>

#include <cassert>
#include <Packet.hpp>
#include <bit>
#include <snappy.h>
#include <spdlog/spdlog.h>

using namespace std::chrono;

// Helper function for network byte order conversion (big endian)
namespace
{
    inline uint64_t ToBigEndian64(uint64_t value)
    {
        if constexpr (std::endian::native == std::endian::little)
        {
            return ((value & 0x00000000000000FFULL) << 56) |
                   ((value & 0x000000000000FF00ULL) << 40) |
                   ((value & 0x0000000000FF0000ULL) << 24) |
                   ((value & 0x00000000FF000000ULL) << 8)  |
                   ((value & 0x000000FF00000000ULL) >> 8)  |
                   ((value & 0x0000FF0000000000ULL) >> 24) |
                   ((value & 0x00FF000000000000ULL) >> 40) |
                   ((value & 0xFF00000000000000ULL) >> 56);
        }
        else
        {
            return value;
        }
    }
}

namespace TiltedPhoques
{
    Server::Server() noexcept
        : m_pHost(nullptr)
        , m_port(0)
        , m_tickRate(10)
        , m_lastUpdateTime(0ns)
        , m_timeBetweenUpdates(100ms)
        , m_lastClockSyncTime(0ns)
    {
        SteamInterface::Acquire();

        // Initialize ENet
        if (enet_initialize() != 0)
        {
            spdlog::error("[TiltedConnect] Failed to initialize ENet");
        }
    }

    Server::~Server()
    {
        Close();
        enet_deinitialize();
        SteamInterface::Release();
    }

    bool Server::Host(const uint16_t aPort, uint32_t aTickRate, bool bEnableDualStackIP) noexcept
    {
        Close();

        ENetAddress address;
        if (bEnableDualStackIP)
        {
            // Dual-stack IPv4/IPv6
            enet_address_build_any(&address, ENET_ADDRESS_TYPE_IPV6);
        }
        else
        {
            // IPv4 only
            enet_address_build_any(&address, ENET_ADDRESS_TYPE_IPV4);
        }
        address.port = aPort;

        // Create host with dual-stack support
        // Parameters: address type, bind address, max peers, channel count, incoming bandwidth, outgoing bandwidth
        m_pHost = enet_host_create(
            bEnableDualStackIP ? ENET_ADDRESS_TYPE_IPV6 : ENET_ADDRESS_TYPE_IPV4,
            &address,
            32,  // max peers
            2,   // channel count (0 = reliable, 1 = unreliable)
            0,   // incoming bandwidth (0 = unlimited)
            0    // outgoing bandwidth (0 = unlimited)
        );

        if (!m_pHost)
        {
            spdlog::error("[TiltedConnect] Failed to create ENet server host on port {}", aPort);
            return false;
        }

        m_port = aPort;

        if (m_tickRate == 0 && aTickRate == 0)
        {
            aTickRate = 10;
        }
        // If we pass 0, reuse the previously used tick rate
        else if (aTickRate == 0)
        {
            aTickRate = m_tickRate;
        }

        m_tickRate = aTickRate;

        // update time in MS
        m_timeBetweenUpdates = 1000ms / m_tickRate;

        spdlog::info("[TiltedConnect] Server listening on port {} (tick rate: {})", aPort, m_tickRate);

        return IsListening();
    }

    void Server::Close() noexcept
    {
        if (m_pHost)
        {
            // Disconnect all peers
            for (auto& [connId, peer] : m_peers)
            {
                enet_peer_disconnect(peer, 0);
            }

            // Flush all packets
            enet_host_flush(m_pHost);

            // Destroy the host
            enet_host_destroy(m_pHost);
            m_pHost = nullptr;
        }

        m_peers.clear();
        m_port = 0;
    }

    void Server::Update() noexcept
    {
        m_currentTick = high_resolution_clock::now();

        if (IsListening())
        {
            ENetEvent event;
            // Service the host with 0 timeout for non-blocking
            while (enet_host_service(m_pHost, &event, 0) > 0)
            {
                switch (event.type)
                {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    const ConnectionId_t connId = static_cast<ConnectionId_t>(reinterpret_cast<uintptr_t>(event.peer));
                    m_peers[connId] = event.peer;

                    // Store connection ID in peer data
                    event.peer->data = reinterpret_cast<void*>(static_cast<uintptr_t>(connId));

                    char buffer[64];
                    enet_address_get_host_ip(&event.peer->address, buffer, sizeof(buffer));
                    spdlog::info("[TiltedConnect] Client connected from {}:{} (ID: {:x})",
                                buffer, event.peer->address.port, connId);

                    SynchronizeClientClocks(connId);
                    OnConnection(connId);
                    break;
                }

                case ENET_EVENT_TYPE_RECEIVE:
                {
                    const ConnectionId_t connId = static_cast<ConnectionId_t>(reinterpret_cast<uintptr_t>(event.peer));
                    HandleMessage(event.packet->data, event.packet->dataLength, connId);
                    enet_packet_destroy(event.packet);
                    break;
                }

                case ENET_EVENT_TYPE_DISCONNECT:
                case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                {
                    const ConnectionId_t connId = static_cast<ConnectionId_t>(reinterpret_cast<uintptr_t>(event.peer));

                    char buffer[64];
                    enet_address_get_host_ip(&event.peer->address, buffer, sizeof(buffer));
                    spdlog::info("[TiltedConnect] Client disconnected: {} (ID: {:x}, reason: {})",
                                buffer, connId,
                                event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT ? "timeout" : "normal");

                    auto it = m_peers.find(connId);
                    if (it != m_peers.end())
                    {
                        m_peers.erase(it);
                    }

                    const auto reason = event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT
                        ? EDisconnectReason::TimedOut
                        : EDisconnectReason::Quit;

                    OnDisconnection(connId, reason);
                    event.peer->data = nullptr;
                    break;
                }

                case ENET_EVENT_TYPE_NONE:
                    break;
                }
            }
        }

        // Sync clocks every 10 seconds
        if (m_currentTick - m_lastClockSyncTime >= 10s)
        {
            m_lastClockSyncTime = m_currentTick;
            SynchronizeClientClocks();
        }

        if (m_currentTick - m_lastUpdateTime >= m_timeBetweenUpdates)
        {
            m_lastUpdateTime = m_currentTick;
            OnUpdate();
        }

        std::this_thread::sleep_for(2ms);
    }

    void Server::SendToAll(Packet* apPacket, const EPacketFlags aPacketFlags) noexcept
    {
        for (const auto& [connId, peer] : m_peers)
        {
            Send(connId, apPacket, aPacketFlags);
        }
    }

    void Server::Send(const ConnectionId_t aConnectionId, Packet* apPacket, EPacketFlags aPacketFlags) const noexcept
    {
        auto it = m_peers.find(aConnectionId);
        if (it == m_peers.end())
        {
            return;
        }

        ENetPeer* peer = it->second;

        if (apPacket->m_pData[0] == kPayload)
        {
            std::string data;
            snappy::Compress(apPacket->GetData(), apPacket->GetSize(), &data);

            if (data.size() < apPacket->GetSize())
            {
                apPacket->m_pData[0] = kCompressedPayload;
                std::copy(std::begin(data), std::end(data), apPacket->GetData());
                apPacket->m_size = (data.size() + 1) & 0xFFFFFFFF;
            }
        }

        // Create ENet packet
        const enet_uint32 flags = (aPacketFlags == kReliable) ? ENET_PACKET_FLAG_RELIABLE : 0;
        ENetPacket* packet = enet_packet_create(apPacket->m_pData, apPacket->m_size, flags);

        if (packet)
        {
            // Send on channel 0
            enet_peer_send(peer, 0, packet);
        }
    }

    void Server::Kick(const ConnectionId_t aConnectionId) noexcept
    {
        auto it = m_peers.find(aConnectionId);
        if (it != m_peers.end())
        {
            enet_peer_disconnect(it->second, 0);
            m_peers.erase(it);
            OnDisconnection(aConnectionId, EDisconnectReason::Kicked);
        }
    }

    uint16_t Server::GetPort() const noexcept
    {
        return m_port;
    }

    bool Server::IsListening() const noexcept
    {
        return m_pHost != nullptr;
    }

    uint32_t Server::GetClientCount() const noexcept
    {
        return m_peers.size() & 0xFFFFFFFF;
    }

    uint32_t Server::GetTickRate() const noexcept
    {
        return m_tickRate;
    }

	uint64_t Server::GetTick() const noexcept
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(m_currentTick.time_since_epoch()).count();
	}

    String Server::GetConnectionAddress(ConnectionId_t aConnectionId) const noexcept
    {
        auto it = m_peers.find(aConnectionId);
        if (it == m_peers.end() || !it->second)
            return "unknown";

        ENetPeer* peer = it->second;
        char buffer[64];
        enet_address_get_host_ip(&peer->address, buffer, sizeof(buffer));
        return String(buffer);
    }

    bool Server::IsAlive(ConnectionId_t aConnectionId) const noexcept
    {
        return m_peers.find(aConnectionId) != m_peers.end();
    }

    void Server::HandleMessage(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId) noexcept
    {
        // We handle the cases where packets target the current stack or the user stack
        if (aSize == 0)
            return;

        const auto pData = static_cast<const uint8_t*>(apData);
        switch (pData[0])
        {
        case kPayload:
            OnConsume(pData + 1, aSize - 1, aConnectionId);
            break;
        case kCompressedPayload:
            HandleCompressedPayload(pData + 1, aSize - 1, aConnectionId);
            break;
        default:
            assert(false);
            break;
        }
    }

    void Server::HandleCompressedPayload(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId) noexcept
    {
        std::string data;
        snappy::Uncompress((const char*)apData, aSize, &data);

        if (!data.empty())
        {
            OnConsume(data.data(), data.size() & 0xFFFFFFFF, aConnectionId);
        }
    }

    void Server::SynchronizeClientClocks(const ConnectionId_t aSpecificConnection) noexcept
    {
        const auto time = GetTick();

        StackAllocator<1 << 10> allocator;
        ScopedAllocator _{ &allocator };

        const auto pBuffer = New<Buffer>(512);

        Buffer::Writer writer(pBuffer);
        writer.WriteBits(kServerTime, 8);
        writer.WriteBits(ToBigEndian64(time), 64);

        // Create ENet packet
        ENetPacket* packet = enet_packet_create(pBuffer->GetData(), writer.Size() & 0xFFFFFFFF,
            aSpecificConnection != ENET_PEER_PACKET_LOSS_SCALE ? ENET_PACKET_FLAG_RELIABLE : 0);

        if (!packet)
            return;

        if (aSpecificConnection != ENET_PEER_PACKET_LOSS_SCALE)
        {
            // Send to specific connection
            auto it = m_peers.find(aSpecificConnection);
            if (it != m_peers.end())
            {
                enet_peer_send(it->second, 0, packet);
            }
            else
            {
                enet_packet_destroy(packet);
            }
        }
        else
        {
            // Broadcast to all connections
            for (const auto& [connId, peer] : m_peers)
            {
                // Need to clone packet for each peer except the last
                ENetPacket* clonedPacket = enet_packet_create(packet->data, packet->dataLength, 0);
                if (clonedPacket)
                {
                    enet_peer_send(peer, 0, clonedPacket);
                }
            }
            enet_packet_destroy(packet);
        }
    }
}
