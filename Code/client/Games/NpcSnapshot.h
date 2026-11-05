#pragma once

#include <cstdint>

namespace NpcSnapshot
{
// CommonLibSSE-NG TESNPC::ChangeFlags::kFullName.
constexpr uint32_t kFullName = 1u << 5;

// A created copy's base is built from these bytes, so its name must be in them: the engine only saves it with kFullName,
// which only naming the player marks (TESActorBase::SaveGame 24387, RaceSexMenu::ChangeName 52415).
inline uint32_t CaptureFlags(bool aPlayer, bool aTemporary) noexcept
{
    return aPlayer || aTemporary ? kFullName : 0;
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

} // namespace NpcSnapshot
