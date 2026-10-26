#include "FakeEngine.h"

#include <Events/ContainerChangeEvent.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/ActivateEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/AiStateChangeEvent.h>
#include <Events/EffectVisualsChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>
#include <Events/HealthChangeEvent.h>
#include <Events/ActorValueChangeEvent.h>
#include <Events/ActorDeathChangeEvent.h>
#include <Games/DeathActions.h>
#include <Events/HitEvent.h>
#include <Events/ProjectileLaunchedEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/PartyNativeEvent.h>
#include <Events/RemoveWaypointEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/TimeScaleEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <Games/Overrides.h>
#include <Events/GameLoadedEvent.h>
#include <Events/MenuEvent.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Services/SaveService.h>
#include <Wire/Buffer.h>
#include <World.h>
#include <FormIds.h>

using namespace PartyRules;

namespace
{
bool Argument(const LocalCall& acCall, size_t aIndex)
{
    const auto* pValue = aIndex < acCall.Arguments.size() ? std::get_if<bool>(&acCall.Arguments[aIndex]) : nullptr;
    return pValue && *pValue;
}

// ControlMap's jumping (UserEvent kJumping): Skyrim takes it with movement.
constexpr uint32_t kJumping = 1 << 10;

// The ControlMap flags a controls native's arguments take, as Skyrim's (measured 2026-10-23, docs/design/testing.md);
// camera switch, activate and the journal take none.
uint32_t FlagsOf(const LocalCall& acCall)
{
    return (Argument(acCall, 0) ? kMovement | kJumping : 0) | (Argument(acCall, 1) ? kFighting : 0) |
           (Argument(acCall, 3) ? kLooking : 0) | (Argument(acCall, 4) ? kSneaking : 0) |
           (Argument(acCall, 5) ? kMainFour : 0);
}

LocalCall ControlsCall(const char* acpFunction, std::array<bool, 8> aFlags)
{
    LocalCall call{.Class = "Game", .Function = acpFunction};
    for (const bool cFlag : aFlags)
        call.Arguments.emplace_back(cFlag);
    call.Arguments.emplace_back(int32_t{0});
    return call;
}
} // namespace

LocalCall DisablePlayerControls(bool aMovement, bool aFighting, bool aCamSwitch, bool aLooking, bool aSneaking,
                                bool aMenu, bool aActivate)
{
    return ControlsCall("DisablePlayerControls",
                        {aMovement, aFighting, aCamSwitch, aLooking, aSneaking, aMenu, aActivate, false});
}

LocalCall EnablePlayerControls(bool aMovement, bool aFighting, bool aCamSwitch, bool aLooking, bool aSneaking,
                               bool aMenu, bool aActivate)
{
    return ControlsCall("EnablePlayerControls",
                        {aMovement, aFighting, aCamSwitch, aLooking, aSneaking, aMenu, aActivate, true});
}

// PartyNatives::Scope around the native, the native (one ControlMap::ToggleControls) and HookToggleControls inside it.
void FakeEngine::RunScript(const LocalCall& acCall, uint32_t aCause, bool aStory)
{
    if (acCall.Class != "Game")
        return;
    if (acCall.Function == "ShakeCamera")
    {
        // Game.ShakeCamera(source, strength, duration): the screen's.
        const auto* pSeconds = acCall.Arguments.size() > 2 ? std::get_if<float>(&acCall.Arguments[2]) : nullptr;
        ShakeSeconds = pSeconds ? *pSeconds : 0.f;
        if (InSession && ShareOf(Kind::kScreen, aCause, aStory, 0) == Share::kParty)
            World::Get().GetRunner().Trigger(PartyNativeEvent{acCall});
        return;
    }
    const bool cEnable = acCall.Function == "EnablePlayerControls";
    if (!cEnable && acCall.Function != "DisablePlayerControls")
        return;
    uint32_t flags = FlagsOf(acCall);
    const uint32_t cAsked = flags;
    if (InSession)
    {
        const uint32_t cHolder = HolderOf(aCause);
        if (cEnable)
            flags = Holds.GiveBack(cHolder, flags);
        else
            Holds.Take(cHolder, flags);
    }
    Controls = cEnable ? Controls | flags : Controls & ~flags;
    if (InSession && ShareOf(Kind::kControls, aCause, aStory, cAsked) == Share::kParty)
        World::Get().GetRunner().Trigger(PartyNativeEvent{acCall});
}

void FakeEngine::Frame()
{
    ShakeSeconds = std::max(0.f, ShakeSeconds - 1.f / 60);
    // Game time at the time scale for the real time since the last frame, as the engine's frame delta is.
    const auto cNow = std::chrono::steady_clock::now();
    if (m_lastFrame.time_since_epoch().count())
        Calendar.Time += Calendar.TimeScale * std::chrono::duration<float>(cNow - m_lastFrame).count() / 3600.f;
    m_lastFrame = cNow;
    if (Calendar.Time >= 24.f)
    {
        Calendar.Time -= 24.f;
        ++Calendar.Day;
        ++Calendar.DaysPassed;
    }
    RunQueued();
}

void FakeEngine::Queue(LocalCall aCall, uint32_t aCause)
{
    m_queued.emplace_back(std::move(aCall), aCause);
}

void FakeEngine::RunQueued()
{
    auto queued = std::exchange(m_queued, {});
    for (const auto& [cCall, cCause] : queued)
        RunScript(cCall, cCause);
}

FakeEngine& FakeEngine::Current() noexcept
{
    return World::Get().ctx().at<FakeEngine&>();
}

GameId SessionIdOf(uint32_t aFormId) noexcept
{
    return GameId(aFormId >> 24, aFormId & 0x00FFFFFF);
}

uint32_t FormIdOf(const GameId& acId) noexcept
{
    return (acId.ModId << 24) | acId.BaseId;
}

namespace
{
// A container's count of an item; none held is no entry, as the engine's lists keep it.
void SetCount(FakeEngine::Reference& aReference, uint32_t aItem, int32_t aCount)
{
    if (aCount > 0)
        aReference.Items[aItem] = aCount;
    else
        aReference.Items.erase(aItem);
}

// ReferenceStates' lock row: the level in the low byte, kLocked above it.
constexpr uint32_t kLocked = 0x100;

ObjectChange LockRow(uint32_t aReference, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kLock, .Object = SessionIdOf(aReference)};
    row.Value = (acReference.Locked ? kLocked : 0) | acReference.LockLevel;
    return row;
}

