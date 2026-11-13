#pragma once

#include <cstdint>

// hkbPoseMatchingGenerator's pick (59583), as pure logic.
namespace PoseMatch
{
// Its first match (none before: -1) takes its child's whole weight at once; a later one blends there.
[[nodiscard]] constexpr bool Snaps(int32_t aCurrentMatch) noexcept
{
    return aCurrentMatch == -1;
}
} // namespace PoseMatch
