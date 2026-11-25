#pragma once

#include <cstdint>

// Movement goes unreliably, jumps and spawns reliably, with no order between them: one sent before a jump can arrive
// after it (actor-movement.md, "A goal outlives its place"). No engine: ClientTests, HostAuthorityTests.
struct SnapshotAge
{
    // Further back than this it is a clock's change, not a late packet.
    static constexpr uint64_t kLatestMs = 2000;

    [[nodiscard]] static bool IsStale(uint64_t aTick, uint64_t aRestartTick) noexcept
    {
        return aTick < aRestartTick && aRestartTick - aTick <= kLatestMs;
    }
};