// ReferenceStates' open row: 1 open or opening.
ObjectChange OpenRow(uint32_t aDoor, const FakeEngine::Reference& acReference)
{
    ObjectChange row{.Type = ObjectChange::Kind::kOpenState, .Object = SessionIdOf(aDoor)};
    row.Value = acReference.Open ? 1 : 0;
    return row;
}

ObjectChange ItemsRow(uint32_t aContainer, uint32_t aItem, int32_t aCount)
{
    ObjectChange row{.Type = ObjectChange::Kind::kItems, .Object = SessionIdOf(aContainer), .Item = SessionIdOf(aItem)};
    row.Value = static_cast<uint32_t>(aCount);
    return row;
}
} // namespace

void FakeEngine::LoadCell(uint32_t aCell)
{
    Attached.insert(aCell);
    InMemory.insert(aCell);
}

// An actor Actor::Create made goes with it, unless persistent (copy-contract.md, "Players' copies"); a placeatme
// reference stays.
void FakeEngine::UnloadCell(uint32_t aCell)
{
    Attached.erase(aCell);
    std::vector<uint32_t> left;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Created && cActor.Cell == aCell && !cActor.Persistent)
            left.push_back(cId);
    for (const uint32_t cId : left)
        DeleteActor(cId);
}

void FakeEngine::PlayerSetsLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    if (IsGuest)
    {
        References.at(aReference).Locked = aLocked;
        References.at(aReference).LockLevel = aLevel;
        Changed.insert(aReference);
        World::Get().GetDispatcher().trigger(LockChangeEvent(aReference, aLocked, aLevel, true));
        return;
    }
    SetLock(aReference, aLocked, aLevel);
}

void FakeEngine::PlayerSetsItems(uint32_t aContainer, uint32_t aItem, int32_t aCount)
{
    if (IsGuest)
    {
        SetCount(References.at(aContainer), aItem, aCount);
        Changed.insert(aContainer);
        World::Get().GetDispatcher().trigger(ContainerChangeEvent{aContainer, aItem});
        return;
    }
    SetItems(aContainer, aItem, aCount);
}

void FakeEngine::SetLock(uint32_t aReference, bool aLocked, uint8_t aLevel)
{
    auto& reference = References.at(aReference);
    reference.Locked = aLocked;
    reference.LockLevel = aLevel;
    Changed.insert(aReference);
    Recorded.push_back(LockRow(aReference, reference));
}

void FakeEngine::SetItems(uint32_t aContainer, uint32_t aItem, int32_t aCount)
{
    SetCount(References.at(aContainer), aItem, aCount);
    Changed.insert(aContainer);
    Recorded.push_back(ItemsRow(aContainer, aItem, aCount));
}

void FakeEngine::SetOpen(uint32_t aDoor, bool aOpen)
{
    auto& reference = References.at(aDoor);
    reference.Open = aOpen;
    Changed.insert(aDoor);
    Recorded.push_back(OpenRow(aDoor, reference));
}

void FakeEngine::ToggleDoor(uint32_t aObject)
{
    const auto it = References.find(aObject);
    if (it == References.end() || !it->second.Door)
        return;
    if (IsGuest)
        it->second.Open = !it->second.Open;
    else
        SetOpen(aObject, !it->second.Open);
}

std::vector<ObjectChange> FakeEngine::RowsOf(uint32_t aReference) const
{
    const auto& cReference = References.at(aReference);
    std::vector<ObjectChange> rows{LockRow(aReference, cReference)};
    if (cReference.Door)
        rows.push_back(OpenRow(aReference, cReference));
    for (const auto& [cItem, cCount] : cReference.Items)
        rows.push_back(ItemsRow(aReference, cItem, cCount));
    return rows;
}

bool FakeEngine::Apply(const ObjectChange& acChange, bool aLoad)
{
    const auto it = References.find(FormIdOf(acChange.Object));
    if (it == References.end() || !InMemory.contains(it->second.Cell))
        return false; // its cell never loaded here: "its object is not in this game"
    auto& reference = it->second;
    switch (acChange.Type)
    {
    case ObjectChange::Kind::kRecord: reference.RecordFlags = acChange.Value; break;
    case ObjectChange::Kind::kLock:
        reference.Locked = (acChange.Value & kLocked) != 0;
        reference.LockLevel = static_cast<uint8_t>(acChange.Value & 0xFF);
        break;
    case ObjectChange::Kind::kItems: SetCount(reference, FormIdOf(acChange.Item), static_cast<int32_t>(acChange.Value)); break;
    case ObjectChange::Kind::kOpenState: reference.Open = acChange.Value != 0; break;
    default: break;
    }
    (void)aLoad; // a state already as the host's is left as it is: the same either way here
    return true;
}

std::optional<std::vector<ObjectChange>> FakeEngine::CellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const
{
    const uint32_t cCell = FormIdOf(acCell);
    if (!Attached.contains(cCell))
        return std::nullopt;
    std::vector<ObjectChange> rows;
    for (const auto& [cId, cReference] : References)
    {
        const bool cNamed = std::ranges::any_of(acChanged, [cId](const ChangedReference& acRef) { return FormIdOf(acRef.Id) == cId; });
        if (cReference.Cell == cCell && (Changed.contains(cId) || cNamed))
            std::ranges::move(RowsOf(cId), std::back_inserter(rows));
    }
    return rows;
}

std::optional<CellStateRequest> FakeEngine::CellChanges(uint32_t aCell) const
{
    if (!Attached.contains(aCell))
        return std::nullopt;
    CellStateRequest request;
    request.Cell = SessionIdOf(aCell);
    for (const uint32_t cId : Changed)
        if (References.at(cId).Cell == aCell)
            request.Changed.push_back({SessionIdOf(cId), 1});
    return request;
}

ActorValues FakeEngine::FullValues()
{
    ActorValues values;
    for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
        values.ActorValuesList[cIndex] = values.ActorMaxValuesList[cIndex] = 100.f;
    return values;
}

// The engine's temporary forms (a reference placed at run time, a copy's NPC): 0xFF ids; a deleted one's id goes to
// the next (docs/re/crashes/2026-10-14-reused-copy-form-id.md).
uint32_t FakeEngine::NewTemporary() noexcept
{
    ++m_made;
    if (m_freedTemporary.empty())
        return m_nextTemporary++;
    return m_freedTemporary.extract(m_freedTemporary.begin()).value();
}

