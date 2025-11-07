#pragma once

#include <cstdint>

namespace TiltedPhoques
{
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
}
