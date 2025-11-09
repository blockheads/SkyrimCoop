#pragma once

#include <enet6/enet.h>
#include <cstdint>

namespace TiltedPhoques
{
    enum EPacketFlags
    {
        kReliable,
        kUnreliable,
        kReliableNoNagle,
        kUnreliableNoDelay
    };

    enum EConnectOpcode : uint8_t
    {
        kPayload = 0,
        kServerTime = 1,
        kCompressedPayload = 2
    };

    struct ENetInterface
    {
        static void Acquire();
        static void Release();
    };

    // Use pointer-based connection ID for enet6
    using ConnectionId_t = uintptr_t;
}