uint32_t FakeEngine::LoadNpc(uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                        .Essential = EssentialBases.contains(aBase), .Made = m_made};
    return cId;
}

void FakeEngine::PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    Actors[aFormId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z},
                            .Essential = EssentialBases.contains(aBase)};
}

void FakeEngine::CheckHealth(uint32_t aFormId, Actor& aActor) noexcept
{
    if (aActor.Copy || aActor.Dead || aActor.Values.ActorValuesList[kHealth] > 0.f)
        return;
    (aActor.Essential ? aActor.Bleeding : aActor.Dead) = true;
    if (aActor.Dead)
    {
        auto& action = aActor.LatestAnimation;
        action.Tick = World::Get().GetTransport().GetTick();
        action.ActorId = aFormId;
        action.ActionId = 0x1337;
        action.State1 = DeathActions::kDying;
        action.EventName = "DeathAnim";
        World::Get().GetDispatcher().trigger(action);
        World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, false});
    }
}

void FakeEngine::Hit(uint32_t aFormId, float aDamage)
{
    DamageActorHealth(aFormId, 0, aDamage);
}

std::vector<uint32_t> FakeEngine::CopiesOf(uint32_t aBase) const
{
    std::vector<uint32_t> copies;
    for (const auto& [cId, cActor] : Actors)
        if (cActor.Copy && (!aBase || cActor.Base == aBase))
            copies.push_back(cId);
    return copies;
}

// The process lists: the actors in a cell loaded here (copies too: they are this game's actors), but the player.
std::vector<uint32_t> FakeEngine::GetLoadedActorFormIds() const noexcept
{
    std::vector<uint32_t> loaded;
    for (const auto& [cId, cActor] : Actors)
        if (cId != kPlayer && ActorHas3D(cId))
            loaded.push_back(cId);
    return loaded;
}

bool FakeEngine::ActorHas3D(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Loaded && !it->second.Disabled && (aFormId == kPlayer || Attached.contains(it->second.Cell));
}

void FakeEngine::PlayerTravels(uint32_t aCell, const glm::vec3& acPosition)
{
    MoveActorTo(kPlayer, aCell, acPosition.x, acPosition.y, acPosition.z);
}

void FakeEngine::SetActorCopy(uint32_t aFormId, bool aCopy) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Copy = aCopy;
}

bool FakeEngine::IsActorCopy(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy;
}

void FakeEngine::SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Player = aPlayer;
}

uint32_t FakeEngine::GetActorBaseFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Base : 0;
}

bool FakeEngine::SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end() || !aBaseFormId)
        return false;
    it->second.Base = aBaseFormId;
    return true;
}

std::string FakeEngine::GetActorBaseName(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Name : std::string{};
}

float FakeEngine::GetActorBaseWeight(uint32_t aFormId) const noexcept
{
    if (aFormId == kPlayer)
        return Player.Weight;
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Weight : 50.f;
}

uint32_t FakeEngine::GetActorRaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Npcs.find(GetActorBaseFormId(aFormId));
    return it != Npcs.end() ? it->second.Race : 0;
}

uint32_t FakeEngine::GetActorParentCellFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Cell : 0;
}

uint32_t FakeEngine::GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.WorldSpace : 0;
}

void FakeEngine::GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Position : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

void FakeEngine::GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept
{
    const auto it = Actors.find(aFormId);
    const float* cpAt = it != Actors.end() ? it->second.Rotation : nullptr;
    aX = cpAt ? cpAt[0] : 0.f;
    aY = cpAt ? cpAt[1] : 0.f;
    aZ = cpAt ? cpAt[2] : 0.f;
}

bool FakeEngine::IsActorDead(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Dead;
}

void FakeEngine::KillActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        (it->second.Essential ? it->second.Bleeding : it->second.Dead) = true;
        if (!cWasDead && it->second.Dead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
    }
}

void FakeEngine::FinishDeath(uint32_t aFormId)
{
    Actors.at(aFormId).BlockResurrect = false;
    World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId});
}

void FakeEngine::ReplayDeathAction(uint32_t aFormId, const ActionEvent& acAction)
{
    if (!RefuseDeathActions)
        KillActor(aFormId);
    Actors.at(aFormId).LatestAnimation = acAction;
    World::Get().GetDispatcher().trigger(DeathActionReplayedEvent{aFormId, acAction.Tick});
}

ActionEvent FakeEngine::GetActorLatestAnimation(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.LatestAnimation : ActionEvent{};
}

void FakeEngine::SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.LatestAnimation = acAction;
}

// Actor::Respawn: alive again, values restored; the value writers and Resurrect report it.
void FakeEngine::RespawnActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        const bool cWasDead = it->second.Dead;
        const auto cAction = it->second.LatestAnimation;
        it->second.Dead = it->second.Bleeding = false;
        it->second.LatestAnimation = {};
        it->second.Values = FullValues();
        it->second.Bases = FullValues().ActorMaxValuesList;
        for (const uint32_t cIndex : {kHealth, kMagicka, kStamina})
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        if (cWasDead)
            World::Get().GetRunner().Trigger(ActorDeathChangeEvent{aFormId, true, true,
                cAction.ActionId && DeathActions::IsDying(cAction.State1) ? std::optional<uint64_t>{cAction.Tick} : std::nullopt});
    }
}

void FakeEngine::ResurrectActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ResurrectCalls;
        if (it->second.BlockResurrect)
            return;
        RespawnActor(aFormId);
    }
}

bool FakeEngine::DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    const bool cWasDead = it->second.Dead;
    it->second.Values.ActorValuesList[kHealth] -= aDamage;
    World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, kHealth});
    CheckHealth(aFormId, it->second);
    if (!cWasDead && it->second.Dead)
        it->second.KilledBy = aAttackerFormId;
    return true;
}

bool FakeEngine::IsBleedingOut(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Bleeding;
}

bool FakeEngine::IsOtherPlayer(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Copy && it->second.Player;
}

void FakeEngine::SetActorPersistent(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Persistent = true;
}

void FakeEngine::SetActorPlayerRespawnMode(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Essential = true;
}

// ForcePosition and SetRotation; the graph's variables are not modelled.
bool FakeEngine::ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3], const float aRotation[3], float,
                                     const AnimationVariables&) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    std::copy_n(aPosition, 3, it->second.Position);
    std::copy_n(aRotation, 3, it->second.Rotation);
    // Outdoors its parent cell follows where it stands.
    if (it->second.WorldSpace)
        if (const uint32_t cCell = CellAt(it->second.WorldSpace, {aPosition[0], aPosition[1], aPosition[2]}))
            it->second.Cell = cCell;
    return true;
}

