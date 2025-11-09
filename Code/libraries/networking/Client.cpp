#include <chrono>
#include "Client.hpp"
#include "ENetInterface.hpp"
#include <cassert>
#include <Buffer.hpp>
#include <Allocator.hpp>
#include <uv.h>
#include "Packet.hpp"
#include <snappy.h>
#include <spdlog/spdlog.h>
#include <cstring>

namespace TiltedPhoques
{
    Client::Client() noexcept
        : m_pHost(nullptr)
        , m_pPeer(nullptr)
        , m_connected(false)
    {
        ENetInterface::Acquire();

        // Create client host (no listening socket, just for outgoing connections)
        // enet_host_create(type, address, peerCount, channelLimit, incomingBandwidth, outgoingBandwidth)
        m_pHost = enet_host_create(
            ENET_ADDRESS_TYPE_ANY,  // Support both IPv4 and IPv6
            nullptr,                // No bind address (client mode)
            1,                      // Only 1 outgoing connection
            2,                      // 2 channels (0 = reliable, 1 = unreliable)
            0,                      // Unlimited incoming bandwidth
            0                       // Unlimited outgoing bandwidth
        );

        if (m_pHost == nullptr)
        {
            spdlog::critical("[SkyrimCoopNetworking] Failed to create ENet client host");
        }

        m_pLoop = Allocator::GetDefault()->Allocate(sizeof(uv_loop_t));
        auto* pLoop = static_cast<uv_loop_t*>(m_pLoop);
        uv_loop_init(pLoop);
        pLoop->data = this;
    }

    Client::~Client()
    {
        Close();

        if (m_pHost != nullptr)
        {
            enet_host_destroy(m_pHost);
            m_pHost = nullptr;
        }

        uv_loop_close(static_cast<uv_loop_t*>(m_pLoop));
        Allocator::Get()->Free(m_pLoop);
        ENetInterface::Release();
    }

    Client::Client(Client&& aRhs) noexcept
        : m_pHost(nullptr)
        , m_pPeer(nullptr)
        , m_connected(false)
    {
        ENetInterface::Acquire();
        this->operator=(std::move(aRhs));
    }

    Client& Client::operator=(Client&& aRhs) noexcept
    {
        std::swap(m_pHost, aRhs.m_pHost);
        std::swap(m_pPeer, aRhs.m_pPeer);
        std::swap(m_connected, aRhs.m_connected);
        std::swap(m_clock, aRhs.m_clock);

        return *this;
    }

    bool Client::Connect(const std::string& acEndpoint) noexcept
    {
        static auto GetAddrInfoCallback = [](uv_getaddrinfo_t* apHandle, int aStatus, struct addrinfo* apResult)
        {
            ENetAddress address;
            bool valid = false;

            auto* pClient = static_cast<Client*>(apHandle->loop->data);
            pClient->m_pHandle = nullptr;

            if (aStatus == 0)
            {
                switch (apResult->ai_family)
                {
                case AF_INET:
                {
                    const auto port = ntohs(reinterpret_cast<sockaddr_in*>(apResult->ai_addr)->sin_port);
                    const auto ip = ntohl(reinterpret_cast<sockaddr_in*>(apResult->ai_addr)->sin_addr.s_addr);

                    address.type = ENET_ADDRESS_TYPE_IPV4;
                    address.host.v4[0] = (ip >> 24) & 0xFF;
                    address.host.v4[1] = (ip >> 16) & 0xFF;
                    address.host.v4[2] = (ip >> 8) & 0xFF;
                    address.host.v4[3] = ip & 0xFF;
                    address.port = port;
                    valid = true;
                } break;
                case AF_INET6:
                {
                    // enet6 supports IPv6
                    const auto port = ntohs(reinterpret_cast<sockaddr_in6*>(apResult->ai_addr)->sin6_port);
                    address.type = ENET_ADDRESS_TYPE_IPV6;
                    std::memcpy(&address.host.v6, &reinterpret_cast<sockaddr_in6*>(apResult->ai_addr)->sin6_addr, 16);
                    address.port = port;
                    valid = true;
                } break;
                }
            }

            uv_freeaddrinfo(apResult);
            Allocator::GetDefault()->Free(apHandle);

            if(aStatus == UV_ECANCELED)
            {
                pClient->OnDisconnected(kAborted);
                return;
            }

            if (!valid)
            {
                pClient->OnDisconnected(kCannotResolve);
                return;
            }

            // Connect to server
            pClient->m_pPeer = enet_host_connect(pClient->m_pHost, &address, 2, 0);
            if (pClient->m_pPeer == nullptr)
            {
                spdlog::error("[SkyrimCoopNetworking] CLIENT: Failed to create peer for connection");
                pClient->OnDisconnected(kLocalProblem);
            }
            else
            {
                spdlog::debug("[SkyrimCoopNetworking] CLIENT: Connecting to server...");
            }
        };

        const auto pos = acEndpoint.find_last_of(':');
        auto* pHandle = static_cast<uv_getaddrinfo_t*>(Allocator::GetDefault()->Allocate(sizeof(uv_getaddrinfo_t)));

        std::string endpoint = acEndpoint;
        std::string serviceName = "10578";  // Default Skyrim Together port
        if(pos != std::string::npos)
        {
            serviceName = acEndpoint.c_str() + 1 + pos;
            endpoint = endpoint.substr(0, pos);
        }

        m_pHandle = pHandle;
        uv_getaddrinfo(static_cast<uv_loop_t*>(m_pLoop), pHandle, GetAddrInfoCallback, endpoint.c_str(), serviceName.c_str(), nullptr);

        return true;
    }

