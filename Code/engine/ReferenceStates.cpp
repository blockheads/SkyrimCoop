#include <ReferenceStates.h>

#include <FormState.h>
#include <world/DestructionRow.h>
#include <items/Overrides.h>
#include <world/EnabledRow.h>
#include <ScriptMotion.h>
#include <FormChangeRows.h>
#include <world/LidUnposedEvent.h>
#include <core/World.h>
#include <SaveLoad.h>
#include <core/FormIds.h>

#include <TESObjectREFR.h>
#include <NetImmerse/NiNode.h>
#include <NetImmerse/NiUpdateData.h>

#include <cstring>
#include <mutex>
#include <unordered_map>
#include <RTTI.h>
#include <VersionDb.h>
#include <Forms/TESBoundObject.h>
#include <ExtraData/ExtraLock.h>
#include <ExtraData/ExtraObjectHealth.h>
#include <Components/BGSDestructibleObjectForm.h>
#include <ExtraData/ExtraTeleport.h>

namespace ReferenceStates
{
namespace
{
// Enable and Disable (19800, 19801) flip the disabled flag through TESForm::SetDisabled (14646), SetDelete (19565)
// the deleted one, SetDestroyed (14649) the destroyed one: form flags all (EnabledRow).
constexpr uint32_t kDeleted = EnabledRow::Pack({.Enabled = false, .Deleted = true});
constexpr uint32_t kDestroyedFlag = 0x800000;

bool ReadEnabled(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    aChange.Value = EnabledRow::Pack({.Enabled = !acObject.IsDisabled(), .Deleted = acObject.IsDeleted(), .Destroyed = (acObject.flags & kDestroyedFlag) != 0});
    return true;
}

// At once, no fade: the host's fade already played.
bool ApplyEnabled(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    const EnabledRow::State cState = EnabledRow::Unpack(acChange.Value);
    if (cState.Destroyed != ((aObject.flags & kDestroyedFlag) != 0))
    {
        using TSetDestroyed = bool(TESForm*, bool);
        POINTER_SKYRIMSE(TSetDestroyed, s_setDestroyed, 14649);
        s_setDestroyed.Get()(&aObject, cState.Destroyed);
    }
    if (cState.Deleted)
    {
        if (!aObject.IsDisabled())
            aObject.DisableImpl();
        aObject.SetDelete(true);
        return true;
    }
    if (cState.Enabled && aObject.IsDisabled())
        aObject.EnableImpl();
    else if (!cState.Enabled && !aObject.IsDisabled())
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
    aObject.SetOpenState(acChange.Value != 0, false);
    if (!aObject.GetNiNode())
        World::Runner().Trigger(LidUnposedEvent{aObject.formID});
    return true;
}

// A plant or tree picked: its form flag 0x2000 (TESObjectREFR::SetHarvestedFlag 14647). 1: harvested.
constexpr uint32_t kHarvestedFlag = 0x2000;

bool ReadHarvested(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    aChange.Value = (acObject.flags & kHarvestedFlag) != 0;
    return true;
}

// As TESProduceForm::Harvest (14864) leaves it: the flag, its change, and the model (UpdateHarvestModel 14865, which
// Load3D calls for one loading later).
bool ApplyHarvested(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    using TSetHarvested = void(TESObjectREFR*, bool);
    POINTER_SKYRIMSE(TSetHarvested, s_setHarvested, 14647);
    using TUpdateModel = void(TESObjectREFR*, NiAVObject*);
    POINTER_SKYRIMSE(TUpdateModel, s_updateModel, 14865);
    s_setHarvested.Get()(&aObject, acChange.Value != 0);
    aObject.MarkChanged(TESObjectREFR::CHANGE_OBJECT_EMPTY);
    if (NiAVObject* p3D = aObject.GetNiNode())
        s_updateModel.Get()(&aObject, p3D);
    return true;
}

} // namespace

void PoseOpenState(TESObjectREFR& aObject) noexcept
{
    const auto cType = aObject.baseForm ? aObject.baseForm->formType : FormType::Count;
    if (cType != FormType::Door && cType != FormType::Container)
        return;
    aObject.SetOpenState(IsOpen(aObject), true);
}

namespace
{
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

bool ReadMotion(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    const auto cValue = ScriptMotion::Get().Of(acObject.formID);
    if (!cValue)
        return false;
    aChange.Value = *cValue;
    return true;
}

// As the script's SetMotionTypeFunctor::Run (56332) does it: every body under its 3D, the fixed ones left (77866).
// With no 3D yet, when it loads (ApplyDeferred).
bool ApplyMotion(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    NiNode* p3D = aObject.GetNiNode();
    if (!p3D)
    {
        ScriptMotion::Get().Defer(aObject.formID, acChange.Value);
        return true;
    }
    p3D->SetMotionType(ScriptMotion::TypeOf(acChange.Value), true, false, ScriptMotion::AllowsActivate(acChange.Value));
    return true;
}

// A destructible's health: ExtraObjectHealth (0x55) once damaged, else its base's full health; none for one with no
// destruction data (form flag 0x1000000, set by SetObjectReference 19785).
constexpr uint32_t kDestructibleFlag = 0x1000000;

std::optional<float> DestructionHealth(const TESObjectREFR& acObject) noexcept
{
    if (!(acObject.flags & kDestructibleFlag) || !acObject.baseForm)
        return std::nullopt;
    if (const auto* pHealth = static_cast<const ExtraObjectHealth*>(acObject.extraData.GetByType(ExtraDataType::ObjectHealth)))
        return pHealth->health;
    using TGetDestructible = BGSDestructibleObjectForm*(TESForm*);
    POINTER_SKYRIMSE(TGetDestructible, s_getDestructible, 14152);
    const BGSDestructibleObjectForm* pForm = s_getDestructible.Get()(acObject.baseForm);
    return pForm && pForm->data ? std::optional(static_cast<float>(pForm->data->health)) : std::nullopt;
}

bool ReadDestruction(const TESObjectREFR& acObject, uint32_t, ObjectChange& aChange) noexcept
{
    const auto cHealth = DestructionHealth(acObject);
    if (cHealth)
        aChange.Value = DestructionRow::Pack(*cHealth);
    return cHealth.has_value();
}

// A row that came before the reference's 3D (its damage needs one), applied when it loads (ApplyDeferred).
struct DeferredDestruction
{
    std::mutex Lock;
    std::unordered_map<uint32_t, uint32_t> Values;
};
DeferredDestruction s_deferredDestruction;

// The engine's own damage (TESObjectREFR::DamageObject 19526, vtable 0x42; BGSDestructibleObjectForm 14158): its
// stage's model swap and 3D reload (a cut web loses its collision), explosion, debris and destroyed flag. Allowed on a
// destroyed one: the host's enabled row may have set the flag first. Back to whole first with ClearDestruction (14181).
// A capped stage stops each call at its threshold (a web's first, at 99%): called again until it reads as the row.
// docs/re/systems/destruction.md.
bool ApplyDestruction(TESObjectREFR& aObject, const ObjectChange& acChange) noexcept
{
    auto here = DestructionHealth(aObject);
    if (!here)
        return false;
    const float cTarget = DestructionRow::Unpack(acChange.Value);
    {
        // The newest row stands: it replaces, or cancels, one still waiting for the 3D.
        std::scoped_lock _(s_deferredDestruction.Lock);
        if (!aObject.GetNiNode() && cTarget != *here)
            s_deferredDestruction.Values[aObject.formID] = acChange.Value;
        else
            s_deferredDestruction.Values.erase(aObject.formID);
    }
    if (cTarget == *here || !aObject.GetNiNode())
        return true;
    if (cTarget > *here)
    {
        using TClearDestruction = void(TESObjectREFR*);
        POINTER_SKYRIMSE(TClearDestruction, s_clearDestruction, 14181);
        s_clearDestruction.Get()(&aObject);
        here = DestructionHealth(aObject);
        if (!here || cTarget >= *here)
            return true;
    }
    using TDamageObject = void(TESObjectREFR*, float, bool);
    POINTER_SKYRIMSE(TDamageObject, s_damageObject, 19526);
    constexpr int cMaxStages = 127; // DestructibleObjectData::numStages is an int8
    for (int i = 0; i < cMaxStages && here && cTarget < *here; ++i)
    {
        s_damageObject.Get()(&aObject, *here - cTarget, true);
        const auto cNow = DestructionHealth(aObject);
        if (!cNow || *cNow == *here)
            break;
        here = cNow;
    }
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
    {ObjectChange::Kind::kHarvested, TESObjectREFR::CHANGE_OBJECT_EMPTY, &ReadHarvested, &ApplyHarvested},
    {ObjectChange::Kind::kMove, TESObjectREFR::CHANGE_REFR_MOVE, &ReadMove, &ApplyMove},
    {ObjectChange::Kind::kScale, TESObjectREFR::CHANGE_REFR_SCALE, &ReadScale, &ApplyScale},
    {ObjectChange::Kind::kOwner, TESObjectREFR::CHANGE_REFR_EXTRA_OWNERSHIP, &ReadOwner, &ApplyOwner},
    {ObjectChange::Kind::kBase, TESObjectREFR::CHANGE_REFR_BASEOBJECT, &ReadBase, &ApplyBase},
    {ObjectChange::Kind::kGraph, TESObjectREFR::CHANGE_REFR_ANIMATION, &ReadGraph, &ApplyGraph},
    // No flag of its own: its SetMotionType's havok change (4) has the reference read, the store says what it was.
    {ObjectChange::Kind::kMotion, 0, &ReadMotion, &ApplyMotion},
    // Its health is game-only extra data (DamageObject records 0x80000000): read only for a destructible.
    {ObjectChange::Kind::kDestruction, TESObjectREFR::CHANGE_REFR_EXTRA_GAME_ONLY, &ReadDestruction, &ApplyDestruction},
    {ObjectChange::Kind::kSaveData, ~0u, &ReadSaveData, &ApplySaveData},
};

// The last row, for mods' states later: off while vanilla's get rows (a flag no row takes is logged instead).
constexpr bool cSaveDataFallback = false;

// Game-only extra data is the engine's runtime bookkeeping (only a destructible's health is taken, by its row: loaded
// whole on a guest it re-runs a reference's alias data under the quests' locks, FinishLoadGame, against a cell load's
// alias fills); promoted and cell changed are the location header's, which FormState does not carry (loaded alone: a
// half-promoted reference).
// Inventory travels as the container event's kItems changes (ContainerEvents), not a row. Activating children: a
// reference on its cell's list while its activate-parent children run (InitChildActivates, 19428), the host's.
constexpr uint32_t cNotSent = TESObjectREFR::CHANGE_REFR_PROMOTED |
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
    if (!pSaveLoad || FormChangeRows::OfFormType(static_cast<uint8_t>(acObject.formType)) != FormChangeRows::Form::kReference)
        return 0;
    uint32_t flags = pSaveLoad->ChangeFlags(acObject, RowFlags() | cHavokMove);
    // Game-only extra data is a row only for a destructible (its health).
    if (!(acObject.flags & kDestructibleFlag))
        flags &= ~TESObjectREFR::CHANGE_REFR_EXTRA_GAME_ONLY;
    return (flags & ~cHavokMove) | (flags & cHavokMove ? TESObjectREFR::CHANGE_REFR_MOVE : 0);
}

uint32_t LinkedLockRows(const TESObjectREFR& acObject) noexcept
{
    const BGSSaveLoadGame* pSaveLoad = BGSSaveLoadGame::Get();
    const auto* pTeleport = static_cast<const ExtraTeleport*>(acObject.extraData.GetByType(ExtraDataType::Teleport));
    // Its own lock as 20223 and 19512 judge it: an ExtraLock holding a lock.
    const auto* pOwn = static_cast<const ExtraLock*>(acObject.extraData.GetByType(ExtraDataType::Lock));
    if (!pSaveLoad || !pTeleport || !pTeleport->teleportData || (pOwn && pOwn->lock))
        return 0;
    const TESObjectREFR* pLinked = TESObjectREFR::GetByHandle(pTeleport->teleportData->linkedDoor);
    return pLinked ? pSaveLoad->ChangeFlags(*pLinked, TESObjectREFR::CHANGE_OBJECT_EXTRA_LOCK) : 0;
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
          std::vector<ObjectChange>& aChanges, bool aWhole) noexcept
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
    // A cell's state carries it; live, each script call once.
    if (const auto cMotion = aWhole ? ScriptMotion::Get().Of(acObject.formID) : ScriptMotion::Get().TakeUnsent(acObject.formID))
        aChanges.push_back({.Type = ObjectChange::Kind::kMotion, .Object = acId, .Value = *cMotion});
    // A flag no row takes, the first time: what vanilla still needs a row for. The game's thread only.
    static uint32_t s_logged = 0;
    if (const uint32_t cNew = left & ~s_logged)
    {
        s_logged |= cNew;
        spdlog::info("Reference sync: change flags {:X} of {:X} have no row, not sent", cNew, acObject.formID);
    }
}

