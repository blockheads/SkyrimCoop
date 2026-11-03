#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Net
{
    struct SynchronizedClock
    {
        SynchronizedClock() noexcept;
        [[nodiscard]] uint64_t GetCurrentTick() const noexcept;
        [[nodiscard]] bool IsSynchronized() const noexcept;
        void Synchronize(uint64_t aServerTick, uint32_t aPing) noexcept;
        void Reset() noexcept;
        void Update() noexcept;

        // A sync as it was taken: the host's time sent, the ping it was read with, the correction it asked for.
        struct Sync
        {
            uint64_t ServerTick;
            uint32_t Ping;
            int64_t CorrectionMs;
        };
        // The last few syncs, oldest first (the status's network readout).
        [[nodiscard]] std::vector<Sync> Syncs() const noexcept;

    private:
        std::array<Sync, 8> m_syncs{};
        size_t m_syncCount{};

        uint64_t m_lastServerTick;
        uint64_t m_simulatedTick;
        std::chrono::nanoseconds m_previousSimulatedTick;
        std::chrono::nanoseconds m_tickDelta;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_lastSynchronizationTime{};
    };
}
