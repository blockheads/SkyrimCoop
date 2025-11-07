#include <chrono>
#include "Client.hpp"
#include "SteamInterface.hpp"
#include <cassert>
#include <Buffer.hpp>
#include <Allocator.hpp>
#include "Packet.hpp"
#include <bit>
#include <snappy.h>
#include <spdlog/spdlog.h>

// Helper function for network byte order conversion (big endian)
namespace
{
    inline uint64_t FromBigEndian64(const void* pData)
    {
        const uint8_t* bytes = static_cast<const uint8_t*>(pData);
        uint64_t value = (static_cast<uint64_t>(bytes[0]) << 56) |
                        (static_cast<uint64_t>(bytes[1]) << 48) |
                        (static_cast<uint64_t>(bytes[2]) << 40) |
                        (static_cast<uint64_t>(bytes[3]) << 32) |
                        (static_cast<uint64_t>(bytes[4]) << 24) |
                        (static_cast<uint64_t>(bytes[5]) << 16) |
                        (static_cast<uint64_t>(bytes[6]) << 8)  |
                        (static_cast<uint64_t>(bytes[7]));
        return value;
    }
}

namespace TiltedPhoques
{
    Client::Client() noexcept
        : m_pHost(nullptr)
        , m_pPeer(nullptr)
        , m_lastStatisticsPoint(0)
    {
        SteamInterface::Acquire();

        // Initialize ENet
        if (enet_initialize() != 0)
        {
            spdlog::error("[TiltedConnect] CLIENT: Failed to initialize ENet");
            return;
        }

        // Create client host (no address binding for client)
        m_pHost = enet_host_create(
            ENET_ADDRESS_TYPE_ANY,  // Accept any address type
            nullptr,                // No specific bind address (client)
            1,                      // Only 1 outgoing connection
            2,                      // 2 channels
            0,                      // Unlimited incoming bandwidth
            0                       // Unlimited outgoing bandwidth
        );

        if (!m_pHost)
        {
            spdlog::error("[TiltedConnect] CLIENT: Failed to create ENet client host");
        }
    }

    Client::~Client()
    {
        Close();

        if (m_pHost)
        {
            enet_host_destroy(m_pHost);
            m_pHost = nullptr;
        }

        enet_deinitialize();
        SteamInterface::Release();
    }

    Client::Client(Client&& aRhs) noexcept
        : m_pHost(nullptr)
        , m_pPeer(nullptr)
    {
        SteamInterface::Acquire();
        this->operator=(std::move(aRhs));
    }

    Client& Client::operator=(Client&& aRhs) noexcept
    {
        std::swap(m_pHost, aRhs.m_pHost);
        std::swap(m_pPeer, aRhs.m_pPeer);
        std::swap(m_clock, aRhs.m_clock);
        std::swap(m_lastStatisticsPoint, aRhs.m_lastStatisticsPoint);
        std::swap(m_currentFrame, aRhs.m_currentFrame);
        std::swap(m_previousFrame, aRhs.m_previousFrame);

        return *this;
    }

    bool Client::Connect(const std::string& acEndpoint, uint16_t aPort) noexcept
    {
        if (!m_pHost)
        {
            spdlog::error("[TiltedConnect] CLIENT: Cannot connect - host not initialized");
            return false;
        }

        if (m_pPeer)
        {
            spdlog::warn("[TiltedConnect] CLIENT: Already connected or connecting");
            return false;
        }

        ENetAddress address;

        // Try to resolve hostname/IP
        int result = enet_address_set_host(&address, ENET_ADDRESS_TYPE_ANY, acEndpoint.c_str());
        if (result != 0)
        {
            spdlog::error("[TiltedConnect] CLIENT: Failed to resolve host: {}", acEndpoint);
            OnDisconnected(kCannotResolve);
            return false;
        }

        address.port = aPort;

        // Connect to the server
        // Parameters: peer to connect to, channel count, user data
        m_pPeer = enet_host_connect(m_pHost, &address, 2, 0);

        if (!m_pPeer)
        {
            spdlog::error("[TiltedConnect] CLIENT: Failed to create connection to {}:{}", acEndpoint, aPort);
            OnDisconnected(kLocalProblem);
            return false;
        }

        spdlog::info("[TiltedConnect] CLIENT: Connecting to {}:{}...", acEndpoint, aPort);
        return true;
    }

