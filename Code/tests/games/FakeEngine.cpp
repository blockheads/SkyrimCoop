#include "FakeEngine.h"

#include <Events/ContainerChangeEvent.h>
#include <Events/LockChangeEvent.h>
#include <Events/PartyNativeEvent.h>
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

void FakeEngine::UnloadCell(uint32_t aCell)
{
    Attached.erase(aCell);
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

namespace
{
// The engine's temporary forms (a reference placed at run time, a copy's NPC): 0xFF ids, counted up.
uint32_t s_nextTemporary = 0xFF000800;
} // namespace

uint32_t FakeEngine::LoadNpc(uint32_t aBase, uint32_t aCell)
{
    const uint32_t cId = s_nextTemporary++;
    Actors[cId] = Actor{.Base = aBase, .Cell = aCell};
    return cId;
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
        it->second.Dead = true;
}

void FakeEngine::RespawnActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Dead = false;
}

bool FakeEngine::IsActorDisabled(uint32_t aFormId) const noexcept
{
    const auto it = Actors.find(aFormId);
    return it != Actors.end() && it->second.Disabled;
}

void FakeEngine::EnableActor(uint32_t aFormId) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Disabled = false;
}

// A copy: a new temporary actor of that NPC, where this game's player is (as PlaceAtMe from the player).
uint32_t FakeEngine::SpawnActorFromNpc(uint32_t aNpcFormId) noexcept
{
    const uint32_t cId = s_nextTemporary++;
    Actors[cId] = Actor{.Base = aNpcFormId, .Cell = Actors.at(kPlayer).Cell, .WorldSpace = Actors.at(kPlayer).WorldSpace};
    return cId;
}

// A player's copy: a temporary NPC made from their appearance, and an actor of it.
uint32_t FakeEngine::CreateCustomActor(const std::string&, uint32_t) noexcept
{
    const uint32_t cNpc = s_nextTemporary++;
    return SpawnActorFromNpc(cNpc);
}

// The player's is a travel (MoveTo_Impl queues it as a door's): the cells load and unload, a teleport is reported.
void FakeEngine::MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept
{
    const auto it = Actors.find(aFormId);
    if (it == Actors.end())
        return;
    const uint32_t cFrom = it->second.Cell;
    it->second.Cell = aCellId;
    it->second.Position[0] = aX;
    it->second.Position[1] = aY;
    it->second.Position[2] = aZ;
    if (aFormId == kPlayer)
    {
        if (cFrom != aCellId)
            UnloadCell(cFrom);
        LoadCell(aCellId);
        Teleported = true;
    }
}

void FakeEngine::SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
    {
        it->second.Rotation[0] = aX;
        it->second.Rotation[2] = aZ;
    }
}

void FakeEngine::DeleteActor(uint32_t aFormId) noexcept
{
    if (aFormId != kPlayer)
        Actors.erase(aFormId);
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

void FakeEngine::SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept
{
    if (const auto it = Actors.find(aFormId); it != Actors.end())
        it->second.Values = acValues;
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
