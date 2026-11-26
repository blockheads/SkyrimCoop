#include <Games/PartyItems.h>
#include <Games/Animation/GraphStream.h>
#include <Events/ItemsChangedEvent.h>
#include <Events/GameLoadFailedEvent.h>
#include <Events/GameLoadedEvent.h>
#include <Events/ActorRenamedEvent.h>
// The host's side in this process: HostService hosting as in Skyrim (the network, HostAuthority, the host's own
// game joining in process), its reads from the fake engine's world and the real CharacterService; and the systems
// and services CharacterService calls that tier 2 leaves out (animation, face tints, AI state, effects).
#include "FakeEngine.h"
#include <Bridges/IDevToolsBridge.h>
#include <Services/DevCommands.h>
#include <Services/CrimeService.h>
#include <Messages/ContainerTransferRequest.h>
#include <Games/Overrides.h>

#include "FakeHostEngine.h"
#include <Bridges/IActorBridge.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/Relayed.h>
#include <ScriptValues.h>
#include <Messages/SummonRequest.h>
#include <CreatedIds.h>
#include <FormIds.h>
#include <SessionIds.h>
#include <Events/DifficultyChangeEvent.h>
#include <Events/GameSavingEvent.h>
#include <Events/SaveWrittenEvent.h>
#include <Services/SaveLibrary.h>
#include <SessionIds.h>
#include <Bridges/IJoinBridge.h>
#include <Services/JoinService.h>
#include <Services/SaveService.h>
#include <Events/UpdateEvent.h>
#include <RunningGame.h>
#include <Services/AiStateService.h>
#include <Services/CharacterService.h>
#include <Services/DialogueService.h>
#include <Services/EffectVisualsService.h>
#include <Services/Host/HostAuthority.h>
#include <Services/Host/HostConversation.h>
#include <Services/Host/HostGame.h>
#include <Services/Host/HostNetwork.h>
#include <Services/HostService.h>
#include <Services/Hosting.h>
#include <Services/Host/HostAggro.h>
#include <Services/Host/HostTakeBacks.h>
#include <Services/Host/HostTraps.h>
#include <Services/BodyService.h>
#include <Services/CrimeService.h>
#include <Services/InventoryService.h>
#include <Bridges/IPlayerBridge.h>
#include <Systems/AnimationSystem.h>
#include <Systems/FaceGenSystem.h>
#include <Components/LocalAnimationComponent.h>
#include <Components/RemoteAnimationComponent.h>
#include <Components/FormIdComponent.h>
#include <Components/LocalComponent.h>
#include <Components/FaceGenComponent.h>
#include <World.h>

// The host's game on the fake engine: what HostEngine does in Skyrim (the real HostService runs on it).
FakeHostEngine::FakeHostEngine(World& aWorld, FakeEngine& aEngine) noexcept
    : m_world(aWorld), m_engine(aEngine), m_physics(aEngine)
{
}

std::optional<std::vector<Followers::State>> FakeHostEngine::ReadFollowers() const noexcept
{
    FakeEngine& engine = m_engine;
    auto followers = engine.PartyFollowers;
    for (auto& follower : followers)
    {
        const auto it = engine.Actors.find(follower.Reference);
        if (it == engine.Actors.end())
            continue;
        follower.Session = Utils::GetSessionIdByFormId(follower.Reference).value_or(0);
        follower.At = {it->second.WorldSpace, it->second.Cell, {it->second.Position[0], it->second.Position[1], it->second.Position[2]}};
        follower.Waiting = engine.GetActorValue(follower.Reference, 95) != 0.f;
        follower.Dead = engine.IsActorDead(follower.Reference);
    }
    return followers;
}

std::vector<Followers::Order> FakeHostEngine::TakeFollowerOrders() const noexcept
{
    FakeEngine& engine = m_engine;
    return std::exchange(engine.FollowerOrders, {});
}

void FakeHostEngine::SetFollowerTargets(const std::vector<Followers::Target>& acTargets) const noexcept
{
    FakeEngine& engine = m_engine;
    engine.FollowerTargets = acTargets;
}

void FakeHostEngine::SetPartyThreats(const PartyThreat::State& acState) const noexcept
{ m_engine.PartyThreats = acState;
}

