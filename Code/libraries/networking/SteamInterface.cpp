#include "SteamInterface.hpp"

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
            // Stub implementation - using enet6 for networking instead of GameNetworkingSockets
            s_initSucceeded = true;
            spdlog::info("[TiltedConnect] Using enet6 for networking (GameNetworkingSockets disabled for MinGW compatibility)");
        }
    }

    void SteamInterface::Release()
    {
        if (s_initCounter.fetch_sub(1, std::memory_order_relaxed) == 1)
        {
            // Stub implementation - nothing to clean up
        }
    }
}