    void Client::Close() noexcept
    {
        if (m_pPeer)
        {
            enet_peer_disconnect(m_pPeer, 0);

            // Wait a bit for disconnect to process
            if (m_pHost)
            {
                ENetEvent event;
                while (enet_host_service(m_pHost, &event, 100) > 0)
                {
                    if (event.type == ENET_EVENT_TYPE_DISCONNECT ||
                        event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT)
                    {
                        break;
                    }
                }
            }

            m_pPeer = nullptr;
            m_clock.Reset();
        }
    }

    void Client::Update() noexcept
    {
        if (!m_pHost)
            return;

        m_clock.Update();

        if (m_clock.GetCurrentTick() - m_lastStatisticsPoint >= 1000)
        {
            m_lastStatisticsPoint = m_clock.GetCurrentTick();
            m_previousFrame = m_currentFrame;
            m_currentFrame = {};
        }

        // Service the network
        ENetEvent event;
        while (enet_host_service(m_pHost, &event, 0) > 0)
        {
            switch (event.type)
            {
            case ENET_EVENT_TYPE_CONNECT:
            {
                spdlog::info("[TiltedConnect] CLIENT: Connection established, waiting for clock sync");
                // Don't notify OnConnected yet - wait for clock sync
                break;
            }

            case ENET_EVENT_TYPE_RECEIVE:
            {
                m_currentFrame.RecvBytes += event.packet->dataLength;
                m_currentFrame.UncompressedRecvBytes += event.packet->dataLength;

                HandleMessage(event.packet->data, event.packet->dataLength);

                enet_packet_destroy(event.packet);
                break;
            }

            case ENET_EVENT_TYPE_DISCONNECT:
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
            {
                spdlog::info("[TiltedConnect] CLIENT: Disconnected from server (reason: {})",
                            event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT ? "timeout" : "normal");

                m_pPeer = nullptr;
                m_clock.Reset();

                const auto reason = event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT
                    ? kTimeout
                    : kNormal;

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
        if (!m_pPeer)
            return;

        m_currentFrame.UncompressedSentBytes += apPacket->m_size;

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

        // Create ENet packet
        const enet_uint32 flags = (acPacketFlags == kReliable) ? ENET_PACKET_FLAG_RELIABLE : 0;
        ENetPacket* packet = enet_packet_create(apPacket->m_pData, apPacket->m_size, flags);

        if (packet)
        {
            enet_peer_send(m_pPeer, 0, packet);
        }
    }

    bool Client::IsConnected() const noexcept
    {
        if (m_pPeer && m_pPeer->state == ENET_PEER_STATE_CONNECTED)
        {
            return GetClock().IsSynchronized();
        }

        return false;
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
        // We handle the cases where packets target the current stack or the user stack
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
            assert(false);
            break;
        }
    }

    void Client::HandleServerTime(const void* apData, uint32_t aSize) noexcept
    {
        if (aSize < 8)
            return;

        // Get ping from peer
        uint32_t ping = 0;
        if (m_pPeer)
        {
            ping = m_pPeer->roundTripTime;
        }

        const auto cServerTime = FromBigEndian64(apData);
        const auto cWasSynchronized = GetClock().IsSynchronized();

        m_clock.Synchronize(cServerTime, ping);

        if (!cWasSynchronized)
        {
            spdlog::info("[TiltedConnect] CLIENT: Clock synchronized (ping: {}ms)", ping);
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
