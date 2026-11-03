#pragma once

#include "ENetInterface.h"
#include <cstdint>
#include <enet6/enet.h>

namespace Net
{
struct WireTotals
{
    uint32_t SentBytes{}, ReceivedBytes{}, SentPackets{}, ReceivedPackets{};
};
inline WireTotals ReadTotals(const ENetHost* acpHost) noexcept
{
    if (!acpHost)
        return {};
    return {acpHost->totalSentData, acpHost->totalReceivedData, acpHost->totalSentPackets,
            acpHost->totalReceivedPackets};
}

// ENet's current estimates, not cumulative loss counters. Valid only while connected.
struct ConnectionQuality
{
    ConnectionId_t Connection{};
    uint32_t RoundTripMs{}, VariationMs{}, ReliableInFlight{}, QueuedReliable{};
    double LossPercent{};
};
inline ConnectionQuality ReadQuality(ConnectionId_t aConnection, const ENetPeer& acPeer) noexcept
{
    return {aConnection,
            acPeer.roundTripTime,
            acPeer.roundTripTimeVariance,
            acPeer.reliableDataInTransit,
            static_cast<uint32_t>(enet_list_size(const_cast<ENetList*>(&acPeer.outgoingSendReliableCommands))),
            100.0 * acPeer.packetLoss / static_cast<double>(ENET_PEER_PACKET_LOSS_SCALE)};
}
} // namespace Net
