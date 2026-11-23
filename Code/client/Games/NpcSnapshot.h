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

// A leveled NPC with a chosen face: its copy uses that base and takes its name from the announcement's DisplayName.
inline uint32_t SaveFlags(uint32_t aRecorded, bool aPlayer, bool aTemporary, bool aSelectedFace = false) noexcept
{
    if (!aPlayer && aTemporary && aSelectedFace)
        return 0;
    return aRecorded | CaptureFlags(aPlayer, aTemporary);
}

// A copy keeps this game's resolved leveled base when it has the owner's face: its stats (level) live there.
inline bool KeepsResolvedBase(bool aTemporary, uint32_t aTemplateBase, uint32_t aOwnerBase) noexcept
{
    return aOwnerBase && aTemporary && aTemplateBase == aOwnerBase;
}

enum class BaseApply
{
    Keep,    // this game's resolved base already has the owner's face
    Relevel, // a leveled reference: built again from the owner's pick, still a temporary base
    Swap,    // any other actor: the owner's base itself
};

// How a copy takes its owner's base. A leveled reference never gets a permanent record: a reset hands its current base
// to the garbage collector (37292/37323 -> 36460), which would destroy that record (leveled-copy-base.md).
inline BaseApply ChooseBaseApply(bool aLeveled, bool aTemporary, uint32_t aTemplateBase, uint32_t aOwnerBase) noexcept
{
    if (KeepsResolvedBase(aTemporary, aTemplateBase, aOwnerBase))
        return BaseApply::Keep;
    return aLeveled ? BaseApply::Relevel : BaseApply::Swap;
}

} // namespace NpcSnapshot
