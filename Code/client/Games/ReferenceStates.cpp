#include <Games/ReferenceStates.h>

#include <Games/FormState.h>
#include <SaveLoad.h>
#include <FormIds.h>

#include <TESObjectREFR.h>
#include <NetImmerse/NiNode.h>
#include <NetImmerse/NiUpdateData.h>

#include <cstring>
#include <RTTI.h>
#include <VersionDb.h>
#include <Forms/TESBoundObject.h>

namespace ReferenceStates
{
namespace
{
// Enable and Disable (19800, 19801) flip the disabled flag through TESForm::SetDisabled (14646), SetDelete (19565)
// the deleted one; both are form flags. 1: enabled; kDeleted: deleted.
constexpr uint32_t kDeleted = 2;

bool ReadEnabled(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    aChange.Value = (acObject.IsDisabled() ? 0 : 1) | (acObject.IsDeleted() ? kDeleted : 0);
    return true;
}

// At once, no fade: the host's fade already played.
bool ApplyEnabled(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (acChange.Value & kDeleted)
    {
        if (!aObject.IsDisabled())
            aObject.DisableImpl();
        aObject.SetDelete(true);
        return true;
    }
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

// Plays the open or close, as ObjectReference.SetOpen does (BGSOpenCloseForm::SetOpenState, 14287). It is nobody's
// activation here: the lid's Open event (14285) would hand the menu to an activator an interrupted lid left behind.
bool ApplyOpen(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (IsOpen(aObject) == (acChange.Value != 0))
        return true;
    aObject.extraData.RemoveByType(ExtraDataType::OpenCloseActivateRef);
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

// 1% steps, as the reference holds it.
bool ReadScale(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    aChange.Value = acObject.scale;
    return true;
}

bool ApplyScale(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (aObject.scale != acChange.Value)
        aObject.SetScale(static_cast<float>(acChange.Value) / 100.f);
    return true;
}

bool ReadOwner(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    const TESForm* pOwner = acObject.GetOwner();
    if (!pOwner)
        return true;
    const auto cOwner = ToSession(pOwner->formID);
    if (cOwner)
        aChange.Item = *cOwner;
    return cOwner.has_value();
}

// No owner: Item's base id 0.
bool ApplyOwner(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    TESForm* pOwner = acChange.Item.BaseId ? TESForm::GetById(ToGame(acChange.Item)) : nullptr;
    if (acChange.Item.BaseId && !pOwner)
        return false;
    if (aObject.GetOwner() != pOwner)
        aObject.SetOwner(pOwner);
    return true;
}

bool ReadBase(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    const auto cBase = acObject.baseForm ? ToSession(acObject.baseForm->formID) : std::nullopt;
    if (cBase)
        aChange.Item = *cBase;
    return cBase.has_value();
}

// A placed leveled item's pick: the host's base, its 3D loaded again.
bool ApplyBase(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    auto* pBase = Cast<TESBoundObject>(TESForm::GetById(ToGame(acChange.Item)));
    if (!pBase)
        return false;
    if (aObject.baseForm != pBase)
        aObject.SetBaseObject(pBase);
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

// An actor's graphs are its owner's (copies load them at spawn).
bool ReadGraph(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    return acObject.formType == FormType::Reference && acObject.SaveGraphs(aChange.Data) && !aChange.Data.empty();
}

bool ApplyGraph(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    if (!aObject.LoadGraphs(acChange.Data))
        return false;
    // Loading changes local bones; publish them to world transforms and collision (70251).
    if (NiAVObject* p3D = aObject.GetNiNode())
        p3D->Update(NiUpdateData::kDirty);
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
    {ObjectChange::Kind::kScale, TESObjectREFR::CHANGE_REFR_SCALE, &ReadScale, &ApplyScale},
    {ObjectChange::Kind::kOwner, TESObjectREFR::CHANGE_REFR_EXTRA_OWNERSHIP, &ReadOwner, &ApplyOwner},
    {ObjectChange::Kind::kBase, TESObjectREFR::CHANGE_REFR_BASEOBJECT, &ReadBase, &ApplyBase},
    {ObjectChange::Kind::kGraph, TESObjectREFR::CHANGE_REFR_ANIMATION, &ReadGraph, &ApplyGraph},
    {ObjectChange::Kind::kSaveData, ~0u, &ReadSaveData, &ApplySaveData},
};

// The last row, for mods' states later: off while vanilla's get rows (a flag no row takes is logged instead).
constexpr bool cSaveDataFallback = false;

// Game-only extra data is the engine's runtime bookkeeping, whose load on a guest re-runs a reference's alias data
// under the quests' locks (FinishLoadGame) against a cell load's alias fills; promoted and cell changed are the
// location header's, which FormState does not carry (loaded alone: a half-promoted reference).
// Inventory travels as the container event's kItems changes (ContainerEvents), not a row. Activating children: a
// reference on its cell's list while its activate-parent children run (InitChildActivates, 19428), the host's.
constexpr uint32_t cNotSent = TESObjectREFR::CHANGE_REFR_EXTRA_GAME_ONLY | TESObjectREFR::CHANGE_REFR_PROMOTED |
                              TESObjectREFR::CHANGE_REFR_CELL_CHANGED | TESObjectREFR::CHANGE_REFR_INVENTORY |
                              TESObjectREFR::CHANGE_REFR_EXTRA_ACTIVATING_CHILDREN;
} // namespace

uint32_t SentFlags() noexcept
{
    return ~cNotSent;
}

uint32_t RowFlags() noexcept
{
    uint32_t flags = 0;
    for (const auto& cState : cStates)
        if (cState.Kind != ObjectChange::Kind::kSaveData)
            flags |= cState.Flags;
    return flags;
}

uint32_t RecordedRows(const TESObjectREFR& acObject) noexcept
{
    constexpr uint32_t cHavokMove = TESObjectREFR::CHANGE_REFR_HAVOK_MOVE;
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    if (!pSaveLoad || acObject.formType != FormType::Reference)
        return 0;
    const uint32_t cFlags = pSaveLoad->ChangeFlags(acObject, RowFlags() | cHavokMove);
    return (cFlags & ~cHavokMove) | (cFlags & cHavokMove ? TESObjectREFR::CHANGE_REFR_MOVE : 0);
}

uint32_t ChangedRows(const TESObjectREFR& acObject) noexcept
{
    return acObject.IsTemporary() ? 0 : RecordedRows(acObject);
}

ObjectChange Deleted(const GameId& acId) noexcept
{
    ObjectChange change{ObjectChange::Kind::kEnabled, acId};
    change.Value = kDeleted;
    return change;
}

void RecordRows(TESObjectREFR& aObject, uint32_t aRows) noexcept
{
    aRows &= RowFlags();
    const uint32_t cHeld = RecordedRows(aObject);
    uint32_t extra = cHeld & ~aRows;
    if (extra & TESObjectREFR::CHANGE_REFR_MOVE)
        extra |= TESObjectREFR::CHANGE_REFR_HAVOK_MOVE;
    if (extra)
        aObject.UnsetChanged(extra);
    if (const uint32_t cMissing = aRows & ~cHeld)
        aObject.MarkChanged(cMissing);
}

void Read(const TESObjectREFR& acObject, const GameId& acId, uint32_t aFlags,
          std::vector<ObjectChange>& aChanges) noexcept
{
    uint32_t left = aFlags & SentFlags();
    for (const auto& cState : cStates)
    {
        if (cState.Kind == ObjectChange::Kind::kSaveData && !cSaveDataFallback)
            break;
        const uint32_t cTaken = left & cState.Flags;
        if (!cTaken)
            continue;
        left &= ~cTaken;
        ObjectChange change{cState.Kind, acId};
        if (cState.Read(acObject, cTaken, change))
            aChanges.push_back(std::move(change));
    }
    // A flag no row takes, the first time: what vanilla still needs a row for. The game's thread only.
    static uint32_t s_logged = 0;
    if (const uint32_t cNew = left & ~s_logged)
    {
        s_logged |= cNew;
        spdlog::info("Reference sync: change flags {:X} of {:X} have no row, not sent", cNew, acObject.formID);
    }
}

bool Matches(const TESObjectREFR& acObject, const ObjectChange& acChange) noexcept
{
    for (const auto& cState : cStates)
        if (cState.Kind == acChange.Type)
        {
            ObjectChange here{acChange.Type, acChange.Object};
            return cState.Read(acObject, cState.Flags, here) && here.Value == acChange.Value && here.Item == acChange.Item &&
                   here.Data == acChange.Data;
        }
    return false;
}

bool Apply(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    for (const auto& cState : cStates)
        if (cState.Kind == acChange.Type)
            return cState.Apply(aObject, acChange);
    return false;
}
} // namespace ReferenceStates
