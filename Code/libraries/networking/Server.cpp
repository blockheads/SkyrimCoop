#include "Server.hpp"
#include "ENetInterface.hpp"
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

namespace TiltedPhoques
{
    Server::Server() noexcept
        : m_pHost(nullptr)
        , m_tickRate(10)
        , m_port(0)
        , m_lastUpdateTime(0ns)
        , m_timeBetweenUpdates(100ms)
        , m_lastClockSyncTime(0ns)
    {
        ENetInterface::Acquire();
    }

    Server::~Server()
    {
        Close();
        ENetInterface::Release();
    }

    bool Server::Host(const uint16_t aPort, uint32_t aTickRate, bool bEnableDualStackIP) noexcept
    {
        Close();

        ENetAddress address;

        // Build the "any" address for the desired IP version
        // Use IPv6 if dual stack is enabled, otherwise IPv4
        ENetAddressType addressType = bEnableDualStackIP ? ENET_ADDRESS_TYPE_IPV6 : ENET_ADDRESS_TYPE_IPV4;
        enet_address_build_any(&address, addressType);
        address.port = aPort;

        // Create server host
        // enet_host_create(type, address, peerCount, channelLimit, incomingBandwidth, outgoingBandwidth)
        m_pHost = enet_host_create(
            addressType,
            &address,
            128,  // Max 128 clients (kMaxPlayerCount from original code)
            2,    // 2 channels (0 = reliable, 1 = unreliable)
            0,    // Unlimited incoming bandwidth
            0     // Unlimited outgoing bandwidth
        );

        if (m_pHost == nullptr)
        {
            spdlog::error("[SkyrimCoopNetworking] SERVER: Failed to create host on port {}", aPort);
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

        spdlog::info("[SkyrimCoopNetworking] SERVER: Listening on port {} (tick rate: {} Hz)", aPort, m_tickRate);
        return IsListening();
    }

    void Server::Close() noexcept
    {
        if (m_pHost != nullptr)
        {
            // Disconnect all peers
            for (auto& [id, peer] : m_peers)
            {
                enet_peer_disconnect(peer, static_cast<enet_uint32>(EDisconnectReason::Quit));
            }

            // Allow disconnection packets to be sent
            ENetEvent event;
            enet_host_service(m_pHost, &event, 100);

            m_peers.clear();

            enet_host_destroy(m_pHost);
            m_pHost = nullptr;
            m_port = 0;
        }
    }

    void Server::Update() noexcept
    {
        m_currentTick = high_resolution_clock::now();

        if (IsListening())
        {
            // Process ENet events
            ENetEvent event;
            while (enet_host_service(m_pHost, &event, 0) > 0)
            {
                switch (event.type)
                {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    ConnectionId_t connectionId = reinterpret_cast<ConnectionId_t>(event.peer);
                    m_peers[connectionId] = event.peer;

                    spdlog::info("[SkyrimCoopNetworking] SERVER: Client {} connected", connectionId);

                    // Send initial clock sync
                    SynchronizeClientClocks(connectionId);

                    OnConnection(connectionId);
                    break;
                }

                case ENET_EVENT_TYPE_RECEIVE:
                {
                    ConnectionId_t connectionId = reinterpret_cast<ConnectionId_t>(event.peer);

                    HandleMessage(event.packet->data, static_cast<uint32_t>(event.packet->dataLength), connectionId);

                    enet_packet_destroy(event.packet);
                    break;
                }

                case ENET_EVENT_TYPE_DISCONNECT:
                {
                    ConnectionId_t connectionId = reinterpret_cast<ConnectionId_t>(event.peer);

                    spdlog::info("[SkyrimCoopNetworking] SERVER: Client {} disconnected (reason: {})", connectionId, event.data);

                    Remove(connectionId);

                    // Map enet disconnect code to EDisconnectReason
                    EDisconnectReason reason = Quit;
                    switch (event.data)
                    {
                    case 0: reason = Quit; break;
                    case 1: reason = Kicked; break;
                    case 2: reason = Banned; break;
                    default: reason = TimedOut; break;
                    }

                    OnDisconnection(connectionId, reason);
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
        for (const auto& [id, peer] : m_peers)
        {
            Send(id, apPacket, aPacketFlags);
        }
    }

    void Server::Send(const ConnectionId_t aConnectionId, Packet* apPacket, EPacketFlags aPacketFlags) const noexcept
    {
        auto it = m_peers.find(aConnectionId);
        if (it == m_peers.end())
            return;

        ENetPeer* peer = it->second;

        // Apply snappy compression for payload packets
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

        // Map packet flags to ENet flags
        enet_uint32 flags = 0;
        uint8_t channel = 0;

        switch (aPacketFlags)
        {
        case kReliable:
        case kReliableNoNagle:
            flags = ENET_PACKET_FLAG_RELIABLE;
            channel = 0;  // Reliable channel
            break;
        case kUnreliable:
        case kUnreliableNoDelay:
            flags = 0;  // Unreliable
            channel = 1;  // Unreliable channel
            break;
        }

        ENetPacket* packet = enet_packet_create(apPacket->m_pData, apPacket->m_size, flags);
        if (packet == nullptr)
        {
            spdlog::error("[SkyrimCoopNetworking] SERVER: Failed to create packet");
            return;
        }

        if (enet_peer_send(peer, channel, packet) < 0)
        {
            spdlog::error("[SkyrimCoopNetworking] SERVER: Failed to send packet to client {}", aConnectionId);
            enet_packet_destroy(packet);
            return;
        }

        // Flush immediately for NoDelay packets
        if (aPacketFlags == kUnreliableNoDelay || aPacketFlags == kReliableNoNagle)
        {
            enet_host_flush(m_pHost);
        }
    }

    void Server::Kick(const ConnectionId_t aConnectionId) noexcept
    {
        auto it = m_peers.find(aConnectionId);
        if (it == m_peers.end())
            return;

        spdlog::info("[SkyrimCoopNetworking] SERVER: Kicking client {}", aConnectionId);

        enet_peer_disconnect(it->second, static_cast<enet_uint32>(EDisconnectReason::Kicked));
        enet_host_flush(m_pHost);

        Remove(aConnectionId);
        OnDisconnection(aConnectionId, EDisconnectReason::Kicked);
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

    bool Server::IsAlive(ConnectionId_t aConnectionId) const noexcept
    {
        return m_peers.find(aConnectionId) != m_peers.end();
    }

    ENetPeer* Server::GetPeer(ConnectionId_t aConnectionId) const noexcept
    {
        auto it = m_peers.find(aConnectionId);
        if (it != m_peers.end())
            return it->second;
        return nullptr;
    }

    void Server::Remove(const ConnectionId_t aId) noexcept
    {
        m_peers.erase(aId);
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
            spdlog::warn("[SkyrimCoopNetworking] SERVER: Unknown opcode from client {}: {}", aConnectionId, pData[0]);
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

        // Write big-endian uint64_t
        writer.WriteBits((time >> 56) & 0xFF, 8);
        writer.WriteBits((time >> 48) & 0xFF, 8);
        writer.WriteBits((time >> 40) & 0xFF, 8);
        writer.WriteBits((time >> 32) & 0xFF, 8);
        writer.WriteBits((time >> 24) & 0xFF, 8);
        writer.WriteBits((time >> 16) & 0xFF, 8);
        writer.WriteBits((time >> 8) & 0xFF, 8);
        writer.WriteBits(time & 0xFF, 8);

        if(aSpecificConnection != 0)
        {
            auto it = m_peers.find(aSpecificConnection);
            if (it != m_peers.end())
            {
                // In this case we probably want it to arrive so send it reliably
                ENetPacket* packet = enet_packet_create(pBuffer->GetData(), writer.Size() & 0xFFFFFFFF, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(it->second, 0, packet);
                enet_host_flush(m_pHost);
            }
        }
        else
        {
            // Broadcast unreliable clock sync to all clients
            for (const auto& [id, peer] : m_peers)
            {
                ENetPacket* packet = enet_packet_create(pBuffer->GetData(), writer.Size() & 0xFFFFFFFF, 0);
                enet_peer_send(peer, 1, packet);
            }
            enet_host_flush(m_pHost);
        }
    }
}
