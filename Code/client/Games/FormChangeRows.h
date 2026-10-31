#pragma once

#include <cstdint>

// Which of a form's recorded changes the host sends its guests as rows (HostEngine::OnFormChange): a world
// reference's states (ReferenceStates), a cell's owner (the party's house) and an actor's owner (the party's horse).
// An actor's other states are its owner's game's.
namespace FormChangeRows
{
enum class Form
{
    kReference,
    kCell,
    kActor,
    kOther,
};
// TESObjectCELL::ChangeFlags::kOwnership: Cell.SetFactionOwner / SetActorOwner (a house bought).
inline constexpr uint32_t cCellOwnership = 1u << 3;
// TESObjectREFR::CHANGE_REFR_EXTRA_OWNERSHIP: SetActorOwner / SetFactionOwner on a reference (a horse bought).
inline constexpr uint32_t cReferenceOwnership = 1u << 6;

// The flags of aFlags sent as rows; aReferenceFlags: a reference's (ReferenceStates::SentFlags).
[[nodiscard]] constexpr uint32_t Sent(Form aForm, uint32_t aFlags, uint32_t aReferenceFlags) noexcept
{
    switch (aForm)
    {
    case Form::kReference: return aFlags & aReferenceFlags;
    case Form::kCell: return aFlags & cCellOwnership;
    case Form::kActor: return aFlags & cReferenceOwnership;
    default: return 0;
    }
}
} // namespace FormChangeRows
