#pragma once

#include <NoCopyMove.h>

#include <enet6/enet.h>
#include <cstdint>
#include <chrono>
#include "ENetInterface.h"
#include <unordered_map>

namespace Net
{
    struct Packet;
    struct Server
    {
        Server() noexcept;
        virtual ~Server();

        NO_COPY_MOVE(Server);

        bool Host(uint16_t aPort, uint32_t aTickRate, bool bEnableDualStackIP = true) noexcept;
        void Close() noexcept;

        void Update() noexcept;

        enum EDisconnectReason : int
        {
            Unknown,
            Quit,
            Kicked,
            Banned,
            BadConnection,
            TimedOut,
            SessionEnded
        };

        void SetDisconnectReason(EDisconnectReason aReason) noexcept { m_disconnectReason = aReason; }

        virtual void OnUpdate() = 0;
        virtual void OnConsume(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId) = 0;
        virtual void OnConnection(ConnectionId_t aHandle) = 0;
        virtual void OnDisconnection(ConnectionId_t aConnectionId,
                                     EDisconnectReason aReason) = 0;

        void SendToAll(Packet* apPacket, EPacketFlags aPacketFlags = kReliable) noexcept;
        void Send(ConnectionId_t aConnectionId, Packet* apPacket, EPacketFlags aPacketFlags = kReliable) const noexcept;
        void Kick(ConnectionId_t aConnectionId) noexcept;

        [[nodiscard]] uint16_t GetPort() const noexcept;
        [[nodiscard]] bool IsListening() const noexcept;
        [[nodiscard]] uint32_t GetClientCount() const noexcept;
        [[nodiscard]] uint32_t GetTickRate() const noexcept;
        [[nodiscard]] uint64_t GetTick() const noexcept;
        [[nodiscard]] bool IsAlive(ConnectionId_t aConnectionId) const noexcept;
        [[nodiscard]] ENetPeer* GetPeer(ConnectionId_t aConnectionId) const noexcept;
        // The connection from a port on this machine (a tunnel's end, SteamService); 0 when none.
        [[nodiscard]] ConnectionId_t FindLocalConnection(uint16_t aPort) const noexcept;

    private:

        void Remove(ConnectionId_t aId) noexcept;

        void HandleMessage(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId) noexcept;
        void HandleCompressedPayload(const void* apData, uint32_t aSize, ConnectionId_t aConnectionId) noexcept;

        void SynchronizeClientClocks(ConnectionId_t aSpecificConnection = 0) noexcept;

        EDisconnectReason m_disconnectReason{Quit};
        ENetHost* m_pHost;
        std::unordered_map<ConnectionId_t, ENetPeer*> m_peers;

        uint32_t m_tickRate;
        uint16_t m_port;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_lastClockSyncTime;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_lastUpdateTime;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_currentTick;
        std::chrono::milliseconds m_timeBetweenUpdates;
    };
}