std::optional<HostSky> FakeHostEngine::ReadWeather() const noexcept
{
    FakeEngine& engine = m_engine;
    if (!engine.Weather.BaseId)
        return std::nullopt;
    return HostSky{.Weather = engine.Weather, .Forced = static_cast<bool>(engine.ForcedWeather),
                   .Region = engine.WeatherRegion ? GameId{0, engine.WeatherRegion} : GameId{}};
}

std::optional<GameId> FakeHostEngine::RegionWeather(const GameId& acRegion) const noexcept
{
    FakeEngine& engine = m_engine;
    // HostEngine::RegionWeather: none set since the load is set as the shown weather's change would.
    if (!engine.RegionWeather.contains(acRegion.BaseId))
        engine.SetRegionWeather(acRegion.BaseId);
    const auto it = engine.RegionWeather.find(acRegion.BaseId);
    return it == engine.RegionWeather.end() ? std::nullopt : std::optional(it->second);
}

std::optional<DetectionRules::EyeReading> FakeHostEngine::ReadEye(uint32_t aSessionId) const noexcept
{
    FakeEngine& engine = m_engine;
    // HostEngine::ReadEye: a loaded copy's 41408 and 46877.
    const uint32_t cCopy = Utils::GetFormIdBySessionId(aSessionId);
    const auto it = engine.Actors.find(cCopy);
    if (it == engine.Actors.end() || !it->second.Copy || !it->second.Loaded)
        return std::nullopt;
    DetectionRules::EyeReading reading;
    reading.Level = engine.HighestDetection(cCopy, reading.Seeing);
    reading.Stealth = engine.LowestStealthPoints(cCopy);
    return reading;
}

std::vector<HostGlobal> FakeHostEngine::ReadGlobals() const noexcept
{
    FakeEngine& engine = m_engine;
    std::vector<HostGlobal> globals;
    for (const auto& [cId, cValue] : engine.Globals)
        if (!engine.SystemQuestList.IsGlobal(cId)) // as HostEngine: each player's own
            globals.push_back({.Id = GameId{0, cId}, .Value = cValue});
    return globals;
}

std::vector<HostGlobal> FakeHostEngine::TakeGlobalChanges() const noexcept
{
    FakeEngine& engine = m_engine;
    std::vector<HostGlobal> globals;
    for (const uint32_t cId : std::exchange(engine.GlobalWrites, {}))
        if (!engine.SystemQuestList.IsGlobal(cId))
            globals.push_back({.Id = GameId{0, cId}, .Value = engine.Globals.at(cId)});
    return globals;
}

std::vector<QuestState> FakeHostEngine::ReadQuests() const noexcept
{
    FakeEngine& engine = m_engine;
    std::vector<QuestState> quests;
    for (const auto& [cId, cQuest] : engine.Quests)
        if (!engine.SystemQuestList.Contains(cId))
            quests.push_back(cQuest);
    return quests;
}

std::vector<QuestState> FakeHostEngine::TakeQuestChanges() const noexcept
{
    FakeEngine& engine = m_engine;
    std::vector<QuestState> quests;
    for (const uint32_t cId : engine.QuestWrites.Take())
        if (const auto it = engine.Quests.find(cId); it != engine.Quests.end() && !engine.SystemQuestList.Contains(cId))
            quests.push_back(it->second);
    return quests;
}

void FakeHostEngine::SetHour(float aHours) noexcept
{
    FakeEngine& engine = m_engine;
    engine.Calendar.Time = aHours;
}

void FakeHostEngine::AdvanceTime(float aHours) noexcept
{
    FakeEngine& engine = m_engine;
    MainLoopTasks::Queue([&engine, aHours] {
        DateTime time(engine.Calendar);
        const float cScale = time.m_timeModel.TimeScale;
        time.m_timeModel.TimeScale = 3600.f;
        time.Update(static_cast<uint64_t>(aHours * 1000.f));
        time.m_timeModel.TimeScale = cScale;
        engine.Calendar = time.m_timeModel;
    });
}

void FakeHostEngine::RecordActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t aOpenState) noexcept
{
    FakeEngine& engine = m_engine;
    // The fake engine records its own world's changes where they happen (FakeEngine::Recorded).
}

