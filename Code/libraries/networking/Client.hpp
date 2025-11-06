#pragma once

#include <string>
#include <enet6/enet.h>
#include "SynchronizedClock.hpp"

namespace TiltedPhoques
{
    // ENet types (if not already defined)
    #ifndef TILTEDPHOQUES_CONNECTIONID_T
    #define TILTEDPHOQUES_CONNECTIONID_T
    using ConnectionId_t = uint32_t;

    enum EPacketFlags
    {
        kReliable = ENET_PACKET_FLAG_RELIABLE,
        kUnreliable = 0
    };
    #endif

    struct Packet;
    struct Client
    {
        enum EDisconnectReason
        {
            kTimeout,
            kLocalProblem,
            kKicked,
            kCannotResolve,
            kAborted,
            kNormal
        };

        struct Statistics
        {
            uint32_t SentBytes{};
            uint32_t RecvBytes{};
            uint32_t UncompressedSentBytes{};
            uint32_t UncompressedRecvBytes{};
        };

        Client() noexcept;
        virtual ~Client();

        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        Client(Client&&) noexcept;
        Client& operator=(Client&&) noexcept;

        bool Connect(const std::string& acEndpoint, uint16_t aPort = 10578) noexcept;
        void Close() noexcept;

        void Update() noexcept;

        virtual void OnConsume(const void* apData, uint32_t aSize) = 0;
        virtual void OnConnected() = 0;
        virtual void OnDisconnected(EDisconnectReason aReason) = 0;
        virtual void OnUpdate() = 0;

        void Send(Packet* apPacket, EPacketFlags acPacketFlags = kReliable) const noexcept;

        [[nodiscard]] bool IsConnected() const noexcept;
        [[nodiscard]] Statistics GetStatistics() const noexcept;
        [[nodiscard]] const SynchronizedClock& GetClock() const noexcept;

    private:

        void HandleMessage(const void* apData, uint32_t aSize) noexcept;
        void HandleServerTime(const void* apData, uint32_t aSize) noexcept;
        void HandleCompressedPayload(const void* apData, uint32_t aSize) noexcept;

        ENetHost* m_pHost;
        ENetPeer* m_pPeer;
        SynchronizedClock m_clock;
        uint64_t m_lastStatisticsPoint{};
        mutable Statistics m_currentFrame{};
        Statistics m_previousFrame{};
    };
}