float FakeEngine::GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++ValueReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
    ++MaximumReads[{aFormId, aIndex}];
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorMaxValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

void FakeEngine::SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        ++it->second.ValueWrites;
        const auto cIndex = static_cast<uint32_t>(aIndex);
        auto& base = it->second.Bases[cIndex];
        const float cDelta = aValue - base;
        if (cDelta != 0.f)
        {
            base = aValue;
            it->second.Values.ActorValuesList[cIndex] += cDelta;
            it->second.Values.ActorMaxValuesList[cIndex] += cDelta;
            World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
        }
    }
}

// A permanent modifier moves the maximum and current together; damage changes only the current value.
void FakeEngine::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    auto& values = it->second.Values;
    ++it->second.ValueWrites;
    const auto cBefore = values;
    const auto cIndex = static_cast<uint32_t>(aIndex);
    if (aForceMode == 0)
    {
        values.ActorValuesList[cIndex] += aValue - values.ActorMaxValuesList[cIndex];
        values.ActorMaxValuesList[cIndex] = aValue;
    }
    else if (aForceMode == 2)
        values.ActorValuesList[cIndex] = std::min(aValue, values.ActorMaxValuesList[cIndex]);
    if (cBefore != values)
        World::Get().GetRunner().Trigger(ActorValueChangeEvent{aFormId, cIndex});
    if (cIndex == kHealth)
        CheckHealth(aFormId, it->second);
}

bool FakeEngine::IsActorDisabled(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Disabled;
}

bool FakeEngine::IsActorDeleted(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Deleted;
}

void FakeEngine::EnableActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Disabled = false;
}

void FakeEngine::DisableActor(uint32_t aFormId, bool aDelete) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Disabled = true;
        it->second.Deleted |= aDelete;
    }
}

// A copy: a new temporary actor of that NPC, where this game's player is (as PlaceAtMe from the player).
uint32_t FakeEngine::SpawnActorFromNpc(uint32_t aNpcFormId) noexcept
{
    ++Spawned[aNpcFormId];
    const uint32_t cId = NewTemporary();
    Actors[cId] = Actor{.Base = aNpcFormId, .Cell = Actors.at(kPlayer).Cell, .WorldSpace = Actors.at(kPlayer).WorldSpace,
                        .Created = true, .Essential = EssentialBases.contains(aNpcFormId), .Made = m_made};
    return cId;
}

// A player's copy: a temporary NPC made from their appearance, and an actor of it.
uint32_t FakeEngine::CreateCustomActor(const std::string& acAppearance, uint32_t, float aWeight) noexcept
{
    const uint32_t cBase = NewTemporary();
    Npcs[cBase].Weight = aWeight;
    Npcs[cBase].Looks = acAppearance;
    return SpawnActorFromNpc(cBase);
}

uint32_t FakeEngine::SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept
{
    if (aFormId != kPlayer)
        return 0;
    aBuffer = Player.Base;
    return Player.BaseFlags;
}

// TESForm::MarkChanged: the sections are saved from now on.
void FakeEngine::MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept
{
    if (aFormId == kPlayer)
        Player.BaseFlags |= aFlags;
}

// The player's is a travel (MoveTo_Impl queues it as a door's): the cells load and unload, a teleport is reported.
void FakeEngine::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    const uint32_t cFrom = it->second.Cell;
    const auto cExterior = Exteriors.find(aCellId);
    it->second.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    // Outdoors the cell at the position, as MoveTo picks it.
    const uint32_t cAt = it->second.WorldSpace ? CellAt(it->second.WorldSpace, {aX, aY, aZ}) : 0;
    it->second.Cell = cAt ? cAt : aCellId;
    it->second.Position[0] = aX;
    it->second.Position[1] = aY;
    it->second.Position[2] = aZ;
    // A created actor landing in a cell not loaded goes as with its cell's unload (measured: a player's copy spawned
    // four cells off is gone that frame, before SetActorPersistent).
    if (aFormId != kPlayer && it->second.Created && !it->second.Persistent && !Attached.contains(it->second.Cell))
    {
        DeleteActor(aFormId);
        return;
    }
    if (aFormId == kPlayer)
    {
        LoadAroundPlayer(cFrom);
        Teleported = true;
        ++Travels;
    }
}

void FakeEngine::Exterior(uint32_t aWorldSpace, uint32_t aFirstCell, int32_t aRadius)
{
    uint32_t cell = aFirstCell;
    for (int32_t x = -aRadius; x <= aRadius; ++x)
        for (int32_t y = -aRadius; y <= aRadius; ++y)
            Exteriors[cell++] = {aWorldSpace, x, y};
}

uint32_t FakeEngine::LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept
{
    for (const auto& [cId, cCell] : Exteriors)
        if (cCell.WorldSpace == aWorldSpaceFormId && cCell.X == aX && cCell.Y == aY)
            return cId;
    return 0;
}

uint32_t FakeEngine::CellAt(uint32_t aWorldSpace, const glm::vec3& acPosition) const noexcept
{
    const auto cAt = GridCellCoords::CalculateGridCellCoords(acPosition.x, acPosition.y);
    return LookupCellByCoords(aWorldSpace, cAt.X, cAt.Y);
}

// The loaded grid's centre is the player's cell (TES::centerGridX/Y; the engine's own lag behind it not modelled).
void FakeEngine::LoadAroundPlayer(uint32_t aFrom)
{
    const auto& cPlayer = Actors.at(kPlayer);
    std::set<uint32_t> cells{cPlayer.Cell};
    if (const auto cCentre = Exteriors.find(cPlayer.Cell); cCentre != Exteriors.end())
        for (const auto& [cId, cCell] : Exteriors)
            if (cCell.WorldSpace == cCentre->second.WorldSpace &&
                GridCellCoords::IsCellInGridCell({cCell.X, cCell.Y}, {cCentre->second.X, cCentre->second.Y}, false))
                cells.insert(cId);
    std::set<uint32_t> left = std::exchange(m_playerCells, cells);
    left.insert(aFrom);
    for (const uint32_t cCell : left)
        if (!cells.contains(cCell))
            UnloadCell(cCell);
    for (const uint32_t cCell : cells)
        LoadCell(cCell);
}