    bool Client::ConnectByIp(const std::string& acEndpoint) noexcept
    {
        ENetAddress address;

        const auto pos = acEndpoint.find_last_of(':');
        std::string ip = acEndpoint;
        uint16_t port = 10578;  // Default port

        if(pos != std::string::npos)
        {
            port = static_cast<uint16_t>(std::stoi(acEndpoint.substr(pos + 1)));
            ip = acEndpoint.substr(0, pos);
        }

        // Try to parse as IP address string (supports both IPv4 and IPv6)
        if (enet_address_set_host_ip(&address, ip.c_str()) != 0)
        {
            // If parsing as IP failed, try DNS resolution
            if (enet_address_set_host(&address, ENET_ADDRESS_TYPE_ANY, ip.c_str()) != 0)
            {
                spdlog::error("[SkyrimCoopNetworking] CLIENT: Failed to parse/resolve address: {}", ip);
                return false;
            }
        }

        address.port = port;

        m_pPeer = enet_host_connect(m_pHost, &address, 2, 0);
        if (m_pPeer == nullptr)
        {
            spdlog::error("[SkyrimCoopNetworking] CLIENT: Failed to create peer for connection");
            return false;
        }

        spdlog::info("[SkyrimCoopNetworking] CLIENT: Connecting to {}:{}", ip, port);
        return true;
    }

    void Client::Close() noexcept
    {
        if (m_pPeer != nullptr)
        {
            enet_peer_disconnect(m_pPeer, static_cast<enet_uint32>(kNormal));

            // Allow time for disconnect packet to be sent
            ENetEvent event;
            enet_host_service(m_pHost, &event, 100);

            enet_peer_reset(m_pPeer);
            m_pPeer = nullptr;

            m_clock.Reset();
            m_connected = false;

            OnDisconnected(kAborted);
        }

        if(m_pHandle != nullptr)
        {
            uv_cancel(static_cast<uv_req_t*>(m_pHandle));
        }
    }

