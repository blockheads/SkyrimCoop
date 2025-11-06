#pragma once

#include <cstdint>

namespace TiltedPhoques
{
    enum EPacketFlags
    {
        kReliable,
        kUnreliable
    };

    enum EConnectOpcode : uint8_t
    {
        kPayload = 0,
        kServerTime = 1,
        kCompressedPayload = 2
    };

    struct SteamInterface
    {
        static void Acquire();
        static void Release();
    };

    // Stub type for compatibility - using enet6 for actual networking
    using ConnectionId_t = uint32_t;
}