void FakeEngine::PlayerStands(uint32_t aCell, const glm::vec3& acPosition)
{
    auto& player = Actors.at(kPlayer);
    const uint32_t cFrom = player.Cell;
    player.Cell = aCell;
    const auto cExterior = Exteriors.find(aCell);
    player.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    std::copy_n(&acPosition.x, 3, player.Position);
    LoadAroundPlayer(cFrom);
}

void FakeEngine::PlayerWalks(const glm::vec3& acPosition)
{
    const auto& cPlayer = Actors.at(kPlayer);
    PlayerStands(CellAt(cPlayer.WorldSpace, acPosition), acPosition);
}

void FakeEngine::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Rotation[0] = aX;
        it->second.Rotation[2] = aZ;
    }
}

// As SkyrimBridge's: only a reference made at run time is deleted, never the plugins' or the player.
void FakeEngine::DeleteActor(uint32_t aFormId) noexcept
{
    if (!IsActorTemporary(aFormId) || !Actors.erase(aFormId))
        return;
    m_freedTemporary.insert(aFormId);
}

bool FakeEngine::IsActorNpcTemporary(uint32_t aFormId) const noexcept
{
    return GetActorBaseFormId(aFormId) >= 0xFF000000;
}

Inventory FakeEngine::GetActorInventory(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Items : Inventory{};
}

void FakeEngine::SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Items = acInventory;
}

ActorValues FakeEngine::GetActorEssentialValues(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Values : ActorValues{};
}

// Each value listed; the others stay.
void FakeEngine::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    for (const auto& [cIndex, cValue] : acValues.ActorMaxValuesList)
        it->second.Values.ActorMaxValuesList[cIndex] = cValue;
    for (const auto& [cIndex, cValue] : acValues.ActorValuesList)
        it->second.Values.ActorValuesList[cIndex] = cValue;
}

// PlayerCharacter::RespawnPlayer: up from bleedout, health restored, moved to the COC marker of its cell (outdoors the
// loaded grid's centre cell, its own here).
void FakeEngine::RespawnPlayer() noexcept
{
    auto& player = Actors.at(kPlayer);
    player.Bleeding = false;
    ForceActorValue(kPlayer, 2, kHealth, 1000000.f);
    glm::vec3 to{};
    if (const auto cMarker = CocMarkers.find(player.Cell); cMarker != CocMarkers.end())
        to = cMarker->second;
    else if (const auto cExterior = Exteriors.find(player.Cell); cExterior != Exteriors.end())
        to = {cExterior->second.X * 4096.f + 2048.f, cExterior->second.Y * 4096.f + 2048.f, 0.f};
    MoveActorTo(kPlayer, player.Cell, to.x, to.y, to.z);
}

// PartyNatives' entry points (Papyrus/PartyNatives.h) on the current game's fake engine.
namespace PartyNatives
{
void SetInSession(bool aInSession) noexcept
{
    FakeEngine::Current().InSession = aInSession;
}

bool InSession() noexcept
{
    return FakeEngine::Current().InSession;
}

bool Run(const LocalCall& acCall, uint32_t aSource) noexcept
{
    FakeEngine::Current().Queue(acCall, MirroredTag(aSource));
    return true;
}

void GiveBackControls(std::optional<uint32_t> aSource) noexcept
{
    auto& engine = FakeEngine::Current();
    for (const auto& cReturned : engine.Holds.Leave(aSource ? std::optional(MirroredTag(*aSource)) : std::nullopt))
    {
        if (!cReturned.Free)
            continue;
        const auto c = EnableFor(cReturned.Free, cReturned.NoneElse);
        engine.Queue(
            EnablePlayerControls(c.Movement, c.Fighting, c.CamSwitch, c.Looking, c.Sneaking, c.Menu, c.Activate),
            cReturned.Holder);
    }
}
} // namespace PartyNatives

namespace
{
GameId ServerId(uint32_t aFormId)
{
    return ToSession(aFormId).value_or(GameId{});
}
} // namespace

uint8_t FakeEngine::GetFormType(uint32_t aFormId) const noexcept
{
    const auto it = ItemForms.find(aFormId);
    return it != ItemForms.end() ? it->second.Type : 0;
}

Inventory::Entry& FakeEngine::EntryOf(Actor& aActor, uint32_t aItem)
{
    const GameId cId = ServerId(aItem);
    for (auto& entry : aActor.Items.Entries)
        if (entry.BaseId == cId)
            return entry;
    Inventory::Entry entry;
    entry.BaseId = cId;
    entry.Count = 1;
    return aActor.Items.Entries.emplace_back(entry);
}

void FakeEngine::Equips(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    Equip(aActor, aItem, aSlot);
}

void FakeEngine::Unequips(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    Unequip(aActor, aItem, aSlot);
}

void FakeEngine::ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t) noexcept
{
    ScopedEquipOverride _;
    Equip(aActorFormId, aItemFormId, aEquipSlotFormId);
}

void FakeEngine::ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t) noexcept
{
    ScopedEquipOverride _;
    Unequip(aActorFormId, aItemFormId, aEquipSlotFormId);
}

// EquipHook, then the engine: a hand holds one weapon, an armor takes its slots from what wore them; what it
// replaces comes off through the unequip, under the hook's ScopedUnequipOverride (no event).
void FakeEngine::Equip(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    const auto cActor = Actors.find(aActor);
    const auto cForm = ItemForms.find(aItem);
    if (cActor == Actors.end() || cForm == ItemForms.end())
        return;
    if (cActor->second.Copy && !ScopedEquipOverride::IsOverriden())
        return;
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(
            EquipmentChangeEvent{.ActorId = aActor, .ItemId = aItem, .EquipSlotId = aSlot, .Count = 1});

    ScopedUnequipOverride _;
    const bool cWeapon = cForm->second.Type == kWeapon;
    const bool cLeft = cWeapon && aSlot == kLeftHand;
    std::vector<std::pair<uint32_t, uint32_t>> replaced; // item, slot
    for (const auto& cEntry : cActor->second.Items.Entries)
    {
        const uint32_t cWornId = ToGame(cEntry.BaseId);
        const auto cWornForm = ItemForms.find(cWornId);
        if (cWornForm == ItemForms.end() || cWornId == aItem)
            continue;
        if (cWeapon && cWornForm->second.Type == kWeapon && (cLeft ? cEntry.ExtraWornLeft : cEntry.ExtraWorn))
            replaced.emplace_back(cWornId, aSlot);
        else if (!cWeapon && cEntry.ExtraWorn && (cWornForm->second.Slots & cForm->second.Slots))
            replaced.emplace_back(cWornId, 0);
    }
    // Ours (EquipManager::Equip, not applied at once) puts no armor over armor: measured, the copy of a player that
    // swapped iron armor for steel wears the steel after InventoryService's unequip-all, equip, re-equip.
    if (!cWeapon && !replaced.empty() && ScopedEquipOverride::IsOverriden())
        return;
    for (const auto& [cItem, cSlot] : replaced)
        Unequip(aActor, cItem, cSlot);

    auto& entry = EntryOf(cActor->second, aItem);
    (cLeft ? entry.ExtraWornLeft : entry.ExtraWorn) = true;
}

