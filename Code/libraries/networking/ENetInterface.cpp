#include "ENetInterface.hpp"

#include <atomic>
#include <spdlog/spdlog.h>

namespace TiltedPhoques
{
    static std::atomic<std::size_t> s_initCounter = 0;
    static bool s_initSucceeded = false;

    void ENetInterface::Acquire()
    {
        if (s_initCounter.fetch_add(1, std::memory_order_relaxed) == 0)
        {
            int result = enet_initialize();
            s_initSucceeded = (result == 0);

            if (!s_initSucceeded)
            {
                spdlog::critical("[SkyrimCoopNetworking] FATAL: enet_initialize failed with error code: {}", result);
            }
            else
            {
                spdlog::info("[SkyrimCoopNetworking] enet_initialize succeeded");
            }
        }
    }

    void ENetInterface::Release()
    {
        if (s_initCounter.fetch_sub(1, std::memory_order_relaxed) == 1)
        {
            enet_deinitialize();
            spdlog::info("[SkyrimCoopNetworking] enet_deinitialize called");
        }
    }
}
