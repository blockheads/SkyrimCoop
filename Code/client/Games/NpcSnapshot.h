#pragma once

#include <cstdint>

namespace NpcSnapshot
{
// CommonLibSSE-NG TESNPC::ChangeFlags::kFullName.
constexpr uint32_t kFullName = 1u << 5;

inline uint32_t CaptureFlags(bool aPlayer, bool aTemporary) noexcept
{
    return !aPlayer && aTemporary ? kFullName : 0;
}

inline uint32_t SaveFlags(uint32_t aRecorded, bool aPlayer, bool aTemporary, bool aSelectedFace = false) noexcept
{
    if (!aPlayer && aTemporary && aSelectedFace)
        return kFullName;
    return aRecorded | CaptureFlags(aPlayer, aTemporary);
}

inline bool HasResolvedName(uint32_t aChanges) noexcept
{
    return (aChanges & kFullName) != 0;
}

} // namespace NpcSnapshot