    void Client::Update() noexcept
    {
        m_clock.Update();

        if (m_clock.GetCurrentTick() - m_lastStatisticsPoint >= 1000)
        {
            m_lastStatisticsPoint = m_clock.GetCurrentTick();
            m_previousFrame = m_currentFrame;
            m_currentFrame = {};
        }

        // Process DNS resolution
        uv_run(static_cast<uv_loop_t*>(m_pLoop), UV_RUN_NOWAIT);

        // Process ENet events
        ENetEvent event;
        while (enet_host_service(m_pHost, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
                spdlog::info("[SkyrimCoopNetworking] CLIENT: Connection established, waiting for clock sync");
                // Don't call OnConnected() yet - wait for clock sync in HandleServerTime()
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                m_currentFrame.RecvBytes += static_cast<uint32_t>(event.packet->dataLength);
                m_currentFrame.UncompressedRecvBytes += static_cast<uint32_t>(event.packet->dataLength);

                HandleMessage(event.packet->data, static_cast<uint32_t>(event.packet->dataLength));

                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
            {
                spdlog::info("[SkyrimCoopNetworking] CLIENT: Disconnected (reason: {})", event.data);

                m_pPeer = nullptr;
                m_clock.Reset();
                m_connected = false;

                // Map enet disconnect code to EDisconnectReason
                EDisconnectReason reason = kNormal;
                switch (event.data)
                {
                case 0: reason = kNormal; break;
                case 1: reason = kKicked; break;
                case 2: reason = kKicked; break;  // Banned treated as kicked
                default: reason = kTimeout; break;
                }

                OnDisconnected(reason);
                break;
            }

            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }

        OnUpdate();
    }

    void Client::Send(Packet* apPacket, const EPacketFlags acPacketFlags) const noexcept
    {
        if (m_pPeer == nullptr)
            return;

        m_currentFrame.UncompressedSentBytes += apPacket->m_size;

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

        m_currentFrame.SentBytes += apPacket->m_size;

        // Map packet flags to ENet flags
        enet_uint32 flags = 0;
        uint8_t channel = 0;

        switch (acPacketFlags)
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
            spdlog::error("[SkyrimCoopNetworking] CLIENT: Failed to create packet");
            return;
        }

        if (enet_peer_send(m_pPeer, channel, packet) < 0)
        {
            spdlog::error("[SkyrimCoopNetworking] CLIENT: Failed to send packet");
            enet_packet_destroy(packet);
            return;
        }

        // Flush immediately for NoDelay packets
        if (acPacketFlags == kUnreliableNoDelay || acPacketFlags == kReliableNoNagle)
        {
            enet_host_flush(m_pHost);
        }
    }

    bool Client::IsConnected() const noexcept
    {
        return m_pPeer != nullptr && m_connected && m_clock.IsSynchronized();
    }

    uint32_t Client::GetRoundTripTime() const noexcept
    {
        if (m_pPeer == nullptr)
            return 0;

        return m_pPeer->roundTripTime;
    }

    Client::Statistics Client::GetStatistics() const noexcept
    {
        return m_previousFrame;
    }

    const SynchronizedClock& Client::GetClock() const noexcept
    {
        return m_clock;
    }

    void Client::HandleMessage(const void* apData, uint32_t aSize) noexcept
    {
        if (aSize == 0)
            return;

        auto pData = static_cast<const uint8_t*>(apData);

        const auto cOpcode = pData[0];

        pData += 1;
        aSize -= 1;

        switch (cOpcode)
        {
        case kPayload:
            OnConsume(pData, aSize);
            break;
        case kServerTime:
            HandleServerTime(pData, aSize);
            break;
        case kCompressedPayload:
            HandleCompressedPayload(pData, aSize);
            break;
        default:
            spdlog::warn("[SkyrimCoopNetworking] CLIENT: Unknown opcode: {}", cOpcode);
            break;
        }
    }

    void Client::HandleServerTime(const void* apData, uint32_t aSize) noexcept
    {
        if (aSize < 8)
            return;

        // Read server timestamp (big-endian uint64_t)
        uint64_t serverTime = 0;
        const auto* bytes = static_cast<const uint8_t*>(apData);
        for (int i = 0; i < 8; ++i)
        {
            serverTime = (serverTime << 8) | bytes[i];
        }

        const auto cWasSynchronized = GetClock().IsSynchronized();

        m_clock.Synchronize(serverTime, GetRoundTripTime());

        // Only call OnConnected() once when we first get synchronized
        if (!cWasSynchronized && GetClock().IsSynchronized())
        {
            m_connected = true;
            OnConnected();
        }
    }

    void Client::HandleCompressedPayload(const void* apData, uint32_t aSize) noexcept
    {
        std::string data;
        snappy::Uncompress((const char*)apData, aSize, &data);

        m_currentFrame.UncompressedRecvBytes -= aSize;
        m_currentFrame.UncompressedRecvBytes += data.size() & 0xFFFFFFFF;

        if (!data.empty()) [[likely]]
        {
            OnConsume(data.data(), data.size() & 0xFFFFFFFF);
        }
    }
}
