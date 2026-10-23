#include "FakeEngine.h"

#include <Events/ContainerChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/PartyNativeEvent.h>
#include <Structs/GridCellCoords.h>
#include <Papyrus/PartyNatives.h>
#include <World.h>

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

std::vector<ObjectChange> FakeEngine::RowsOf(uint32_t aReference) const
{
    const auto& cReference = References.at(aReference);
    std::vector<ObjectChange> rows{LockRow(aReference, cReference)};
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
    case ObjectChange::Kind::kLock:
        reference.Locked = (acChange.Value & kLocked) != 0;
        reference.LockLevel = static_cast<uint8_t>(acChange.Value & 0xFF);
        break;
    case ObjectChange::Kind::kItems: SetCount(reference, FormIdOf(acChange.Item), static_cast<int32_t>(acChange.Value)); break;
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
    Actors[cId] =
        Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z}, .Made = m_made};
    return cId;
}

void FakeEngine::PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition)
{
    Actors[aFormId] = Actor{.Base = aBase, .Cell = aCell, .Position = {acPosition.x, acPosition.y, acPosition.z}};
}

void FakeEngine::CheckHealth(Actor& aActor) noexcept
{
    if (aActor.Copy || aActor.Dead || aActor.Values.ActorValuesList[kHealth] > 0.f)
        return;
    (aActor.Essential ? aActor.Bleeding : aActor.Dead) = true;
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
        it->second.Dead = true;
        it->second.Bleeding = false;
    }
}

// Actor::Respawn: alive again, values restored.
void FakeEngine::RespawnActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Dead = it->second.Bleeding = false;
        it->second.Values = FullValues();
    }
}

void FakeEngine::ResurrectActor(uint32_t aFormId) noexcept
{
    RespawnActor(aFormId);
}

bool FakeEngine::DamageActorHealth(uint32_t aFormId, uint32_t, float aDamage) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return false;
    it->second.Values.ActorValuesList[kHealth] -= aDamage;
    CheckHealth(it->second);
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
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return 0.f;
    const auto& cValues = it->second.Values.ActorValuesList;
    const auto cValue = cValues.find(static_cast<uint32_t>(aIndex));
    return cValue != cValues.end() ? cValue->second : 0.f;
}

float FakeEngine::GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept
{
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
        it->second.Values.ActorValuesList[static_cast<uint32_t>(aIndex)] = aValue;
}

// ForceMode PERMANENT (0) sets the maximum, DAMAGE (2) the current value up to it; at 0 health, as a hit.
void FakeEngine::ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    auto& values = it->second.Values;
    const auto cIndex = static_cast<uint32_t>(aIndex);
    if (aForceMode == 0)
        values.ActorMaxValuesList[cIndex] = aValue;
    else if (aForceMode == 2)
        values.ActorValuesList[cIndex] = std::min(aValue, values.ActorMaxValuesList[cIndex]);
    if (cIndex == kHealth)
        CheckHealth(it->second);
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
                        .Created = true, .Made = m_made};
    return cId;
}

// A player's copy: a temporary NPC made from their appearance, and an actor of it.
uint32_t FakeEngine::CreateCustomActor(const std::string&, uint32_t) noexcept
{
    return SpawnActorFromNpc(NewTemporary());
}

// The player's is a travel (MoveTo_Impl queues it as a door's): the cells load and unload, a teleport is reported.
void FakeEngine::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    const uint32_t cFrom = it->second.Cell;
    it->second.Cell = aCellId;
    const auto cExterior = Exteriors.find(aCellId);
    it->second.WorldSpace = cExterior != Exteriors.end() ? cExterior->second.WorldSpace : 0;
    it->second.Position[0] = aX;
    it->second.Position[1] = aY;
    it->second.Position[2] = aZ;
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