std::optional<ObjectChange> ReadRow(const TESObjectREFR& acObject, const GameId& acId, ObjectChange::Kind aKind) noexcept
{
    for (const auto& cState : cStates)
        if (cState.Kind == aKind)
        {
            ObjectChange row{aKind, acId};
            return cState.Read(acObject, cState.Flags, row) ? std::optional(std::move(row)) : std::nullopt;
        }
    return std::nullopt;
}

bool Matches(const TESObjectREFR& acObject, const ObjectChange& acChange) noexcept
{
    const auto cHere = ReadRow(acObject, acChange.Object, acChange.Type);
    return cHere && cHere->Value == acChange.Value && cHere->Item == acChange.Item && cHere->Data == acChange.Data;
}

void ForgetDeferred() noexcept
{
    ScriptMotion::Get().Clear();
    std::scoped_lock _(s_deferredDestruction.Lock);
    s_deferredDestruction.Values.clear();
}

void ApplyDeferred(TESObjectREFR& aObject) noexcept
{
    if (const auto cValue = ScriptMotion::Get().TakeDeferred(aObject.formID))
        (void)ApplyMotion(aObject, {.Type = ObjectChange::Kind::kMotion, .Value = *cValue});
    std::optional<uint32_t> destruction;
    {
        std::scoped_lock _(s_deferredDestruction.Lock);
        if (const auto it = s_deferredDestruction.Values.find(aObject.formID); it != s_deferredDestruction.Values.end())
        {
            destruction = it->second;
            s_deferredDestruction.Values.erase(it);
        }
    }
    if (destruction)
    {
        ScopedHostChangeOverride _; // the host's row, not this game's damage: proposed nowhere
        (void)ApplyDestruction(aObject, {.Type = ObjectChange::Kind::kDestruction, .Value = *destruction});
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