// UnEquipHook, then the engine: off that hand (both for no hand's slot).
void FakeEngine::Unequip(uint32_t aActor, uint32_t aItem, uint32_t aSlot)
{
    const auto cActor = Actors.find(aActor);
    if (cActor == Actors.end())
        return;
    if (cActor->second.Copy && !ScopedEquipOverride::IsOverriden() && !ScopedInventoryOverride::IsOverriden() &&
        !ScopedItemRemovalOverride::IsOverriden())
        return;
    if (!cActor->second.Copy && !ScopedUnequipOverride::IsOverriden())
        World::Get().GetRunner().Trigger(EquipmentChangeEvent{
            .ActorId = aActor, .ItemId = aItem, .EquipSlotId = aSlot, .Count = 1, .Unequip = true});

    const GameId cId = ServerId(aItem);
    for (auto& entry : cActor->second.Items.Entries)
        if (entry.BaseId == cId)
        {
            if (aSlot != kLeftHand)
                entry.ExtraWorn = false;
            if (aSlot != kRightHand)
                entry.ExtraWornLeft = false;
        }
}

std::set<std::tuple<uint32_t, uint32_t, bool>> FakeEngine::Worn(uint32_t aActor) const
{
    std::set<std::tuple<uint32_t, uint32_t, bool>> worn;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
        {
            if (cEntry.ExtraWorn)
                worn.emplace(cEntry.BaseId.ModId, cEntry.BaseId.BaseId, false);
            if (cEntry.ExtraWornLeft)
                worn.emplace(cEntry.BaseId.ModId, cEntry.BaseId.BaseId, true);
        }
    return worn;
}

// Actor::GetEquipment: the worn entries.
Inventory FakeEngine::GetActorEquipment(uint32_t aActorFormId) const noexcept
{
    Inventory equipment;
    if (const auto it = Actors.find(aActorFormId); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            if (cEntry.IsWorn())
                equipment.Entries.push_back(cEntry);
    return equipment;
}

// TESObjectREFR::GetWornArmor: the worn armors.
Inventory FakeEngine::GetActorWornArmor(uint32_t aActorFormId) const noexcept
{
    Inventory armor = GetActorEquipment(aActorFormId);
    std::erase_if(armor.Entries, [&](const Inventory::Entry& acEntry) {
        return GetFormType(ToGame(acEntry.BaseId)) != kArmor;
    });
    return armor;
}

void FakeEngine::AddItems(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    const auto cActor = Actors.find(aActor);
    if (cActor == Actors.end() || !aCount)
        return;
    const GameId cId = ServerId(aItem);
    auto& entries = cActor->second.Items.Entries;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& acEntry) { return acEntry.BaseId == cId; });
    if (it == entries.end())
    {
        if (aCount < 0)
            return;
        Inventory::Entry entry;
        entry.BaseId = cId;
        entries.push_back(entry);
        it = std::prev(entries.end());
    }
    if (it->Count + aCount <= 0)
    {
        if (it->IsWorn())
            Unequip(aActor, aItem, 0);
        std::erase_if(entries, [&](const auto& acEntry) { return acEntry.BaseId == cId; });
        return;
    }
    it->Count += aCount;
}

void FakeEngine::GetsItem(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        Inventory::Entry item;
        item.BaseId = ServerId(aItem);
        item.Count = aCount;
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item));
    }
    AddItems(aActor, aItem, aCount);
}

void FakeEngine::LosesItem(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    if (!ScopedInventoryOverride::IsOverriden())
    {
        Inventory::Entry item;
        item.BaseId = ServerId(aItem);
        item.Count = -aCount;
        World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item));
    }
    AddItems(aActor, aItem, -aCount);
}

void FakeEngine::Drops(uint32_t aActor, uint32_t aItem, int32_t aCount)
{
    Inventory::Entry item;
    item.BaseId = ServerId(aItem);
    item.Count = -aCount;
    World::Get().GetRunner().Trigger(InventoryChangeEvent(aActor, item, true));
    ScopedInventoryOverride _;
    AddItems(aActor, aItem, -aCount);
}

int32_t FakeEngine::CountOf(uint32_t aActor, uint32_t aItem) const
{
    int32_t count = 0;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            if (cEntry.BaseId.BaseId == (aItem & 0x00FFFFFF))
                count += cEntry.Count;
    return count;
}

std::map<std::pair<uint32_t, uint32_t>, int32_t> FakeEngine::Carried(uint32_t aActor) const
{
    std::map<std::pair<uint32_t, uint32_t>, int32_t> carried;
    if (const auto it = Actors.find(aActor); it != Actors.end())
        for (const auto& cEntry : it->second.Items.Entries)
            carried[{cEntry.BaseId.ModId, cEntry.BaseId.BaseId}] += cEntry.Count;
    return carried;
}

// Added (a worn one equipped, as SetInventory's adds do) or removed.
void FakeEngine::ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept
{
    const uint32_t cItem = ToGame(aItem.BaseId);
    AddItems(aObjectFormId, cItem, aItem.Count);
    if (aItem.Count > 0 && aItem.IsWorn())
    {
        ScopedEquipOverride _;
        Equip(aObjectFormId, cItem, aItem.ExtraWornLeft ? kLeftHand : kRightHand);
    }
}

WeaponState FakeEngine::GetWeaponState(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() ? it->second.Weapon : WeaponState::Sheathed;
}

void FakeEngine::SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Weapon = aDraw ? WeaponState::Drawn : WeaponState::Sheathed;
}