void FakeHostEngine::RecordAnimation(uint32_t aObjectFormId, const std::string& acAnimation, const std::string& acEvent, uint32_t aPullerFormId) noexcept
{
}

void FakeHostEngine::RecordItems(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept
{
}

void FakeHostEngine::Activate(const GameId& acObject, uint32_t aNpcId, uint32_t aActivatorId) noexcept
{
    FakeEngine& engine = m_engine;
    const uint32_t cObject = aNpcId ? Utils::GetFormIdBySessionId(aNpcId) : FormIdOf(acObject);
    const uint32_t cActivator = Utils::GetFormIdBySessionId(aActivatorId);
    if (!cActivator || (aNpcId && engine.IsActorCopy(cObject)))
        return;
    engine.RunsActivation(cObject, cActivator);
    // As HostEngine::Activate: a lever furniture's pull, which 17420 gives this game's player only, recorded.
    if (const auto cLever = engine.References.find(cObject); cLever != engine.References.end() && cLever->second.Lever &&
        !cLever->second.ActivationBlocked)
    {
        cLever->second.GraphEvents.push_back(FakeEngine::kPull);
        ObjectChange pull{.Type = ObjectChange::Kind::kAnimation, .Object = SessionIdOf(cObject), .Event = FakeEngine::kPull};
        pull.Value = aActivatorId;
        engine.Recorded.push_back(std::move(pull));
    }
}

void FakeHostEngine::Summon(uint32_t aCasterId, const SummonRequest& acSummon) noexcept
{
    FakeEngine& engine = m_engine;
    const uint32_t cCopy = Utils::GetFormIdBySessionId(aCasterId);
    const uint32_t cSpell = FormIdOf(acSummon.SpellId);
    if (cCopy && engine.IsOtherPlayer(cCopy) && engine.SpellForms.contains(cSpell) && engine.SpellForms.at(cSpell).Summons)
        engine.AddMagicTargetEffect(cCopy, cCopy, cSpell, FormIdOf(acSummon.EffectId), acSummon.Magnitude, acSummon.DualCast, false, false);
}

void FakeHostEngine::TouchTrap(const Payloads::TrapContact& acContact) noexcept
{
    FakeEngine& engine = m_engine;
    if (const uint32_t cCopy = Utils::GetFormIdBySessionId(acContact.TargetId); cCopy && engine.References.contains(FormIdOf(acContact.Trap)))
        engine.RunsTrapScript(FormIdOf(acContact.Trap), cCopy);
}

void FakeHostEngine::SetLock(const GameId& acObject, bool aLocked, uint8_t aLockLevel) noexcept
{
    FakeEngine& engine = m_engine;
    engine.SetLock(FormIdOf(acObject), aLocked, aLockLevel);
}

void FakeHostEngine::SetItems(const ObjectChange& acItems) noexcept
{
    FakeEngine& engine = m_engine;
    engine.SetItems(FormIdOf(acItems.Object), ItemForm(acItems.Item), static_cast<int32_t>(acItems.Value), acItems.Stacks);
}

Inventory::Entry FakeHostEngine::TransferContainer(const ContainerTransferRequest& acRequest, uint32_t aPlayer) noexcept
{
    FakeEngine& engine = m_engine;
    Inventory::Entry moved = acRequest.Item;
    const uint32_t cPlayer = Utils::GetFormIdBySessionId(aPlayer);
    const uint32_t cContainer = acRequest.ContainerActorId ? Utils::GetFormIdBySessionId(acRequest.ContainerActorId) : FormIdOf(acRequest.Container);
    const uint32_t cItem = ItemForm(moved.BaseId);
    const bool cTake = moved.Count > 0;
    const int32_t cAvailable = cTake ? (acRequest.ContainerActorId ? engine.CountOf(cContainer, cItem) : engine.ContainerCountOf(cContainer, cItem)) : engine.CountOf(cPlayer, cItem);
    moved.Count = std::min(std::abs(moved.Count), cAvailable) * (cTake ? 1 : -1);
    // As HostEngine::TransferContainer: a put of a party item's mirror moves the host's player's real stack.
    if (!cTake && moved.Count && PartyItems::IsParty(moved) && !acRequest.ContainerActorId)
    {
        auto& entries = engine.Actors.at(FakeEngine::kPlayer).Items.Entries;
        auto it = std::ranges::find_if(entries, [&](const auto& acEntry) {
            return acEntry.BaseId == moved.BaseId && !acEntry.IsQuestItem && acEntry.ExtraName == moved.ExtraName && acEntry.Count > 0;
        });
        if (it == entries.end()) // a quest object's real stack keeps its alias's name
            it = std::ranges::find_if(entries, [&](const auto& acEntry) { return acEntry.BaseId == moved.BaseId && acEntry.IsQuestItem && acEntry.Count > 0; });
        if (it == entries.end())
        {
            moved.Count = 0;
            return moved;
        }
        {
            ScopedInventoryOverride _;
            const int32_t cCount = std::min(-moved.Count, it->Count);
            Inventory::Entry real = *it;
            real.Count = cCount;
            if ((it->Count -= cCount) <= 0)
                entries.erase(it);
            auto& reference = engine.References.at(cContainer);
            std::vector<Inventory::Entry> stacks;
            for (const auto& cStack : reference.Stacks)
                if (cStack.BaseId == moved.BaseId)
                    stacks.push_back(cStack);
            stacks.push_back(real);
            engine.SetItems(cContainer, cItem, reference.Items[cItem] + cCount, stacks);
            if (const auto cAlias = engine.AliasStacks.find({FakeEngine::kPlayer, cItem}); cAlias != engine.AliasStacks.end())
            {
                engine.AliasStacks[{cContainer, cItem}] = cAlias->second;
                engine.AliasStacks.erase(cAlias);
            }
            moved.Count = -cCount;
            moved.IsQuestItem = true;
            return moved;
        }
    }
    // As HostEngine::MoveAliasStack: a quest alias's stack moves to this game's player, its arrival raised.
    bool aliasMoved = false;
    if (cTake && moved.Count && engine.AliasStacks.contains({cContainer, cItem}))
    {
        moved.IsQuestItem = true;
        engine.MovesStack(cContainer, FakeEngine::kPlayer, cItem, moved.Count, acRequest.Steal);
        aliasMoved = true;
    }
    // As HostEngine::TransferContainer: a take changes only the container, the player's own game its player; a put
    // moves from the player's copy here.
    if (moved.Count && !aliasMoved)
    {
        ScopedInventoryOverride _;
        if (!cTake)
            engine.GetsItem(cPlayer, moved);
        Inventory::Entry other = moved;
        other.Count = -other.Count;
        // As FindStack: a plain request matches a quest item's list too (ContainerTransfers::Stack drops the flag).
        const auto cStacks = acRequest.ContainerActorId ? engine.EntriesOf(cContainer, cItem) : std::vector<Inventory::Entry>{};
        if (!other.ContainsExtraData() && std::ranges::any_of(cStacks, [](const auto& acStack) { return acStack.IsQuestItem; }) &&
            std::ranges::none_of(cStacks, [](const auto& acStack) { return !acStack.ContainsExtraData(); }))
            other.IsQuestItem = true;
        if (acRequest.ContainerActorId)
            engine.GetsItem(cContainer, other);
        else
        {
            // Measured as HostEngine::TransferContainer does: the count before and after the removal.
            const int32_t cBefore = engine.ContainerCountOf(cContainer, cItem);
            engine.SetItems(cContainer, cItem, engine.References.at(cContainer).Items[cItem] - moved.Count);
            const int32_t cAfter = engine.ContainerCountOf(cContainer, cItem);
            moved.Count = std::clamp(cTake ? cBefore - cAfter : cAfter - cBefore, 0, std::abs(moved.Count)) * (cTake ? 1 : -1);
        }
    }
    if (moved.Count && acRequest.ContainerActorId) // as HostEngine::TransferContainer
        World::Get().GetRunner().Trigger(ItemsChangedEvent{cContainer});
    // A theft marks the item as the container's owner's (16059 on its target): an actor's NPC, else the
    // reference's owner, else its cell's.
    if (cTake && acRequest.Steal && moved.Count && !moved.IsQuestItem && moved.ExtraOwner == GameId{} && cItem != 0xF)
    {
        uint32_t owner{};
        if (acRequest.ContainerActorId) // its original leveled form, else its base (TESObjectREFR::GetItemsOwner)
        {
            const auto& cActor = engine.Actors.at(cContainer);
            owner = cActor.LeveledBase ? cActor.LeveledBase : cActor.Base ? cActor.Base : cContainer;
        }
        else if (const auto& cReference = engine.References.at(cContainer); cReference.Owner)
            owner = cReference.Owner;
        else if (const auto cCell = engine.CellOwners.find(engine.References.at(cContainer).Cell); cCell != engine.CellOwners.end())
            owner = cCell->second;
        if (owner)
            moved.ExtraOwner = GameId(0, owner);
    }
    return moved;
}

void FakeHostEngine::RaiseStoryEvent(const StoryEvent& acEvent) noexcept
{
    FakeEngine& engine = m_engine;
    engine.StoriesRaised.push_back(acEvent);
}

std::optional<GameId> FakeHostEngine::CreateItem(const Inventory::Entry& acItem, const ObjectChange::Placement& acPlacement) noexcept
{
    FakeEngine& engine = m_engine;
    return engine.CreateLoose(acItem, FormIdOf(acPlacement.Cell));
}

std::optional<Inventory::Entry> FakeHostEngine::TakeItem(const GameId& acObject) noexcept
{
    FakeEngine& engine = m_engine;
    return engine.TakeLoose(acObject);
}

std::optional<GameId> FakeHostEngine::DropPartyItem(const Inventory::Entry& acItem, uint32_t aPlayer) noexcept
{
    FakeEngine& engine = m_engine;
    return engine.DropsPartyItem(acItem, Utils::GetFormIdBySessionId(aPlayer));
}

bool FakeHostEngine::TakeQuestObject(const GameId& acObject) noexcept
{
    FakeEngine& engine = m_engine;
    return engine.TakeQuestObject(acObject);
}

std::vector<ObjectChange> FakeHostEngine::TakeObjectChanges() noexcept
{
    FakeEngine& engine = m_engine;
    return std::exchange(engine.Recorded, {});
}

std::vector<BodyKey> FakeHostEngine::TakeTouches() noexcept
{
    FakeEngine& engine = m_engine;
    std::vector<BodyKey> keys;
    for (const auto& cPush : std::exchange(engine.PlayerPushes, {}))
        keys.push_back(cPush.Key);
    for (const auto& cHit : std::exchange(engine.PlayerHits, {}))
        keys.push_back(cHit.Key);
    return keys;
}

std::vector<std::pair<GameId, glm::vec3>> FakeHostEngine::TakePlaced() noexcept
{
    FakeEngine& engine = m_engine;
    for (auto& [cKey, body] : engine.Bodies)
        body.PlacedByGame = false; // read: where the game put it (HostEngine::TakeObjectChanges clears PlacedByGame)
    return std::exchange(engine.Placed, {});
}

HostBodyReads FakeHostEngine::TakeBodies(uint64_t aTick, const std::function<uint32_t(uint32_t aFormId)>& aSessionOf) noexcept
{
    FakeEngine& engine = m_engine;
    const auto cOwner = [&](const PhysicsBodies::Bodies& acBodies) -> std::optional<BodyKey> {
        if (acBodies.Actor)
        {
            const uint32_t cSessionId = aSessionOf(acBodies.FormId);
            return cSessionId ? std::optional(BodyKey{.Actor = cSessionId}) : std::nullopt;
        }
        for (const auto& [cKey, cBody] : engine.Bodies)
            if (!cKey.Actor && FakeEngine::ReferenceOf(cKey) == acBodies.FormId)
                return BodyKey{.Object = cKey.Object};
        return std::nullopt;
    };
    engine.PhysicsTick = engine.PhysicsTime(m_world.GetHosting().GetNetwork()->GetTick());
    const auto cUpdate = m_physics.Take(aTick);
    HostBodyReads bodies;
    for (const auto& cAwake : cUpdate.Awake)
        if (const auto cKey = cOwner(cAwake))
            for (BodyFrame frame : cAwake.Read)
            {
                frame.Key = {cKey->Object, cKey->Actor, frame.Key.Index};
                if (cAwake.Held)
                    bodies.Held.push_back(frame.Key);
                if (!cKey->Actor)
                    engine.SetMoving(frame.Key, true);
                bodies.Awake.push_back(frame);
            }
    for (const auto& cRested : cUpdate.Rested)
        if (const auto cKey = cOwner(cRested))
            for (const BodyFrame& cRead : cRested.Read)
            {
                if (!cKey->Actor)
                    engine.SetMoving({cKey->Object, 0, cRead.Key.Index}, false);
                bodies.Rested.push_back({.Key = {cKey->Object, cKey->Actor, cRead.Key.Index}, .Position = cRead.Position,
                                         .Rotation = cRead.Rotation, .Knocked = cRead.Knocked});
            }
    return bodies;
}

std::optional<ObjectChange> FakeHostEngine::ReadContents(const GameId& acObject) const noexcept
{
    FakeEngine& engine = m_engine;
    return acObject.ModId == ObjectChange::kCreatedModId ? engine.ContentsOf(0xFF000000 | acObject.BaseId) : std::nullopt;
}

std::optional<std::vector<ObjectChange>> FakeHostEngine::ReadCellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const noexcept
{
    FakeEngine& engine = m_engine;
    return engine.CellState(acCell, acChanged);
}

bool FakeHostEngine::RunFor(uint32_t aGuestFormId, const LocalCall& acCall, std::function<void(LocalValue)> aOnReturn) noexcept
{
    m_engine.Queue(acCall, aGuestFormId, std::move(aOnReturn));
    return true;
}

// AnimationSystem and FaceGenSystem as their own (their files read the engine too): the components only.
void AnimationSystem::Setup(World& aWorld, entt::entity aEntity) noexcept
{
    aWorld.emplace_or_replace<RemoteAnimationComponent>(aEntity);
}

// As the real one through the actor bridge: where the actor is and its actions; no graph variables or direction.
void AnimationSystem::Serialize(World& aWorld, ReferencesMoveRequest& aMovementSnapshot, LocalComponent& localComponent,
                                LocalAnimationComponent& animationComponent, FormIdComponent& formIdComponent)
{
    const auto& cActors = aWorld.ctx().at<IActorBridge&>();
    if (!cActors.ActorExists(formIdComponent.Id))
        return;
    auto& update = aMovementSnapshot.Updates[localComponent.Id];
    auto& movement = update.UpdatedMovement;
    if (const uint32_t cCell = cActors.GetActorParentCellFormId(formIdComponent.Id))
        movement.CellId = ToSession(cCell).value_or(GameId{});
    if (const uint32_t cWorldSpace = cActors.GetActorWorldSpaceFormId(formIdComponent.Id))
        movement.WorldSpaceId = ToSession(cWorldSpace).value_or(GameId{});
    float x = 0.f, y = 0.f, z = 0.f;
    cActors.GetActorPosition(formIdComponent.Id, x, y, z);
    movement.Position = {x, y, z};
    cActors.GetActorRotation(formIdComponent.Id, x, y, z);
    movement.Rotation.x = x;
    movement.Rotation.y = z;
    const bool cStreamed = aWorld.GetGraphStreams().Streaming(formIdComponent.Id);
    if (const auto it = aWorld.ctx().at<FakeEngine&>().Actors.find(formIdComponent.Id); it != aWorld.ctx().at<FakeEngine&>().Actors.end() && !cStreamed)
        movement.Variables = it->second.GraphVariables;
    for (auto& entry : animationComponent.Actions)
        update.ActionEvents.push_back(entry);
    if (auto latestAction = animationComponent.GetLatestAction())
        localComponent.CurrentAction = std::move(*latestAction);
    animationComponent.Actions.clear();
}

void AnimationSystem::TargetToNetwork(ActionEvent& aAction) noexcept
{
    const auto cSessionId = Utils::GetSessionIdByFormId(aAction.TargetId);
    aAction.TargetActor = cSessionId ? *cSessionId + 1 : 0;
    if (cSessionId)
        aAction.TargetId = 0;
}

// CombatService's engine upkeep (Actor.cpp): no remote-player anger or hang watch in the fake.
void UpdateRemotePlayerAnger() noexcept
{
}

void RefreshHangLockWatch() noexcept
{
}

void FaceGenSystem::Setup(World& aWorld, entt::entity aEntity, const Tints& acTints) noexcept
{
    if (!acTints.Entries.empty())
        aWorld.emplace_or_replace<FaceGenComponent>(aEntity).FaceTints = acTints;
}
