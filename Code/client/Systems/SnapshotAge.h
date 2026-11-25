#pragma once

#include <algorithm>
#include <cstdint>

// Movement goes unreliably, jumps and spawns reliably, with no order between them: one sent before a jump can arrive
// after it (actor-movement.md, "Late snapshots"). No engine: ClientTests, HostAuthorityTests.
struct SnapshotAge
{
    [[nodiscard]] static bool IsStale(uint64_t aTick, uint64_t aCutoff) noexcept { return aTick < aCutoff; }

    // A jump's cutoff: never back, never ahead of this game's clock (an owner's wrong stamp would hold its copy still).
    // aNow 0: no clock yet.
    [[nodiscard]] static uint64_t Cutoff(uint64_t aTick, uint64_t aNow, uint64_t aLast) noexcept
    {
        return std::max(aNow ? std::min(aTick, aNow) : aTick, aLast);
    }
};