void FakeEngine::PlacesMarker(const glm::vec3& acPosition, uint32_t aWorldSpace)
{
    Vector3_NetQuantize position;
    position.x = acPosition.x;
    position.y = acPosition.y;
    position.z = acPosition.z;
    World::Get().GetRunner().Trigger(SetWaypointEvent(position, aWorldSpace));
    Waypoint = Marker{acPosition, aWorldSpace};
}

void FakeEngine::ChoosesDifficulty(uint32_t aDifficulty)
{
    Difficulty = aDifficulty;
    World::Get().GetRunner().Trigger(DifficultyChangeEvent{aDifficulty});
}

void FakeEngine::PlayerSwitchesRace(uint32_t aRace)
{
    Npcs[Actors.at(kPlayer).Base].Race = aRace;
    Player.Base = std::format("race {:X}", aRace);
    Player.BaseFlags |= 0x2000000; // CHANGE_NPC_RACE
    if (InSession)
        World::Get().GetRunner().Trigger(PlayerFormChangeEvent{});
}

void FakeEngine::PlayerUsesRaceMenu(const std::function<void()>& acChanges)
{
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", true});
    acChanges();
    World::Get().GetRunner().Trigger(MenuEvent{"RaceSex Menu", false});
}

std::string FakeEngine::PlayerId() noexcept
{
    return std::format("id:{:016x}", std::hash<std::string>{}(Name));
}

PlayerRecord FakeEngine::ReadPlayer() const noexcept
{
    PlayerRecord record = Player;
    const Actor& cPlayer = Actors.at(kPlayer);
    // As JoinBridge: what the player carries, from its inventory.
    record.Items = cPlayer.Items;
    record.Cell = GameId(0, cPlayer.Cell);
    record.Position = {cPlayer.Position[0], cPlayer.Position[1], cPlayer.Position[2]};
    return record;
}

void FakeEngine::ApplyPlayer(const PlayerRecord& acRecord) noexcept
{
    Actor& cPlayer = Actors.at(kPlayer);
    Player = acRecord;
    Player.Key.clear();
    Player.Items = {};
    cPlayer.Items = acRecord.Items;
    // As JoinBridge: the joining player stands where the save put it, beside the host.
    Player.Cell = GameId(0, cPlayer.Cell);
    Player.WorldSpace = {};
    Player.Position = {cPlayer.Position[0], cPlayer.Position[1], cPlayer.Position[2]};
}

std::optional<IJoinBridge::SaveFiles> FakeEngine::SaveNow(const std::string& acName) noexcept
{
    if (MadeSave)
        return *std::exchange(MadeSave, std::nullopt);
    if (!PendingSave)
        PendingSave = acName;
    return std::nullopt;
}

bool FakeEngine::WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept
{
    PendingLoad = {acName, acFiles};
    return true;
}

// The save's game file is the player's record; its co-save the records SaveService writes.
void FakeEngine::RunMainLoop() noexcept
{
    if (const auto cName = std::exchange(PendingSave, std::nullopt))
    {
        if (RefusesSaves || !World::Get().ctx().at<SaveService>().OnSaveRequested())
            MadeSave = std::optional<SaveFiles>{};
        else
        {
            Saves.push_back(*cName);
            Wire::Buffer buffer;
            Wire::Buffer::Writer writer(&buffer);
            ReadPlayer().Serialize(writer);
            MadeSave = SaveFiles{{reinterpret_cast<const char*>(buffer.GetData()), writer.Size()}, World::Get().ctx().at<SaveService>().WriteCoSave()};
        }
    }
    const auto cLoad = std::exchange(PendingLoad, std::nullopt);
    if (!cLoad)
        return;
    const auto& [cName, cFiles] = *cLoad;
    Wire::Buffer::Reader reader(std::span(reinterpret_cast<const uint8_t*>(cFiles.Game.data()), cFiles.Game.size()));
    PlayerRecord saved;
    saved.Deserialize(reader);
    Player = saved;
    Player.Items = {};
    Actor& player = Actors.at(kPlayer);
    player.Items = saved.Items;
    player.Cell = saved.Cell.BaseId;
    std::copy(saved.Position.begin(), saved.Position.end(), player.Position);
    LoadCell(player.Cell);
    auto& saves = World::Get().ctx().at<SaveService>();
    saves.RevertCoSave();
    saves.ReadCoSave(cFiles.CoSave, CharacterRecords::kVersion);
    AtMainMenu = false;
    Loaded.push_back(cName);
    World::Get().GetRunner().Trigger(GameLoadedEvent{});
}

bool FakeEngine::Autosaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested(true))
        return false;
    Saves.push_back(acName);
    return true;
}

// As Save_Impl's hook: SaveService decides first.
bool FakeEngine::PlayerSaves(const std::string& acName)
{
    if (!World::Get().ctx().at<SaveService>().OnSaveRequested())
        return false;
    Saves.push_back(acName);
    return true;
}

// From the map ("Remove It"): the only removal our hook raises.
void FakeEngine::RemovesMarker()
{
    World::Get().GetRunner().Trigger(RemoveWaypointEvent());
    Waypoint.reset();
}

// HookSetGlobalTimeMultiplier: a change of this game's own, raised while connected.
void FakeEngine::SlowsTime(float aMultiplier)
{
    if (aMultiplier != WantedTimeScale && World::Get().GetTransport().IsConnected())
        World::Get().GetRunner().Trigger(TimeScaleEvent{aMultiplier});
    WantedTimeScale = aMultiplier;
}

void FakeEngine::CastsSpell(uint32_t aCaster, uint32_t aSpell, uint32_t aSource, uint32_t aTarget)
{
    const auto cActor = Actors.find(aCaster);
    if (cActor == Actors.end())
        return;
    if (!cActor->second.Copy)
        World::Get().GetRunner().Trigger(SpellCastEvent(aCaster, aSpell, aTarget, aSource, false, cActor->second.Loaded));
    Casts.push_back({aCaster, aSpell, aSource, aTarget});
}

void FakeEngine::SpellHits(uint32_t aTarget, uint32_t aCaster, uint32_t aSpell, uint32_t aEffect, float aMagnitude)
{
    const auto cTarget = Actors.find(aTarget);
    if (cTarget == Actors.end())
        return;
    const bool cOurCast = Actors.contains(aCaster) && !Actors.at(aCaster).Copy;
    if (cTarget->second.Copy)
    {
        if (!cOurCast)
            return;
        AddTargetEvent event;
        event.TargetID = aTarget;
        event.CasterID = aCaster;
        event.SpellID = aSpell;
        event.EffectID = aEffect;
        event.Magnitude = aMagnitude;
        World::Get().GetRunner().Trigger(event);
        return;
    }
    Effects.push_back({aTarget, aCaster, aSpell, aEffect});
}

