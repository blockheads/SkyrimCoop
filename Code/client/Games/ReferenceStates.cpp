#include <TiltedOnlinePCH.h>

#include <Games/ReferenceStates.h>

#include <Games/FormState.h>

#include <TESObjectREFR.h>

#include <cstring>

namespace ReferenceStates
{
namespace
{
// Enable and Disable (19800, 19801) flip the disabled flag through TESForm::SetDisabled (14646). 1: enabled.
bool ReadEnabled(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    aChange.Value = !acObject.IsDisabled();
    return true;
}

// At once, no fade: the host's fade already played.
bool ApplyEnabled(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (acChange.Value && aObject.IsDisabled())
        aObject.EnableImpl();
    else if (!acChange.Value && !aObject.IsDisabled())
        aObject.DisableImpl();
    return true;
}

// The level in the low byte, kLocked above it; no lock reads as unlocked, level 0.
constexpr uint32_t kLocked = 0x100;

bool ReadLock(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    const Lock* pLock = acObject.GetLock();
    aChange.Value = pLock ? (pLock->IsLocked() ? kLocked : 0) | pLock->lockLevel : 0;
    return true;
}

bool ApplyLock(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    return aObject.SetLockState((acChange.Value & kLocked) != 0, static_cast<uint8_t>(acChange.Value & 0xFF));
}

bool IsOpen(const TESObjectREFR& acObject) noexcept
{
    const auto cState = const_cast<TESObjectREFR&>(acObject).GetOpenState();
    return cState == TESObjectREFR::kOpen || cState == TESObjectREFR::kOpening;
}

// 1: open or opening.
bool ReadOpen(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    aChange.Value = IsOpen(acObject);
    return true;
}

// Plays the open or close, as ObjectReference.SetOpen does (BGSOpenCloseForm::SetOpenState, 14287).
bool ApplyOpen(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (IsOpen(aObject) == (acChange.Value != 0))
        return true;
    using TSetOpenState = bool(TESObjectREFR*, bool, bool);
    POINTER_SKYRIMSE(TSetOpenState, s_setOpenState, 14287);
    s_setOpenState.Get()(&aObject, acChange.Value != 0, false);
    return true;
}

// Position, then angle.
bool ReadMove(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    const float cValues[6] = {acObject.position.x, acObject.position.y, acObject.position.z,
                              acObject.rotation.x, acObject.rotation.y, acObject.rotation.z};
    aChange.Data.assign(reinterpret_cast<const char*>(cValues), sizeof(cValues));
    return true;
}

// In its cell, each axis that differs, as Papyrus' SetPosition and SetAngle do (22053, 22054): its 3D and collision
// follow, nothing disabled (MoveTo's disable and enable crash a placeable water's cell animations).
bool ApplyMove(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    float values[6];
    if (acChange.Data.size() != sizeof(values))
        return false;
    std::memcpy(values, acChange.Data.data(), sizeof(values));
    constexpr char cAxes[] = {'X', 'Y', 'Z'};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (aObject.position[axis] != values[axis])
            aObject.SetPositionOnAxis(cAxes[axis], values[axis]);
        if (aObject.rotation[axis] != values[3 + axis])
            aObject.SetAngleOnAxis(cAxes[axis], glm::degrees(values[3 + axis]));
    }
    return true;
}

// Every flag no row above takes, as its save data. Nothing saved, nothing sent (a havok move: its position is in the
// location header FormState skips).
bool ReadSaveData(const TESObjectREFR& acObject, uint32_t aFlags, ObjectChange& aChange) noexcept
{
    aChange.Value = aFlags;
    return FormState::Save(acObject, aFlags, aChange.Data) && !aChange.Data.empty();
}

// A load records no change: marked after it, so the cell's unload keeps it as the host's does.
bool ApplySaveData(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (!FormState::Load(aObject, acChange.Value, acChange.Data))
        return false;
    aObject.MarkChanged(acChange.Value);
    return true;
}

struct ReferenceState
{
    ObjectChange::Kind Kind;
    uint32_t Flags; // the change flags it takes (TESObjectREFR::ChangeFlags)
    bool (*Read)(const TESObjectREFR& acObject, uint32_t aFlags, ObjectChange& aChange) noexcept; // false: nothing to send
    bool (*Apply)(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept;
};

// In order: each row takes its flags, the last what is left.
constexpr ReferenceState cStates[] = {
    {ObjectChange::Kind::kEnabled, TESObjectREFR::CHANGE_FORM_FLAGS, &ReadEnabled, &ApplyEnabled},
    {ObjectChange::Kind::kLock, TESObjectREFR::CHANGE_OBJECT_EXTRA_LOCK, &ReadLock, &ApplyLock},
    {ObjectChange::Kind::kOpenState, TESObjectREFR::CHANGE_OBJECT_OPEN_STATE, &ReadOpen, &ApplyOpen},
    {ObjectChange::Kind::kMove, TESObjectREFR::CHANGE_REFR_MOVE, &ReadMove, &ApplyMove},
    {ObjectChange::Kind::kSaveData, ~0u, &ReadSaveData, &ApplySaveData},
};

// Game-only extra data is the engine's runtime bookkeeping, whose load on a guest re-runs a reference's alias data
// under the quests' locks (FinishLoadGame) against a cell load's alias fills; promoted and cell changed are the
// location header's, which FormState does not carry (loaded alone: a half-promoted reference).
constexpr uint32_t cNotSent = TESObjectREFR::CHANGE_REFR_EXTRA_GAME_ONLY | TESObjectREFR::CHANGE_REFR_PROMOTED |
                              TESObjectREFR::CHANGE_REFR_CELL_CHANGED;
} // namespace

uint32_t SentFlags() noexcept
{
    return ~cNotSent;
}

void Read(const TESObjectREFR& acObject, const GameId& acId, uint32_t aFlags, TiltedPhoques::Vector<ObjectChange>& aChanges) noexcept
{
    uint32_t left = aFlags & SentFlags();
    for (const auto& cState : cStates)
    {
        const uint32_t cTaken = left & cState.Flags;
        if (!cTaken)
            continue;
        left &= ~cTaken;
        ObjectChange change{cState.Kind, acId};
        if (cState.Read(acObject, cTaken, change))
            aChanges.push_back(std::move(change));
    }
}

bool Apply(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    for (const auto& cState : cStates)
        if (cState.Kind == acChange.Type)
            return cState.Apply(aObject, acChange);
    return false;
}
} // namespace ReferenceStates
