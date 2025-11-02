#include "SteamInterface.hpp"
#include "steam/steamnetworkingsockets.h"

#include <atomic>
#include <spdlog/spdlog.h>

namespace TiltedPhoques
{
    static std::atomic<std::size_t> s_initCounter = 0;
    static bool s_initSucceeded = false;

    void SteamInterface::Acquire()
    {
        if (s_initCounter.fetch_add(1, std::memory_order_relaxed) == 0)
        {
            SteamDatagramErrMsg errorMessage;
            s_initSucceeded = GameNetworkingSockets_Init(nullptr, errorMessage);
            if (!s_initSucceeded)
            {
                spdlog::critical("[TiltedConnect] FATAL: GameNetworkingSockets_Init failed: {}", errorMessage);
            }
            else
            {
                spdlog::info("[TiltedConnect] GameNetworkingSockets_Init succeeded");
            }
        }
    }

    void SteamInterface::Release()
    {
        if (s_initCounter.fetch_sub(1, std::memory_order_relaxed) == 1)
        {
            GameNetworkingSockets_Kill();
        }
    }
}