void FakeEngine::Fights(uint32_t aActor, uint32_t aTarget)
{
    auto& ai = Actors.at(aActor).Ai;
    ai.CombatState = aTarget ? 1 : 0;
    ai.CombatTargetId = aTarget;
    World::Get().GetRunner().Trigger(AiStateChangeEvent{aActor});
}

void FakeEngine::Burns(uint32_t aActor, uint32_t aEffect, bool aOn)
{
    auto& visuals = Actors.at(aActor).Visuals;
    aOn ? (void)visuals.insert(aEffect) : (void)visuals.erase(aEffect);
    World::Get().GetRunner().Trigger(EffectVisualsChangeEvent{aActor, 0});
}

std::vector<IActorBridge::ActiveEffectInfo> FakeEngine::GetActorActiveEffects(uint32_t aFormId) const noexcept
{
    std::vector<ActiveEffectInfo> effects;
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        for (const uint32_t cId : it->second.Visuals)
            effects.push_back({.EffectFormId = cId});
    return effects;
}

void FakeEngine::Strikes(uint32_t aHitter, uint32_t aHittee, float aDamage)
{
    const auto cHittee = Actors.find(aHittee);
    if (cHittee == Actors.end())
        return;
    World::Get().GetRunner().Trigger(HitEvent(aHitter, aHittee));
    if (aHittee == kPlayer)
    {
        World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage));
        DamageActorHealth(aHittee, aHitter, aDamage);
        return;
    }
    if (cHittee->second.Copy && cHittee->second.Player)
        return; // another game's player: its own game takes the hit
    if (aHitter == kPlayer)
    {
        World::Get().GetRunner().Trigger(HealthChangeEvent(aHittee, -aDamage, aHitter));
        if (cHittee->second.Copy)
            return;
    }
    DamageActorHealth(aHittee, aHitter, aDamage);
}

void FakeEngine::Shoots(uint32_t aShooter, uint32_t aSpell, const glm::vec3& acOrigin)
{
    ProjectileLaunchedEvent event{};
    event.Origin = acOrigin;
    event.ShooterID = aShooter;
    event.SpellID = aSpell;
    event.ParentCellID = Actors.at(aShooter).Cell;
    World::Get().GetRunner().Trigger(event);
    Shots.push_back({aShooter, aSpell, acOrigin});
}

void FakeEngine::Activates(uint32_t aActor, uint32_t aObject)
{
    ActivateEvent event;
    event.ObjectFormId = aObject;
    event.ActivatorFormId = aActor;
    event.ObjectParentCellFormId = References.at(aObject).Cell;
    World::Get().GetRunner().Trigger(event);
    Activations.emplace_back(aObject, aActor);
    ToggleDoor(aObject);
}

// The alias clear hooks report their quest after the engine removes the fill, without a stage/flag event.
void FakeEngine::ClearReferenceAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Aliases)
        if (fill.AliasId == aAlias)
        {
            fill = QuestAliasFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

void FakeEngine::ClearLocationAlias(uint32_t aQuest, uint32_t aAlias)
{
    for (auto& fill : Quests.at(aQuest).Locations)
        if (fill.AliasId == aAlias)
        {
            fill = QuestLocationFill{aAlias};
            QuestChanged(aQuest);
            return;
        }
}

// StatsMenu::ModifyPerkPoints writes directly; its hook marks both default globals after either signed delta.
void FakeEngine::ModifyBeastPerks(bool aVampire, int32_t aDelta)
{
    Globals.at(BeastPerkGlobals[aVampire ? 1 : 0]) += static_cast<float>(aDelta);
    GlobalWrites.insert(BeastPerkGlobals.begin(), BeastPerkGlobals.end());
}

void FakeEngine::Knock(const BodyKey& acKey, const glm::vec3& acVelocity, const glm::vec3& acSpin)
{
    Body& body = Bodies.at(acKey);
    body.Flight.Resting = false;
    body.Flight.Body.LinearVelocity = acVelocity;
    body.Flight.Body.AngularVelocity = acSpin;
    body.Awake = true;
    body.Tick = 0;
}

bool FakeEngine::Push(const BodyPush& acPush)
{
    const auto it = Bodies.find(acPush.Key);
    if (it == Bodies.end() || it->second.Held)
        return false;
    Knock(acPush.Key, it->second.Flight.Body.LinearVelocity + acPush.Impulse, it->second.Flight.Body.AngularVelocity);
    return true;
}

bool FakeEngine::Drive(const BodyFrame& acFrame)
{
    const auto it = Bodies.find(acFrame.Key);
    if (it == Bodies.end())
        return false;
    Body& body = it->second;
    body.Held = true;
    body.Awake = false;
    body.Flight.Body.Position = acFrame.Position;
    body.Flight.Body.Rotation = acFrame.Rotation;
    ++body.Drives;
    return true;
}

bool FakeEngine::Free(const BodyFrame& acFrame)
{
    const auto it = Bodies.find(acFrame.Key);
    if (it == Bodies.end() || !it->second.Held)
        return false;
    Body& body = it->second;
    body.Held = false;
    body.Flight.Body = acFrame;
    body.Flight.Body.Key = {};
    Knock(acFrame.Key, acFrame.LinearVelocity, acFrame.AngularVelocity);
    return true;
}

HostBodies FakeEngine::TakeBodies(uint64_t aTick)
{
    constexpr uint64_t cStepMs = 16; // Havok's substeps (fTimePerSubStep)
    HostBodies bodies;
    for (auto& [cKey, body] : Bodies)
    {
        if (!body.Awake)
            continue;
        if (!body.Tick)
            body.Tick = aTick;
        for (; body.Tick + cStepMs <= aTick && !body.Flight.Resting; body.Tick += cStepMs)
            body.Flight.Step(cStepMs / 1000.f);
        BodyFrame frame = body.Flight.Body;
        frame.Key = cKey;
        if (body.Flight.Resting)
        {
            body.Awake = false;
            bodies.Rested.push_back({cKey, frame.Position, frame.Rotation});
        }
        else
            bodies.Awake.push_back(frame);
    }
    return bodies;
}
