#pragma once

#include "FakeGraph.h"
#include <span>
#include <Followers/State.h>

// One game's engine in tier 2 (docs/design/testing.md): the player's controls (ControlMap's bitmask), a script VM
// running Game.DisablePlayerControls / EnablePlayerControls through what PartyNatives' glue does around them, the
// world's references (locks, containers' items) with the change records the engine keeps, and the actors (the
// player, placed and placeatme NPCs, copies: where, health, death, bleedout, enabled).
#include "StillBridges.h"
#include <Services/PartyMarkers.h>
#include <Services/Host/PartyThreat.h>
#include <Games/Skyrim/CopyPackage.h>
#include "../FakeNativeVm.h"
#include <Games/Animation/ReplayStep.h>
#include <Games/Animation/ReplayState.h>
#include <Games/Animation/HeldClipLoads.h>
#include "../FakeFlight.h"

#include <Bridges/IJoinBridge.h>
#include <Bridges/IBodyBridge.h>
#include <Bridges/IBodyReads.h>
#include <Games/KnockReplay.h>
#include <Games/ItemMenuRebuild.h>
#include <Physics/RagdollPending.h>
#include <Physics/RagdollRest.h>

#include <Games/QuestChanges.h>
#include <Games/HitVisualsRules.h>
#include <MainLoopTasks.h>
#include <Games/Skyrim/HitRules.h>
#include <Games/Skyrim/DetectionRules.h>
#include <Messages/CellStateRequest.h>
#include <Messages/WorldState.h>
#include <ItemIds.h>
#include <Misc/StoryEventRules.h>
#include <Papyrus/OwnEvents.h>
#include <Papyrus/PartyRules.h>
#include <Structs/Recipe.h>
#include <Services/Host/HostGame.h>
#include <Structs/ScriptValue.h>
#include <Structs/SystemQuestState.h>

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

struct World;

// TESPackage as CreatePackage (29496) leaves it: procedure type -1 until 29582 fills it.
struct FakePackage
{
    uint32_t packFlags = 0;
    int32_t Procedure = -1;
};

// A lock a queued clip load names (63969's +0x58): Skyrim takes it before the load's callback (63997), and one in freed
// memory spins that job forever. Live while it exists; the fake's loads check it.
struct FakeLoadLock
{
    FakeLoadLock();
    ~FakeLoadLock();
    FakeLoadLock(const FakeLoadLock&) = delete;
    FakeLoadLock& operator=(const FakeLoadLock&) = delete;
    void lock() noexcept {}
    void unlock() noexcept {}
    [[nodiscard]] static bool Live(uint64_t aId) noexcept;
    uint64_t Id;
};

struct FakeEngine : StillBridges, IJoinBridge, IBodyBridge, IBodyReads
{
    PartyThreat::State PartyThreats;
    // Skyrim keeps the first candidate selected while other known targets attack it.
    std::map<uint32_t, std::vector<PartyThreat::Candidate>> CombatCandidates;
    std::map<uint32_t, uint32_t> CombatProposals; // Periodic native recalculation (47191/47195).
    std::map<uint64_t, PartyThreat::Perception> CombatPerception;
    // Pairs (State::Key) not yet in the NPC's combat group, its reaction calling them no enemy: admitted (and erased)
    // once it is (CombatGroup::AddTarget 44704 needs CanAttackActor 37532; combat-targets.md).
    std::set<uint64_t> CombatFriendly;
    uint32_t CombatSelectionsPerFrame{1};
    void RunNativeCombat() noexcept;
    static constexpr uint32_t kAllControls = 0xFFFFFFFF;

    uint32_t Controls = kAllControls; // ControlMap::enabledControls
    float ShakeSeconds = 0.f;         // the camera's shake left (Game.ShakeCamera), a frame a 60th of a second
    // A script's calls on a body (cutscenes.md step 2): idles played, graph events sent (Debug.SendAnimationEvent), the
    // player AI-driven; its moves are travels (ObjectReference.MoveTo, MoveActorTo).
    std::map<uint32_t, std::vector<uint32_t>> Idles;
    std::map<uint32_t, std::vector<std::string>> GraphEvents;
    // The events each furniture's own graph got from the actor linked to it (63372; the sawmill's from its worker),
    // and the state its graph is in: FurnitureTransitions {state, event} -> state, from "Idle".
    std::map<uint32_t, std::vector<std::string>> FurnitureGraphEvents;
    std::map<uint32_t, std::string> FurnitureStates;
    std::map<std::pair<std::string, std::string>, std::string> FurnitureTransitions;
    // An actor's graph entering this state sends this event out (its clip's annotation, 63588), to its linked furniture.
    std::map<std::string, std::string> StateOutputs;
    // Furniture whose 3D (and graph) is not loaded here: nothing links to it until LoadsFurniture.
    std::set<uint32_t> FurnitureUnloaded;
    void LoadsFurniture(uint32_t aFurniture);
    void FurnitureReceives(uint32_t aFurniture, const std::string& acEvent);
    std::map<uint32_t, std::map<std::string, bool>> GraphBools; // what a graph event sets (Skyrim: BleedOutStart's IsBleedingOut)
    [[nodiscard]] bool GraphBool(uint32_t aActor, const std::string& acName) const
    {
        const auto it = GraphBools.find(aActor);
        return it != GraphBools.end() && it->second.contains(acName) && it->second.at(acName);
    }
    bool AiDriven = false;
    uint32_t ScenePackage{};
    PartyRules::ControlHolds Holds;   // PartyNatives' holds on this game
    bool InSession = false;
    uint32_t Difficulty = 3;
    // The player picks a difficulty in the settings menu (our hook on PlayerCharacter::SetDifficulty raises it).
    void ChoosesDifficulty(uint32_t aDifficulty);
    // The player's race switches (Actor::SwitchRace: its base's race and the race its save data holds), reported as
    // TESSwitchRaceCompleteEvent while a session's engine runs (PlayerEvents).
    void PlayerSwitchesRace(uint32_t aRace);
    // The player opens the race menu (showracemenu), makes aChanges in it and closes it (MenuEvent).
    void PlayerUsesRaceMenu(const std::function<void()>& acChanges);
    // RaceSexMenu::ChangeName (52415): the base's name, FullName marked changed.
    void PlayerNames(const std::string& acName);
    // BGSRefAlias::InitializeRefAlias (24011) or ClearRefAlias (24012) on an actor; empty: back to its base's name.
    void AliasNames(uint32_t aFormId, const std::string& acName);
    float GreetDistance = 150.f;
    // The join flow's engine (IJoinBridge). A save carries this game's player as a record and the co-save; loading one
    // makes the player the save's (the host's) until JoinService applies the joining player's own.
    FakeNativeVm NativeVm;
    void QuitToDesktop() { NativeVm.ReleaseTypes(); }
    bool AtMainMenu = false;
    bool CreationAvailable = true;
    Creation CreationPhase = Creation::kIdle;
    unsigned CreationRequests = 0;
    bool BeginCharacterCreation() noexcept override
    {
        if (!CreationAvailable || CreationPhase != Creation::kIdle)
            return false;
        ++CreationRequests;
        CreationPhase = Creation::kStarting;
        return true;
    }
    void CancelCharacterCreation() noexcept override {}
    Creation PollCharacterCreation() noexcept override
    {
        const auto phase = CreationPhase;
        if (phase == Creation::kComplete || phase == Creation::kFailed)
            CreationPhase = Creation::kIdle;
        return phase;
    }
    PlayerRecord Player;             // this game's player, as ReadPlayer reads it (but its items and place: the actor's)
    bool RefusesSaves = false;       // Save_Impl's own refusals (combat, a menu)
    std::vector<std::string> Loaded; // the saves this game loaded, by name
    bool IsAtMainMenu() const noexcept override { return AtMainMenu; }
    void ReturnToMainMenu() noexcept override { AtMainMenu = true; }
    std::string PlayerId() noexcept override;
    // As JoinBridge: a save or a load is made on the next main loop frame (RunMainLoop), never inside our update.
    std::optional<SaveFiles> SaveNow(const std::string& acName) noexcept override;
    SaveFiles MakeSave();
    bool WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept override;
    // The engine's main loop between frames, before our update (HookMainLoop's MainLoopTasks).
    void RunMainLoop() noexcept;
    std::optional<SaveFiles> ReadSave(const std::string& acName) noexcept override;
    // As PreviousGameplaySave: the newest save on disk of CharacterId, not a SkyrimCoop scratch save.
    void SetCharacter(uint32_t aCharacterId) noexcept override { CharacterId = aCharacterId; }
    // The engine's currentCharacterID: a load sets it from the save's file name (SaveNames::CharacterId, 0 for
    // SkyrimCoopWipe), the engine's own saves are named with it.
    uint32_t CharacterId = 0;
    std::map<std::string, SaveFiles> SavedFiles;
    // As JoinBridge's worker thread, but in step: the work runs at once and what it returns right after, or with
    // DefersWork both wait for the next FinishWork (JoinService's update), as work still running would.
    bool DefersWork = false;
    std::vector<std::function<std::function<void()>()>> QueuedWork;
    void OffThread(std::function<std::function<void()>()> aWork) noexcept override;
    void FinishWork() noexcept override;
    // The save sync store's files, the newest first.
    std::vector<std::pair<std::string, std::string>> HeldSaveFiles;
    std::vector<std::string> HeldSaves() noexcept override;
    std::optional<std::string> ReadHeldSave(const std::string& acId) noexcept override;
    bool WriteHeldSave(const std::string& acId, const std::string& acData) noexcept override;
    void DropHeldSave(const std::string& acId) noexcept override;
    PlayerRecord ReadPlayer() const noexcept override;
    void ApplyPlayer(const PlayerRecord& acRecord) noexcept override;
    PlayerKnowledge Knowledge; // native ingredient/enchantment forms, separate from the actor's state
    PlayerKnowledge ReadKnowledge() const noexcept override { return Knowledge; }
    void ApplyKnowledge(const PlayerKnowledge& acKnowledge) noexcept override
    {
        if (acKnowledge.Present)
            Knowledge = acKnowledge;
    }
    bool PlacementClear = true;
    uint32_t PlacementChecks = 0;
    bool PlaceBesideHost() noexcept override;

    // This game's player saves (a menu, quick, auto or console save) through Save_Impl's hook (SaveGameHooks.cpp): made
    // unless SaveService refuses it. The saves made, by name.
    bool PlayerSaves(const std::string& acName);
    // A quickload, menu or console load through Load_Impl's policy.
    bool PlayerLoads(const std::string& acName);
    // The engine's own autosave (a load door, fast travel, waiting), through the same hook.
    bool Autosaves(const std::string& acName);
    // A quicksave or autosave with its file: made now, written by the save thread after Save_Impl returned (35794),
    // here on the next main loop frame. SaveWrittenEvent follows every written file, a menu save's at once.
    bool Quicksaves(const std::string& acName);
    std::vector<std::pair<std::string, SaveFiles>> SaveThread; // made, not yet written
    std::string SavePadding;                                   // more bytes in each save's game file (a real one has megabytes)
    std::vector<std::string> Saves;
    std::optional<std::string> PendingSave;            // asked for, made by the next main loop frame
    std::optional<std::optional<SaveFiles>> MadeSave;  // made (or refused), for the next SaveNow
    std::optional<std::pair<std::string, SaveFiles>> PendingLoad;

    // A script's call on this game, run now in a stack whose cause is aCause (GuestScripts': 0 for no player's action).
    void RunScript(const LocalCall& acCall, uint32_t aCause = 0, bool aStory = false, bool aBody = false);
    uint32_t Perform(const LocalCall& acCall, PartyRules::Call aKind, uint32_t aSelf, uint32_t aCause);
    // What actors show (VisualEffect, EffectShader, Actor.PlaySubGraphAnimation: NativeReach's kShow): by actor, the
    // calls run on it here.
    std::map<uint32_t, std::vector<std::string>> Shows;
    // The messages this game's screen showed (Debug.Notification, Debug.MessageBox, Message.Show): text, or the form.
    std::vector<std::string> Messages;
    // Message.Show's boxes open here by the stack waiting on them (latent), the answers they got, and the latent party
    // calls: this game's originals and other games' copies (PartyNatives).
    std::map<uint32_t, std::string> Boxes;
    std::map<uint32_t, int32_t> Answers;
    std::set<uint32_t> LatentOriginals;
    PartyRules::LatentCopies Copies;
    // What the last question a script asked here answered (ObjectReference.GetDistance).
    std::optional<LocalValue> LastAnswer;
    // What the last native run here returned (PlaceAtMe's reference); and a call sent to the host's game waiting on its
    // answer (PlayerNatives' Wait): its stack, then what the stack got (PlayerNatives::Answer).
    LocalValue LastReturn;
    uint32_t LastWaiting = 0;
    std::set<uint32_t> Waiting; // stacks waiting on another game (PlayerNatives' s_waiting)
    std::map<uint32_t, LocalValue> ScriptReturns;
    [[nodiscard]] LocalValue Answer(const LocalCall& acCall, bool aAsksParty) const;
    // This game's player answers the box in aStack: its stack returns (ReturnFromLatent's hook: an original's result
    // goes to its copies, CallEndedEvent).
    void AnswerBox(uint32_t aStack, int32_t aButton);
    // A call the VM runs on its next frame, its stack tagged aCause (GlobalCall::QueueCall): a quest's function
    // (QuestFunctions) or a native.
    void Queue(LocalCall aCall, uint32_t aCause, std::function<void(LocalValue)> aOnReturn = {});
    void RunQueued();
    // A script's call into a quest's function, its self (QuestFunctions, by the quest and the function's name), as
    // GuestScripts' call hook takes it (docs/design/system-quests.md): one from this game's player's own script
    // (aFromBody: its disease's effect) makes the quest a system quest; a call into one runs on the instance of the
    // player behind it (PartyRules::RouteOfSystemCall), its natives that instance's (InSystemQuest).
    using QuestFunction = std::function<void(FakeEngine&, const LocalCall&, uint32_t aCause)>;
    std::map<std::pair<uint32_t, std::string>, QuestFunction> QuestFunctions;
    void CallsQuest(const LocalCall& acCall, uint32_t aCause = 0, bool aStory = false, bool aFromBody = false);
    // This game's system quests (SystemQuests' seam); the globals each quest's scripts hold; each instance's script
    // variables and registrations (its timers fire through FiresQuestTimer).
    mutable OwnEvents::SystemQuestList SystemQuestList; // its reads lock
    std::map<uint32_t, std::vector<uint32_t>> QuestGlobals;
    std::map<uint32_t, std::map<std::string, LocalValue>> QuestVariables;
    std::map<uint32_t, std::map<std::string, SystemQuestState::Timer>> QuestTimers;
    [[nodiscard]] std::string ReadSystemQuests() const;
    void ApplySystemQuests(std::string_view aState);
    // A registration of a system quest's instance fires (its event, as the VM sends it: this game's own).
    void FiresQuestTimer(uint32_t aQuest, const std::string& acFunction);
    // One frame: the shake wears off, knocked-down ragdolls come to rest, heads are built, the VM runs what is queued;
    // copies are drawn, then their graphs update (one lying in its ragdoll put on its root body, as the engine keeps any actor).
    void Frame();

    // A world reference (every game has the same load order: a form id is the same everywhere).
    struct Reference
    {
        uint32_t Cell = 0;
        bool Locked = false;
        uint8_t LockLevel = 0;
        std::map<uint32_t, int32_t> Items; // a container's count of each item
        std::vector<Inventory::Entry> Stacks; // those of them with extra data (an enchantment, tempering...)
        bool Door = false;
        bool Open = false; // a door's or chest's (BGSOpenCloseForm's open state)
        bool Chest = false; // a container with a lid (an Open/Close sequence)
        bool Has3D = true; // none while disabled; an enable queues its load (Finishes3D)
        float Lid = 0.f; // a chest's lid: 0 closed, 1 open
        bool Unposed = false; // its open state changed with no 3D: its next 3D comes up part open (measured)
        glm::vec3 Position{}; // where it stands (a marker MoveTo goes to)
        uint32_t RecordFlags{};
        uint32_t Base = 0; // a created one's (a script's PlaceAtMe)
        bool Disabled = false; // a script's Disable (kEnabled's row)
        uint32_t Owner = 0; // its ExtraOwnership (an NPC or a faction); none: its cell's (CellOwners)
        bool Plant = false;     // a flora or tree reference: an activation picks it (TESProduceForm::Harvest 14864)
        bool Harvested = false; // its form flag 0x2000
        std::set<uint32_t> Rolled; // items a leveled roll put in (their changes entry a roll's: ExtraLeveledItem lists)
        uint32_t LinkedDoor = 0; // a load door with no lock of its own: it uses this door's (GetLock 20223)
        bool Persistent = false; // in memory with its cell unloaded (a load door's other side)
        bool operator==(const Reference&) const = default;
    };
    std::map<uint32_t, Reference> References;
    // The reference holding its lock: a load door's linked door when it has none (AddLockChange 19512 records there).
    [[nodiscard]] uint32_t LockHolderOf(uint32_t aReference) const;
    // A container's count of an item as TESObjectREFR::GetContainerCount reads it (ContainerCount): Items holds its
    // changes entry's count, kept at 0 for a roll's item taken (16059 frees it only on an actor).
    [[nodiscard]] int32_t ContainerCountOf(uint32_t aContainer, uint32_t aItem) const;
    // The container bases: a created one is a container reference, not a loose item.
    std::set<uint32_t> ContainerBases;
    // A container reference made here (ModManager::CreateReference): it holds nothing, the engine rolls no leveled
    // entries for it until opened, and a guest's never rolls (the host's contents fill it). Its id: the next temporary.
    uint32_t PlaceContainer(uint32_t aCell, uint32_t aBase);
    // The host's script places a container (PlaceAtMe of a leveled chest) that rolled aRolled: made and recorded.
    uint32_t ScriptPlacesContainer(uint32_t aCell, uint32_t aBase, const std::map<uint32_t, int32_t>& acRolled);
    // A script's PlaceAtMe (56203: CreateReferenceAtLocation 13723) of aBase at aAt (an actor or a reference): made and
    // recorded as the host's creations are (kCreate).
    uint32_t ScriptPlaces(uint32_t aAt, uint32_t aBase);
    // A created container's contents (HostGame::ReadContents); none for no such container.
    std::optional<ObjectChange> ContentsOf(uint32_t aContainer) const;
    // A container holds exactly the kContents change's items (ApplyContainerContents).
    void ApplyContents(Reference& aContainer, const ObjectChange& acContents);
    // Each cell's owner (its ExtraOwnership), by cell: every game has the cells.
    std::map<uint32_t, uint32_t> CellOwners;
    // A script makes a cell another's (Cell.SetFactionOwner, a house bought): the engine records the cell's
    // ownership change, which the host's game sends as a row if FormChangeRows says so.
    void ScriptSetsCellOwner(uint32_t aCell, uint32_t aOwner);
    // A script makes an actor another's (SetFactionOwner, a horse bought): the engine records the reference's
    // ownership change (CHANGE_REFR_EXTRA_OWNERSHIP), sent as a row if FormChangeRows says so.
    void ScriptSetsActorOwner(uint32_t aActor, uint32_t aOwner);
    std::set<uint32_t> PendingPlacements; // controlled by shared-rest reconciliation tests
    std::set<uint32_t> Attached;       // the cells loaded here
    std::set<uint32_t> InMemory;       // the cells loaded here once: their references stay in memory
    std::set<uint32_t> Changed;        // the references this game's change records hold
    std::vector<ObjectChange> Recorded; // the host's: its world's changes since HostGame::TakeObjectChanges
    bool IsGuest = false;              // a guest's engine raises its player's changes as events (GuestWorldState)
    bool DecidesWorld = true;          // GuestEngine::SetDecidesWorld
    uint32_t HoldSession = 0;          // GuestNpcs::Session's, set with DecidesWorld: 0 while not a guest
    // A guest's own NPC: no AI (HookUpdateAI) and no 3D (HookLoad3D) until the host spawns its copy (GuestNpcs::Held).
    [[nodiscard]] bool HeldOnGuest(uint32_t aFormId) const noexcept;
    // The loader threads busy: no load job finishes (QueuedReference::Finish 13022) while set.
    bool LoadsHeld = false;
    // Actor::ApplyHeld3D: held with 3D, reloaded (refused); not held after a refused load, queued (a job still out
    // only takes the priority, QueueReference 13057).
    void ApplyHeld3D(uint32_t aFormId);

    void LoadCell(uint32_t aCell);
    void UnloadCell(uint32_t aCell);
    // This game's player picks a lock or takes from a container, as the engine's hooks report it: the host's game
    // records it, a guest's raises LockChangeEvent / ContainerChangeEvent.
    void PlayerSetsLock(uint32_t aReference, bool aLocked, uint8_t aLevel);
    void PlayerSetsItems(uint32_t aContainer, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks = {});
    // The host's game takes a change (a guest's proposal accepted, HostGame::SetLock / SetItems): made and recorded.
    void SetLock(uint32_t aReference, bool aLocked, uint8_t aLevel);
    void SetItems(uint32_t aContainer, uint32_t aItem, int32_t aCount, std::vector<Inventory::Entry> aStacks = {});
    // The host's game opens or closes a door (its player, a replayed activation): made and recorded.
    void SetOpen(uint32_t aDoor, bool aOpen);
    // The host's script disables or enables a reference (Disable 19801 / Enable 19800): made and recorded.
    void ScriptSetsEnabled(uint32_t aReference, bool aEnabled);
    // Each cell's location (BGSLocation), by cell: the player moved into a cell of another location raises CLOC.
    std::map<uint32_t, uint32_t> Locations;
    // The story event types this game registers, by index (BGSStoryEventManager::registeredEvents).
    struct RegisteredStory
    {
        uint32_t Id{};
        std::vector<StoryEventRules::MemberType> Members;
    };
    using enum StoryEventRules::MemberType;
    std::vector<RegisteredStory> StoryTypes{
        {StoryEvent::TypeOf("CLOC"), {kReference, kLocation, kLocation}},
        {StoryEvent::TypeOf("SKIL"), {kInteger}},
        {StoryEvent::TypeOf("KILL"), {kReference, kReference, kLocation}},
    };
    // This game raises a story event (AddEvent_Impl), its members as BGSStoryEvent holds them (a reference or a form by
    // its form id here): while a guest, it goes to the host (StoryEvents' hook, off while the game decides its world).
    void RaisesStoryEvent(uint32_t aType, std::vector<uint64_t> aMembers);
    // Player-made potions and poisons, by form id (BGSCreatedObjectManager's): one per recipe, ids this game's own.
    std::map<uint32_t, Recipe> Brewed;
    // This game's potion for a recipe, made if new (AddPotion: the engine's own when it has one).
    uint32_t Brew(const Recipe& acRecipe);
    // This game's player brews aCount at an alchemy lab (the menu's add to its inventory): the potion's form id.
    uint32_t PlayerBrews(const Recipe& acRecipe, int32_t aCount);
    // The story events the host's game queued for a guest's character (HostGame::RaiseStoryEvent).
    std::vector<StoryEvent> StoriesRaised;
    // The global variables (TESGlobal), by form id: the host's read (HostGame::ReadGlobals), a guest's set to them.
    std::map<uint32_t, float> Globals;
    std::set<uint32_t> GlobalWrites;
    void SetGlobal(uint32_t aId, float aValue) { Globals[aId] = aValue; GlobalWrites.insert(aId); }
    std::array<uint32_t, 2> BeastPerkGlobals{}; // werewolf, vampire: default objects 320/321
    void ModifyBeastPerks(bool aVampire, int32_t aDelta);
    std::map<uint32_t, QuestState> Quests;
    // Quests whose dialogue lines this game has read (TESQuest 25086, at the engine's start of the quest).
    std::set<uint32_t> DialogueLoaded;
    // The host's game starts a quest as the engine does: running, its lines read, the change reported.
    void StartsQuest(uint32_t aQuest)
    {
        Quests[aQuest].Id = GameId{0, aQuest};
        Quests[aQuest].Flags |= 1;
        DialogueLoaded.insert(aQuest);
        QuestChanged(aQuest);
    }
    QuestChanges QuestWrites;
    // The quest events and alias-write hooks report the quest after its state was written.
    void QuestChanged(uint32_t aId) { QuestWrites.Quest(aId); }
    void ClearReferenceAlias(uint32_t aQuest, uint32_t aAlias);
    void ClearLocationAlias(uint32_t aQuest, uint32_t aAlias);
    // Loose items (docs/design/dropped-items.md): references made at run time holding an item (its count and extra
    // data), each game's own 0xFF ids. The host's go out as kCreate and their deletion; a guest's are its player's
    // drops, or twins of the host's (GuestEngine's m_created, the seams').
    struct LooseItem
    {
        uint32_t Cell = 0;
        Inventory::Entry Item;
        // A quest's alias holds it (a quest object, the host's game's alone): picked up, the engine's quest item.
        bool QuestObject = false;
        // Its owner form (ExtraOwnership, an NPC base): a guest's player picking it up steals it (CrimeBridge).
        uint32_t Owner = 0;
    };
    std::map<uint32_t, LooseItem> Loose;
    // A guest's loose items the host decides (twins, its own drops not answered yet), as GuestEngine's pickup gate
    // reads them; and GuestEngine::PickUp's own pickup, past the gate.
    std::set<uint32_t> HostDecides;
    bool PickUpGranted = false;
    // Forms the host's game lacks: a drop of one cannot be made there (HostGame::CreateItem's empty id).
    std::set<uint32_t> MissingForms;
    // A loose item made here: its id (the engine's next temporary).
    uint32_t PlaceLoose(uint32_t aCell, const Inventory::Entry& acItem);
    // The player drops some of what it carries (aItem: its base, extra data and count) where it stands, as
    // PlayerCharacter::DropObject: the inventory's removal raised as a drop; the host's made reference recorded, a
    // guest's raised (ItemDroppedEvent). Returns the loose item's id here.
    uint32_t DropsInWorld(const Inventory::Entry& acItem);
    // The player picks a loose item up (PlayerCharacter::PickUpObject): the host's game takes it (its deletion
    // recorded, the item raised as an inventory change); a guest's asks the host first for one the host decides
    // (ItemPickUpEvent).
    void PicksUp(uint32_t aObject);
    // The player sneaks and takes one aItem from aVictim's pockets (ContainerMenu 51141 in pickpocket mode). Caught
    // (AttemptPickpocket 40654): PickpocketAlarm (37423) with no item and count 0, nothing moves. Taken: the victim's
    // RemoveItem (kSteal) to the player, a guest's proposed to the host instead (ContainerTransfers).
    void Pickpockets(uint32_t aVictim, uint32_t aItem, bool aCaught);
    // The host's game makes a loose item where the placement says (HostGame::CreateItem): none while that cell is not
    // loaded here, an empty id for a missing form. And takes one out of its world (HostGame::TakeItem).
    std::optional<GameId> CreateLoose(const Inventory::Entry& acItem, uint32_t aCell);
    std::optional<Inventory::Entry> TakeLoose(const GameId& acObject);
    // A loose quest object the host's player picks up (HostGame::TakeQuestObject); false for any other.
    bool TakeQuestObject(const GameId& acObject);
    // The actor's entries of that item, extra data and all.
    [[nodiscard]] std::vector<Inventory::Entry> EntriesOf(uint32_t aActor, uint32_t aItem) const;

    // The rows of a reference's state (ReferenceStates' encoding: a lock's level, kLocked above it; items' counts).
    [[nodiscard]] std::vector<ObjectChange> RowsOf(uint32_t aReference) const;
    // Another game's change made here (GuestEngine::ApplyObjectChange); false: no such reference.
    bool Apply(const ObjectChange& acChange, bool aLoad);
    // The host's state of a cell (HostGame::ReadCellState): the rows of the references it changed or aChanged names.
    [[nodiscard]] std::optional<std::vector<ObjectChange>> CellState(const GameId& acCell, const std::vector<ChangedReference>& acChanged) const;
    // A guest asking for a loaded cell's state (GuestEngine::ReadCellChanges).
    [[nodiscard]] std::optional<CellStateRequest> CellChanges(uint32_t aCell) const;

    // Physics bodies (docs/design/physics-sync.md), each game's own by key, in its references' cells. The host's fly
    // (FakeFlight) in its network's time and fall asleep at rest, as Havok's island events report them; a guest's are
    // held keyframed and moved by the host's frames (GuestEngine::DriveBody / RestBody). One pose stands for body and 3D:
    // in Skyrim a keyframed body leaves its node behind, so GuestEngine writes the node (FollowBody, the scenario's check),
    // and so does a host body far from the host's player, so PhysicsBodies writes it too (guest-cast-moves-host).
    struct Body
    {
        bool NeedsKnock{};
        bool RestBusy{};
        size_t RestAttempts{};
        FakeFlight Flight;
        bool Awake = false;
        bool Held = false;    // keyframed by the host's frames (a guest's; a reference's freed at its rest)
        size_t Drives = 0;    // frames of the host's followed
        // Its node (a reference's 3D): the engine writes it from the body each step, except while it leaves it behind
        // (docs/re/systems/physics.md). A node write on a body the engine already followed re-places the body, which
        // loses its contacts (the bucket that sank into a head): counted in Replaced.
        bool NodeLeftBehind = false;
        glm::vec3 NodePosition{};
        glm::quat NodeRotation{1.f, 0.f, 0.f, 0.f};
        size_t NodeWrites = 0;
        size_t SceneWritesDuringJobs = 0; // manual 3D writes outside the between-frames main-loop drain
        size_t Replaced = 0;
        size_t Rests = 0;     // rests of the host's placed
        // Havok's contact points for it: none once an owner's rest is set dynamic and put to sleep at once
        // (physics.md, "A rest needs its contacts"); a capsule's first push then throws it up (Thrown).
        bool Contacts = true;
        std::optional<BodyPose> Rested; // the owner's rest it was last put at
        std::optional<BodyPose> Placing; // an owner's rest being put here, not done (BodyBridge's Rested, not Done)
        uint16_t GlideFrames = 0;        // frames it has glided onto Placing (BodyPrediction::GlidesToRest)
        bool RestStep = false;  // awake at or onto its rest this frame: read awake, not stepped (BodyBridge::Rest)
        size_t Thrown = 0;
        size_t MoveRows = 0;  // world state move rows written into its 3D (GuestEngine::PlaceAtHostRest)
        uint64_t Tick = 0;    // the host's: where its flight is
        uint32_t RagdollOf = 0; // an actor's ragdoll body: its animation drives it while the actor lives
        std::optional<glm::vec3> RestAt; // a copy's ragdoll body: its owner's rest it is being taken to
        glm::quat RestTurn{1.f, 0.f, 0.f, 0.f};
        // A copy's ragdoll body awake that nothing steered: its joints settle it off where it was put, until Havok
        // sleeps it (duo, death-spectate-host's RagdollTrace, 2026-11-12: a few degrees off, about half a second).
        uint16_t SettleFrames = 0;
        bool Caught = false;    // this game's world holds it (a head under a table's edge): its get-up cannot move it
        bool OutOfWorld = false; // its actor stood up: not in the physics world, not read, until its next knock
        uint64_t StillFor = 0;  // woken without a push: awake, unmoved, this long (Wake)
        uint64_t StillUntil = 0;
        // An animated body shares its island (Orgnar's, 78910): Havok never sleeps it, stopped or not (physics.md,
        // "Woken still").
        bool KeptAwake = false;
    };
    // How long an actor takes to get up (knock states 5 and 6), and how fast its get-up moves its ragdoll's bodies.
    static constexpr uint32_t kRisingFrames = 60;
    static constexpr float kRisingSpeed = 120.f;
    // Where its animation has that ragdoll body once it stands.
    [[nodiscard]] glm::vec3 StandingPose(uint32_t aFormId, uint8_t aIndex) const;
    // Lying no longer: with a ragdoll its bodies stay simulated while it gets up (Rising).
    void StandUp(uint32_t aFormId);
    // Its knock state as ActorState's: 1 lying, 6 getting up, 0 standing.
    [[nodiscard]] uint8_t KnockStateOf(uint32_t aFormId) const;
    std::map<BodyKey, Body> Bodies;
    // A ragdolled actor's heading as this engine turns it with its root body: where the body's y axis points, about z.
    static float HeadingOf(const glm::quat& acRotation) noexcept;
    // The host's player knocks it: it wakes and flies off at that velocity.
    void Knock(const BodyKey& acKey, const glm::vec3& acVelocity, const glm::vec3& acSpin = {});
    // This game's player's weapon hits it (38628's applyPointImpulse): its own physics knocks it and the hit is
    // recorded (PlayerHits); a keyframed one (Held) gets a zero impulse, nothing. Every body has a mass of 1.
    void Swing(const BodyKey& acKey, const glm::vec3& acImpulse);
    // This game itself puts it there (a script's SetPosition, the console's setpos): asleep at that spot, its move
    // recorded (the host's: Recorded's row and Placed).
    void PlaceByGame(const BodyKey& acKey, const glm::vec3& acPosition);
    std::vector<std::pair<GameId, glm::vec3>> Placed;
    // An actor's animated body in its island wakes it, unmoved (Orgnar at his bar, 78910): awake for aMs, then asleep.
    void Wake(const BodyKey& acKey, uint64_t aMs);
    // This game's physics steps up to aTick (HostGame::TakeBodies, a guest's GuestEngine::TakeOwnBodies): the awake
    // bodies, and those that fell asleep; acWhich: only those (a guest's player's ragdoll).
    HostBodies TakeBodies(uint64_t aTick, const std::function<bool(const BodyKey&)>& acWhich = {});
    // IBodyReads: this game's physics as PhysicsBodies reads it (the host's, in its network's time: PhysicsTick).
    uint64_t PhysicsTick = 0;
    std::vector<uint32_t> TakeMovedReferences() noexcept override;
    uint32_t PlayerInRagdoll() noexcept override;
    uint32_t PlayerHolds() noexcept override;
    std::optional<IBodyReads::Reference> ReadReference(uint32_t aFormId, bool aBodies = true) noexcept override;
    void WriteNode(uint32_t aFormId, uint8_t aIndex) noexcept override;
    // The reference a body belongs to (an actor's for its ragdoll's).
    [[nodiscard]] static uint32_t ReferenceOf(const BodyKey& acKey) noexcept;
    // This game's player's hand (a guest's: GuestEngine::TakePushes, ReadHeld): what its character pushed since the
    // last read (every body a mass of 1), and what it holds. Another game's hand on this game's bodies comes through
    // IBodyBridge (a push, a held body played keyframed, so not awake, then let go).
    std::vector<BodyPush> PlayerPushes;
    // Its weapon's swings (Actor::CalculateCurrentHitTargetForWeaponSwing 38628): the impulse each gave a body, which
    // its own physics moves too (OwnMoves). Swing() makes one.
    std::vector<BodyPush> PlayerHits;
    std::optional<BodyKey> Grabbed;
    std::set<uint32_t> WatchedCorpses; // copies' corpses this game's player grabbed: read as its own until they rest
    // A guest's own physics moved these (GuestEngine::TakeOwnBodies): its own blast, not the host's.
    std::vector<BodyPose> OwnMoves;
    // IBodyBridge: the bodies other games own, played here as Skyrim's would be (BodyBridge).
    void SetReferences(std::function<uint32_t(const GameId& acId)>) noexcept override {}
    std::optional<BodyPose> ReadPose(const BodyKey& acKey) noexcept override;
    bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0, bool aHeld = false) noexcept override;
    bool KnockDown(uint32_t aCopyFormId) noexcept override;
    // What a body meets here (Physics/BodyGroups.h): played while another game's frames drive it, moving while this
    // game says so.
    std::map<BodyKey, BodyClass> Classes;
    void SetMoving(const BodyKey& acKey, bool aMoving) noexcept override
    {
        // A copy's corpse this game's player took: moving, then played again (BodyBridge::SetMoving).
        if (acKey.Actor)
        {
            if (const auto it = Classes.find(acKey); it != Classes.end())
                it->second = aMoving ? BodyClass::kMoving : BodyClass::kPlayed;
            return;
        }
        if (const auto it = Classes.find(acKey); it != Classes.end() && it->second == BodyClass::kPlayed)
            return;
        // As BodyBridge::SetMoving: its owner's rest being put here wakes it, which is not this game moving it.
        if (const auto cBody = Bodies.find(acKey); aMoving && cBody != Bodies.end())
        {
            if (cBody->second.Placing)
                return;
            cBody->second.Rested.reset();
        }
        if (aMoving) Classes[acKey] = BodyClass::kMoving; else Classes.erase(acKey);
    }
    void Forget(const BodyKey& acKey) noexcept override
    {
        if (!acKey.Actor) // a ragdoll keeps its group (BodyBridge::Forget)
            Classes.erase(acKey);
    }
    void ForgetAll() noexcept override { Classes.clear(); }
    // Body acKey runs into acInto in this game: knocked at that velocity, unless acKey is played and nobody moves
    // acInto (its owner's game never hit that one).
    void Collide(const BodyKey& acKey, const BodyKey& acInto, const glm::vec3& acVelocity)
    {
        const auto cClass = [&](const BodyKey& acBody) { const auto it = Classes.find(acBody); return it != Classes.end() ? it->second : BodyClass::kNone; };
        if (cClass(acKey) == BodyClass::kPlayed && cClass(acInto) != BodyClass::kMoving)
            return;
        Knock(acInto, acVelocity);
    }
    // Havok's simulation islands (61509): a body whose AABB meets another's shares its island from then, and the
    // whole island is awake while any body in it is. Lands: acKey comes down on acOnto, which wakes unmoved.
    std::vector<std::set<BodyKey>> Islands;
    void Lands(const BodyKey& acKey, const BodyKey& acOnto) { Islands.push_back({acKey, acOnto}); }
    // The islands any of acKeys is in while one of that island's bodies is awake: every body in each (not keyframed)
    // and its island's index.
    [[nodiscard]] std::vector<std::pair<BodyPose, uint32_t>> AwakeIslands(const std::vector<BodyKey>& acKeys) const
    {
        std::vector<std::pair<BodyPose, uint32_t>> bodies;
        for (uint32_t i = 0; i < Islands.size(); ++i)
        {
            const auto& cIsland = Islands[i];
            const bool cOwn = std::ranges::any_of(acKeys, [&](const BodyKey& acKey) { return cIsland.contains(acKey); });
            const bool cAwake = std::ranges::any_of(cIsland, [&](const BodyKey& acKey) { return Bodies.contains(acKey) && Bodies.at(acKey).Awake; });
            if (!cOwn || !cAwake)
                continue;
            for (const BodyKey& cKey : cIsland)
                if (Bodies.contains(cKey) && !Bodies.at(cKey).Held)
                    bodies.push_back({{.Key = cKey, .Position = Bodies.at(cKey).Flight.Body.Position, .Rotation = Bodies.at(cKey).Flight.Body.Rotation}, i});
        }
        return bodies;
    }
    void SetCopies(const std::vector<uint32_t>& acCopyFormIds) noexcept override { CopiesPushNothing = acCopyFormIds; }
    std::vector<uint32_t> CopiesPushNothing; // the copies whose capsules this game told the engine push nothing
    // A character's capsule walks into the body in this game (79133): knocked at that velocity, unless it is a
    // copy's the game named (SetCopies).
    void Bump(uint32_t aActorFormId, const BodyKey& acKey, const glm::vec3& acVelocity)
    {
        if (std::ranges::find(CopiesPushNothing, aActorFormId) != CopiesPushNothing.end())
            return;
        // No floor under the push's downward part for one step: it sinks and bounces up.
        Body& body = Bodies.at(acKey);
        const bool cThrown = !std::exchange(body.Contacts, true);
        body.Thrown += cThrown;
        Knock(acKey, cThrown ? acVelocity * 0.75f + glm::vec3(0.f, 0.f, glm::length(acVelocity) * 0.25f) : acVelocity);
    }
    bool Rest(const BodyPose& acPose, uint32_t aCopyFormId = 0) noexcept override;
    // An actor's character controller keeps simulated bodies out of its capsule as it integrates (its proxy's
    // object interactions); a keyframed body passes through, and so does a placed copy's capsule (a warp steps
    // nothing: guest-holds-into-npc, its bucket 2 units off the copy's axis, 19.7 off an owner's). The duo's probe:
    // the capsule's top 128 above the feet.
    static constexpr float kCapsuleRadius = 20.f;
    // Havok's character step lands this short of a stepped copy's goal (measured: 0.04 units).
    static constexpr float kStepShort = 0.04f;
    // A character's body wakes its island when its step sets a new velocity (62456): the bodies this near its axis.
    static constexpr float kIslandReach = kCapsuleRadius + 40.f;
    static constexpr float kCapsuleTop = 128.f;
    // Where a simulated body moving from acFrom toward acTo stops: outside every capsule that pushes.
    [[nodiscard]] glm::vec3 OutsideCapsules(const glm::vec3& acFrom, const glm::vec3& acTo) const noexcept;
    // This game's player grabs it (Activate held: StartGrabObject 40552), which the engine refuses for a keyframed body
    // (another game's frames move it): whether it holds it now.
    bool TryGrab(const BodyKey& acKey) noexcept;
    // This game's player holds a body toward acTo (the grab's spring): it stops outside every capsule that pushes.
    void HoldTo(const BodyKey& acKey, const glm::vec3& acTo) noexcept;
    bool Free(const BodyFrame& acFrame) noexcept override;
    [[nodiscard]] uint32_t ReferenceFormId(const BodyKey& acKey) const noexcept override { return acKey.Actor ? 0 : ReferenceOf(acKey); }
    bool KeepMotion(const BodyKey&) noexcept override { return false; }
    bool Settle(const BodyKey&) noexcept override { return false; }

    // Every game's plugins, in one load order: Skyrim.esm, Update.esm, a light ccBGSSSE001-Fish.esm.
    std::vector<ModEntry> GetLoadedMods() const noexcept override
    {
        return {{0, false, "Skyrim.esm"}, {1, false, "Update.esm"}, {0, true, "ccBGSSSE001-Fish.esm"}};
    }
    // One load order: every form is in every game.
    bool FormExists(uint32_t aFormId) const noexcept override { return aFormId != 0; }

    // ActorValueInfo's health, magicka and stamina; the fake keeps no other values.
    static constexpr uint32_t kHealth = 24;
    static constexpr uint32_t kMagicka = 25;
    static constexpr uint32_t kStamina = 26;
    // Health, magicka and stamina at 100 of 100.
    [[nodiscard]] static ActorValues FullValues();

    // The engine's actors: this game's player (0x14), the NPCs its cells loaded, the copies its services made.
    struct Actor
    {
        uint32_t Base = 0;
        // A leveled actor's original form (ExtraLeveledCreature's, 11810): the leveled character it was placed from,
        // a permanent form; Base is then each game's own temporary NPC.
        uint32_t LeveledBase = 0;
        std::string DisplayName;
        uint32_t Cell = 0;
        uint32_t WorldSpace = 0;
        float Position[3]{};
        float Rotation[3]{};
        bool Dead = false;
        uint64_t GraphHash = 0;
        bool GraphUpdating = true; // KillImpl clears kShouldAnimGraphUpdate; resurrection leaves it stopped.
        bool KillHeld = false;    // a scripted deferred kill (38616): every copy's (UpdateCopyAI), and it outlives the
                                  // copy (no engine path ends one, 38618's callers); held, a kill is only counted (37896)
        bool KillCounted = false; // a kill held back (MiddleHigh +0x2F8 above 1); our release drops it (Actor::HoldKill)
        bool KillQueued = false;  // QueueKill 37316's task waiting (MiddleHigh +0x331): run at the next frame's start
        uint32_t Commander = 0; // a summon's caster (MiddleProcess 0x218); this game's player's: Actor::IsPlayerSummon
        bool Teammate = false;  // a follower of the player (flags1 kPlayerTeammate): off a player's eye (41408)
        bool ProcessMe = true; // kProcessMe: EnableAI(false) (37504, the harness's freeze) clears it, and the graph job skips it (41466)
        bool GraphReady = true; // attached 3D does not guarantee a behavior graph is ready for actions
        bool InAir = false; // its controller jumping or falling, else on the ground
        bool InKillMove = false; // either half of a pair (Actor::IsInKillMove 38615)
        uint32_t PairBlowIn = 0;  // frames until its own pair's blow lands (HitFrame), 0 none
        uint32_t PairVictim = 0;
        uint32_t KillMoveEndIn = 0; // frames until its own clip's KillMoveEnd (KillMoveEndHandler 42871), 0 none
        uint32_t KillActorIn = 0; // frames until its victim half's clip sends KillActor (42838), 0 none
        uint32_t ClashPartner = 0;
        uint64_t ClashUntil = 0;
        uint8_t PairStops = 0;
        bool PairedWithCopy = false;
        uint64_t PoseHeldUntil = 0;
        bool InPairClip = false;    // bIsSynced: on through a pair's clip
        uint32_t State1 = 0;        // ActorState's flags1 (sneaking 1 << 9): an owner's goes with its actions
        uint32_t State2 = 0;        // flags2 but the weapon state (Weapon): wantBlocking 0x100, recoil 0xC00
        uint32_t Killer = 0;        // a kill move's victim: its killer (killerHandle, 38613)
        int32_t Noise = 0;          // actionValue: its own engine sets it (a player's bump, a swing: 37711), detection's
                                    // sound term hears it (37774)
        bool NoiseHeld = false;     // a copy's, its owner's: no countdown ends it (ActorStateRules::CopyNoiseTimer)
        bool RefusesDraw = false;   // its draw or sheathe action refused (37279: weapon state 1 or 4), its graph not ready
        uint32_t ExitIn = 0;        // frames until its exit clip's own event (42835) ends its getting off, 0 none
        bool InPair = false;        // in a pair's instance on this game (32808): a replay with a copy partner makes none
        double MarkWaitMs = 0.0;    // in an instance: until every actor is at its mark, its synchronized clip waits
        uint32_t StopsDropped = 0; // pairedStops its uninterruptible transition still drops
        bool KnockedDown = false;  // a living copy lying in its ragdoll with its owner's (BodyBridge::KnockedDown); an
                                   // owner on its ragdoll before it simulates (knock state 2)
        uint8_t KnockIn = 0;       // frames until its queued knock lands (KnockExplosion on the task queue): drawn first
        uint32_t Rising = 0;       // frames left getting up (knock states 5 and 6): its ragdoll's bodies are still this
                                   // game's physics', driven toward its animation's pose (58291)
        bool Bleeding = false; // essential and down (ActorState's bleedout)
        bool Disabled = false;
        uint32_t Owner = 0;          // its ExtraOwnership (a horse bought: the player faction)
        bool OwnerRecorded = false;  // its change record holds the ownership change
        bool Deleted = false;    // marked deleted (DisableActor's aDelete)
        bool Created = false;    // made by Actor::Create (a copy): deleted with its cell unless persistent
        bool Persistent = false; // kept when its cell unloads (SetActorPersistent); no parent cell outdoors out of it
        bool Collectable = false; // a plugin's non-persistent reference: moved out of the loaded cells, the engine
                                  // frees it (37501, 19113; a guard on the guest, 2026-11-06 and 2026-11-07)
        bool Hidden = false;      // a held copy hidden: its enables refused (HookEnable, HookSetDisabled)
        std::set<uint32_t> Outfit;     // its outfit's items: an enable's inventory reset removes them (19690)
        std::set<uint32_t> ForcedWorn; // worn items whose list holds ExtraCannotWear (an equip with forceEquip: ours)
        uint32_t EnableParent = 0; // its enable parent (XESP): takes the parent's state as the parent loads
        bool NoProcess = false;   // its process freed, its handle still valid (marked deleted, MarkForDelete)
        bool Essential = false;  // goes down at 0 health instead of dying
        bool Copy = false;   // another game owns it (SetActorCopy)
        uint32_t ClaimedIn = 0; // the guest session SetActorCopy(false) made it this game's in (GuestNpcs::Held)
        bool Refused3D = false; // held on a guest: its Load3D refused (HookLoad3D), loaded once it is not
        bool LoadQueued = false; // its load job out (TESQueuedHandles' handle), until QueuedReference::Finish
        bool ModelReady = false; // its job's model loaded, attached at Finish
        bool Stepped = false; // a copy last moved by its engine's step (CopyMove::Step), not placed: its capsule pushes
        float Goal[3]{};      // a stepped copy's owner's place: Havok's step lands kStepShort short of it
        bool InFurniture = false; // sit/sleep state set: a colliding capsule there is pushed off its goal by its furniture
        bool Collides = true;     // its capsule collides (filter bit 0x8000 clear); the engine clears it in furniture
        uint8_t BodiesLayer = 0;  // the layer its own 3D's bodies are on in the world, 0 out of it: the AI puts an
                                  // actor's there (33) as it sits or sleeps (40063), never a copy's (it runs no AI)
        bool GraphsStepPhysics = true; // its graphs step with the physics world; the AI detaches them in furniture (40063)
        uint32_t Movements = 0; // its owner's movement applied (ApplyRemoteMovement stepping or placing it)
        bool Player = false; // a player's character, this game's or a copy (SetActorPlayer)
        bool NotOnStealthMeter = false; // boolFlags kDoNotShowOnStealthMeter: 41408 skips it for the player
        bool Initializing = false; // TESForm::kStillLoading: no 3D yet, FinishLoading ends it
        bool Loaded = true;  // its 3D is loaded (in the process lists)
        uint32_t CriticalStage = 0; // Actor+0xF0 (ACTOR_CRITICAL_STAGE)
        bool BodyHidden = false;     // its third-person root's kHidden (first person, or a refused ash or goo end)
        bool FirstRootHidden = true; // a player's first-person root's kHidden (shown in first person)
        bool HeadBuilt = false;         // its head is built: the frame its 3D is there (TESNPC::UpdateHeadState 24724)
        std::optional<Tints> HeadTints; // painted on its head (FaceGenSystem); a head built again has none (24731)
        uint32_t DyingFrames = 0;  // the player's: dying until its ragdoll comes to rest, then dead
        uint32_t DeadFrames = 0;   // the player's: dying or dead, the reload's countdown (40748)
        WeaponState Weapon = WeaponState::Sheathed;
        // Frames until its own draw end (42824/37169) for a draw 37279 took; dropped on its stream and by a sync point.
        uint32_t WeaponEventIn = 0;
        uint32_t KilledBy = 0;            // the attacker of the hit that killed it (the engine's kill credit)
        uint32_t LocalScene = 0; // a copy's independent native scene, distinct from its owner's answer
        ActorAiFields Ai;                 // package, scene, combat (a copy's set by AiStateService)
        uint32_t LinkedFurniture = 0;     // the furniture whose graphs get its graphs' events (63364), 0 none
        FakePackage DoNothing;            // the do-nothing package a copy holds with no owner's to show
        int32_t ProcedureType = 0;        // the running package's: the engine indexes a table with it unchecked
        uint8_t RunningPackageType = 0;   // TESPackage::packType of the package in its running slot
        uint32_t OwnerCombatTarget{};
        uint32_t OwnCombatTarget{};       // what its own combat controller fights (AttackedBy)
        uint32_t ExtraPackageFallback{};
        std::set<uint32_t> Visuals;       // an owner's effects with hit visuals on (burning)
        std::set<uint32_t> ShownVisuals;  // a copy's visuals played for its owner's
        ActorValues Values = FullValues();
        std::map<uint32_t, float> Bases = FullValues().ActorMaxValuesList;
        std::map<uint32_t, float> TemporaryValues;
        Inventory Items; // its CurrentMagicEquipment: the hands' selected spells (Actor::selectedSpells +0x1C8)
        // The weapon or torch each hand holds (AIProcess::equippedObjects, left and right): put there by an equip with
        // 3D, emptied by TESNPC::ResetWornToOutfit 418622 (39811-39813) whose worn flags stay (measured, Aela's torch).
        uint32_t HeldWeapons[2]{};
        // The spells its hands hold (AIProcess::equippedObjects, left and right): an equip without 3D only selects.
        uint32_t HeldSpells[2]{};
        bool EquipmentNeedsReset = false;
        struct ReplayState : AnimationReplayStep
        {
            ReplayState() = default;
            ReplayState(const ReplayState& acOther) { *this = acOther; }
            ReplayState& operator=(const ReplayState& acOther)
            {
                Phase.store(acOther.Phase.load());
                Retries = acOther.Retries;
                Action = acOther.Action;
                Variables = acOther.Variables;
                PairHere = acOther.PairHere;
                ClipsLoaded = acOther.ClipsLoaded;
                return *this;
            }
        } Replay;
        ActionEvent LatestAnimation;
        ActionEvent PairHalf; // its half of the pair it last took (32803 knows its instance's halves)
        std::string GraphState;
        std::set<std::string> LoadedClips; // the event whose clip files its process holds: one action set (KeepSet 40055)
        std::vector<std::pair<uint32_t, bool>> AnimObjects; // its props (AnimationObjects 400328): ANIO, drawn
        AnimationVariables GraphVariables; // an owner's to send; a copy's as last loaded (ApplyRemoteMovement)
        uint32_t GraphRandom = 0;          // its graph's generator: each game's graph runs its own (hkbBehaviorGraph +0x78)
        uint32_t LyingPose = 0;            // the get-up candidate its own ragdoll lies closest to (FakeGraphRules::Match)
        uint32_t LoopFrames = 0, LoopsSeen = 0, LoopsToSend = 1;
        double AnimationMs = 0.0;             // its graph's own clock: the frames' time while its graph updates
        double AnimationStepMs = 0.0;         // what this frame's graph update added to AnimationMs
        double InterpCountdownMs = 0.0;       // a far owner's graph: until its next chunk (63587)
        FakeGraphMemory Graph;                // its graph's clip, channels and history (FakeGraph.h)
        uint32_t GraphGeneration = 0;         // which graph: a 3D reload makes a new one (ReloadGraph)
        uint32_t GraphProject = 1;            // its graph's behaviour project, its race's (projectName hashed)
        uint32_t ForeignPoints = 0;           // another project's sync points restored into its graph (58373)
        AnimationVariables ReplayedOn;        // a copy's graph variables as its last replayed event was sent
        std::vector<uint32_t> LoopDraws;   // each number of loops its graph drew (BSEventEveryNEventsModifier)
        // Native skeleton observations are independent of the latest action and saved state-machine IDs.
        // Optional: the fake has no Havok/IK solver; fixtures supply readings at an observed boundary.
        struct BodyPose { float Head; float Pelvis; float FloorGap; };
        std::optional<BodyPose> Pose;
        std::optional<BodyPose> EvaluatedPose;
        bool PoseInFrustum = true;
        bool BlockResurrect = false;
        uint32_t ResurrectCalls = 0;
        uint32_t ValueWrites = 0;
        uint32_t Made = 0; // which actor at this form id: a reused id is a new one (a fresh ActorExtension)
        // Where it stood at its last graph update (Frame). A skeleton its ragdoll poses, its actor moved after the graph
        // update, is drawn in its old pose at the new place for a frame: a snap.
        float GraphPlace[4]{}; // x, y, z, heading
        // A player's death ragdoll (physics.md, "A revived player's ragdoll stays in the world"): its death puts it in
        // the world (KillImpl); while in the world, a living actor's skeleton stands on its bodies, which stay where
        // they were. RagdollListed: its graph's ragdoll passes run at this frame's step (listed by 63587 via 63761; 63562).
        bool RagdollInWorld = false;
        bool RagdollForDeath = false;
        bool RagdollListed = false;
        glm::vec3 RagdollAt{};
        uint32_t Snaps = 0;
        float WorstSnap = 0.f; // units: the move, and the turn at a body's reach (kSnapReach)
    };
    void RenderBodyPose(uint32_t aActor, Actor::BodyPose aPose)
    {
        auto& actor = Actors.at(aActor);
        actor.EvaluatedPose = aPose;
        if (actor.PoseInFrustum) actor.Pose = aPose;
    }
    void SetBodyVisible(uint32_t aActor, bool aVisible)
    {
        auto& actor = Actors.at(aActor);
        actor.PoseInFrustum = aVisible;
        if (aVisible && actor.EvaluatedPose) actor.Pose = actor.EvaluatedPose;
    }
    static constexpr float kSnapReach = 60.f;
    struct Npc
    {
        std::string Name;
        uint32_t Race = 0;
        float Weight = 50.f; // this game's player's is its record's (Player.Weight)
        std::string Looks;   // a copy's: the save data it was made from (CreateCustomActor)
        uint16_t Level = 1;
        uint32_t FaceBase = 0;
        uint32_t ChangeFlags = 0;
    };
    std::map<uint32_t, Npc> Npcs;
    std::map<uint32_t, Actor> Actors{{kPlayer, Actor{.Base = 0x7, .Player = true}}};
    std::map<std::string, std::string> PooledNames;
    static constexpr uint32_t kPlayer = 0x14;
    mutable std::map<std::pair<uint32_t, int32_t>, uint32_t> ValueReads;
    mutable std::map<std::pair<uint32_t, int32_t>, uint32_t> MaximumReads;
    // A behaviour's transitions; a clip's own annotation can advance only this game's graph.
    std::map<std::pair<std::string, std::string>, std::string> GraphTransitions;
    std::map<uint32_t, uint32_t> RefusedAnimations;
    static constexpr uint8_t kBipedLayer = 8; // a ragdoll's bodies' collision layer in the world (L_BIPED)
    bool PairBlows = false; // an owner's pair lands its blow on its victim's copy after PairBlowFrames: uninterruptible (pa_KillMoveI)
    uint32_t PairStopsDropped = 0; // a clip's entry transition drops this many pairedStops (flags 0xd04)
    uint32_t PairBlowFrames = 2000; // after the other game replays the pair (~700 frames here), as HitFrame at ~1.2 s is
    uint32_t PairEndFrames = 0; // after its blow, until the attacker's clip sends KillMoveEnd (0: four times the blow's)
    double PairMarkMs = 0.0; // how long a pair's actors take to reach their marks in its instance (150 ms on the host)
    bool PairMarkBlocked = false; // the mark sweep (TryMoveTo 78260) fails here: this game's positions, its own walls
    uint32_t BrokenPairs = 0; // a plain action took a copy out of its pair's clip, the pair ended before its blow
    uint32_t CollapsedPairs = 0; // a pair played on two actors already paired: both clips ended at once (32803)
    bool ReplayAnimation(uint32_t aFormId, const ActionEvent& acAction);
    bool PlayReplayed(uint32_t aFormId, const ActionEvent& acAction); // AnimationSystem::PlayReplayedAction
    void StepLoop(Actor& aActor);
    bool Replaying = false; // under PlayReplayed (g_forceAnimation)
    void EndKillMove(uint32_t aFormId);
    // ActionGetUp (the default object): replayed on a knocked-down copy it stands it up (AnimationSystem's
    // PerformAction, KnockReplay); nothing else ends a living copy's knock.
    static constexpr uint32_t kActionGetUp = 0x13FF0;
    static constexpr uint32_t kActionRagdoll = 0x13FEF; // what a knock plays (39895)
    // KillActorHandler 42838, a victim's clip's KillActor: dies credited to its pair partner, unless its kill is held
    // (a copy, an NPC taken for a claimed kill move): then counted, its pose held.
    void ClipKillActor(uint32_t aFormId);
    // A clip whose last update sends PairEnd, KillMoveEnd and KillActor together (pa_KillMoveI's victim, duo 2026-11-22).
    void ClipEndsAtBlow(uint32_t aFormId);
    bool m_applyingOwnHit = false; // TakesDamage's Apply: the engine's own hit, not the bridge's forwarded one
    uint32_t PlainSwings = 0; // ActorPerformPlainly taken: the engine's second try after a refused pair (42350)
    static constexpr const char* kPlainSwing = "attackStart";
    [[nodiscard]] bool PoseHeld(uint32_t aFormId) const noexcept; // HookUpdateAnimationGraphs: its graph takes no update
    void RagdollContact(uint32_t aFormId); // a kill move's victim touches something: 63092 kills it, unless held back
    void StopPairClip(uint32_t aFormId); // pairedStop: the clip leaves, unless its transition drops it
    bool PerformAnimation(uint32_t aFormId, ActionEvent aAction, bool aQuery = false);
    // Skyrim: an event's clip started before its file is in lasts 0.01333 s, so the graph runs on to what follows it.
    std::map<std::string, std::string> SkippedClips; // a state to the state it runs on to
    std::map<std::string, std::string> ClipFiles;    // an event to its clip's state: it needs a file loaded first
    // A state whose clip fires AnimObjLoad/AnimObjDraw as it starts (42888/42889): entering it holds these props.
    std::map<std::string, std::vector<std::pair<uint32_t, bool>>> StateProps;
    // Events with clip files a process loads into another set (IMovementSelectIdle's): loaded into the action set
    // anyway (ActionClips::Request), they replace what it held (KeepSet 40055).
    std::set<std::string> OtherSetClips;
    // A state to the states its machine starts in at random (hkbStateMachine 59406): gestures, idle variants.
    std::map<std::string, std::vector<std::string>> GraphPicks;
    // A state whose clip loops, and after a random number of loops sends to the other (Lucan's counter): no action.
    struct GraphLoop
    {
        std::string State;
        std::string Other;
        uint32_t Frames = 1; // a loop's length
        uint32_t Most = 3;   // loops before it sends, 1 up to this
    };
    std::optional<GraphLoop> Loop;
    // The graph's rules (FakeGraph.h), on every actor's graph: clips that end with no event, range modifiers, channels.
    std::optional<FakeGraphRules> Graph;
    // An owner's AI performs Events LeadMs of the frames' time before State's clip ends (the wolf's turnStop came 30-45 ms
    // before TurnCannedR180's end, duo trace 2026-11-08); the graph refuses them, their ActionWrites still land.
    struct ClipEndActions
    {
        std::string State;
        std::vector<std::string> Events;
        double LeadMs = 40.0;
    };
    std::optional<ClipEndActions> OwnerClipEnds;
    // A graph update or event the graph took, in its history.
    void GraphEntry(Actor& aActor, std::string aInput) const;
    void UpdateGraph(uint32_t aFormId, Actor& aActor);
    void WriteGraphChannels(Actor& aActor, double aStepMs);
    void StepGraph(Actor& aActor, double aStepMs);
    // Its generate pass (63568): after each frame its graph updated in (63589), and an owner's replayed on its copies.
    void GenerateGraph(Actor& aActor) const;
    bool TakeGraphEvent(Actor& aActor, const std::string& acEvent);
    void EnterClip(Actor& aActor) const;
    static void HoldAnimObjects(Actor& aActor, std::span<const std::pair<uint32_t, bool>> aProps);
    void ReplayStream(uint32_t aFormId, Actor& aActor); // a copy on the stream: its owner's ops instead (GraphStream.h)
    bool m_replaying = false; // inside ReplayStream: a copy's graph takes its owner's inputs
    // Its 3D reloaded: a new graph in aState, its clip and variables reset (Skyrim: a new BShkbAnimationGraph).
    void ReloadGraph(uint32_t aFormId, const std::string& aState, uint32_t aProject = 0); // aProject: its new race's
    [[nodiscard]] static const void* GraphIdentity(const Actor& acActor) noexcept
    {
        return reinterpret_cast<const void*>(uintptr_t(acActor.GraphGeneration) + 1);
    }
    // Its graph's variables as the stream's words: kGraphWords integers, then kGraphWords floats' bits.
    static constexpr size_t kGraphWords = 8;
    [[nodiscard]] static std::vector<uint32_t> GraphWords(const Actor& acActor);
    static void SetGraphWords(Actor& aActor, std::span<const uint32_t> aWords);
    [[nodiscard]] std::vector<std::string> GraphEventTable() const;
    [[nodiscard]] uint16_t GraphEventId(const std::string& acEvent) const;
    // Animation interpolation (63587): an owner's graph far from this game's camera, not forced, steps this much at
    // once when its countdown runs out (fAnimInterpMaxTime 250 ms beyond fAnimInterpFarDist), and not between. 0: every
    // frame. A copy's and a recorded owner's are not interpolated (the stream's hooks), so they step every frame.
    double FarOwnerChunkMs = 0.0;
    // 39004's integer writes by event: run by PerformAction (38949, not for a query) and ForceAction (38953)
    // whether or not the graph takes the event (TurnStop: iSyncTurnState 1). {variable, value}.
    std::map<std::string, std::pair<size_t, int32_t>> ActionWrites;
    // An action that sets (true) or clears a bit of its actor's state as the engine takes it (ActionSneak toggles
    // ActorState1::sneaking), on an owner and on a copy replaying it.
    std::map<std::string, std::pair<uint32_t, bool>> ActionStates;
    void ApplyActionWrites(uint32_t aFormId, const std::string& acEvent);
    uint32_t DrawGraphRandom(Actor& aActor) noexcept; // 59406, 63213: seed * 1664525 + 1013904223
    // Exit actions (HorseExit) and their clips' length in frames: the actor wants to stand (sit state 4) from the
    // action until its own clip's exit event (ChairFurnitureExitHandler 42835), which unlinks it from its mount. A
    // sync point loaded into a copy's graph before then ends the clip without the event.
    std::map<std::string, uint32_t> ExitClips;
    static constexpr uint32_t kSitShift = 14;
    [[nodiscard]] uint8_t SitState(uint32_t aFormId) const { return Actors.at(aFormId).State1 >> kSitShift & 0xF; }
    uint32_t ClipLoadFrames = 3;
    // AIProcess's action load (39625's half): nothing when the event needs no file; set once it is in (aDone, or a
    // new one).
    std::shared_ptr<std::atomic<bool>> RequestActionClips(uint32_t aFormId, const std::string& acEvent,
                                                         std::shared_ptr<std::atomic<bool>> aDone = nullptr);
    // Clip requests made inside a copy's graph update, its graph manager's lock held (63358): the loader's job takes
    // that lock under the locks a request takes (63997), so Skyrim can stop there for good (animation-file-loads.md).
    size_t ClipRequestsUnderGraphLock = 0;
    struct ClipLoad
    {
        uint32_t FormId;
        std::string Event;
        uint32_t Frames;
        std::shared_ptr<std::atomic<bool>> Done;
        uint64_t Lock = 0;            // the requester's (FakeLoadLock), taken before Loaded (63997)
        std::function<void()> Loaded; // its callback, under that lock; none: the action set's
    };
    std::vector<ClipLoad> ClipLoads;
    uintptr_t m_nextClipLoad = 0;
    // Loads that came to a lock no longer there: Skyrim's animation file job would spin on it forever (63997).
    size_t ClipLoadHangs = 0;
    // An owner's movement selector loads an idle's clips into its own set (IMovementSelectIdle 41694, kind 47).
    void LoadMovementClips(uint32_t aFormId, const std::string& acEvent);
    // A copy's movement idle set, loaded as its owner's stream says (ActionClips::Request, kMovement).
    std::shared_ptr<std::atomic<bool>> RequestMovementClips(uint32_t aFormId, const std::string& acEvent,
                                                           std::shared_ptr<std::atomic<bool>> aDone = nullptr);
    struct MovementSet
    {
        std::string Event;
    };
    HeldClipLoads<MovementSet, FakeLoadLock> MovementSets;
    bool NewCopiesGraphReady = true; // a copy's graph ready as it is made (Skyrim: its 3D can come later)
    bool IsAnimationGraphReady(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && it->second.GraphReady;
    }
    bool HoldDeathActions = false;
    bool RefuseDeathActions = false;
    void ReplayDeathAction(uint32_t aFormId, const ActionEvent& acAction);
    void KillBesideGraph(uint32_t aFormId, const ActionEvent& acAction);
    static constexpr const char* kDeathEvent = "DeathAnim"; // its death action's graph event
    void FinishDeath(uint32_t aFormId);
    ActionEvent GetActorLatestAnimation(uint32_t aFormId) const noexcept override;
    std::string SaveActorGraphState(uint32_t aFormId) const noexcept override;
    // A sync point's: the whole graph, its generators' choices too (Havok's internal state, not Skyrim's save).
    [[nodiscard]] std::string SaveGraphInternalState(uint32_t aFormId) const;
    // Graphs restored from a point taken in a cross-blend: Skyrim crashes at their next update (59960, null blenders).
    uint32_t RestoredMidCrossBlend = 0;
    bool LoadGraphInternalState(uint32_t aFormId, const std::string& acState);
    bool LoadActorGraphState(uint32_t aFormId, const std::string& aState) noexcept override;
    // TESObjectREFR::LoadGraphStateFlags: a saved state's actor state words alone.
    bool LoadGraphStateFlags(uint32_t aFormId, const std::string& acState) noexcept;
    void SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept override;

    // This game's player travels by itself (a door, coc): to aCell at aPosition, that cell loaded and the one it
    // left unloaded, a teleport the engine reports once (ConsumeTeleported).
    void PlayerTravels(uint32_t aCell, const glm::vec3& acPosition);
    // A move the hook at 40438's entry does not see: PayFine's release (QueueJailRelease 40661) is written into
    // queuedTarget inside ProcessQueuedTravel (40438), so only the arrival reports it (docs/re/systems/player-travel.md).
    void PlayerTravelsUnannounced(uint32_t aCell, const glm::vec3& acPosition);
    // The native mount interaction, replayed for copies by CharacterService.
    std::map<uint32_t, uint32_t> Mounts;
    void ActorPutOnMount(uint32_t aRider, uint32_t aMount) noexcept override
    {
        Mounts[aRider] = aMount;
        MountSlots.try_emplace(aMount, aRider); // 12074 adds only to an empty slot
        // A copy replays its owner's get-on: HorseEnter (39004) sets sit state 2. The fake plays no clip.
        if (const auto it = Actors.find(aRider); it != Actors.end() && it->second.Copy && SitState(aRider) == 0)
            it->second.State1 = (it->second.State1 & ~(0xFu << kSitShift)) | 2u << kSitShift;
    }
    void ActorEndRide(uint32_t aRider) noexcept override;
    void ActorSeatOnMount(uint32_t aRider, uint32_t aMount) noexcept override;
    // Riders only: a mount's slot holds its rider's link, which is no mount of its own.
    uint32_t GetActorMount(uint32_t aRider) const noexcept override
    {
        const auto it = Mounts.find(aRider);
        return it != Mounts.end() ? it->second : 0;
    }
    void ActorHoldMount(uint32_t aRider, uint32_t aMount) noexcept override { MountSlots.try_emplace(aMount, aRider); }
    void ActorEndMountPair(uint32_t aMount) noexcept override { SendGraphEvent(aMount, "PairEnd"); }
    uint8_t GetActorSitState(uint32_t aFormId) const noexcept override { return Actors.contains(aFormId) ? SitState(aFormId) : 0; }
    void ActorFinishDismount(uint32_t aRider) noexcept override { EndRide(aRider); } // its sit state its clip's
    // A mount's half of its rider's exit (MountBehavior: pa_HorseExit -> DismountSynced, left only on PairEnd, which only
    // the pair's instance sends: 32797, in the frame of the owner rider's Detach, just before it; duo 2026-11-19). A copy
    // takes both from its owner's stream; a mount this game took before its copy ran that PairEnd stays synced and
    // steers nothing (mounts.md). MountGraph puts the rules on an engine.
    static constexpr const char* kDismountSynced = "DismountSynced";
    static void MountGraph(FakeEngine& aEngine);
    [[nodiscard]] bool Synced(uint32_t aMount) const { return Actors.contains(aMount) && Actors.at(aMount).GraphState == kDismountSynced; }
    // An event the engine sends a graph (63591): recorded for an owner's copies, dropped on a copy on its stream.
    void SendGraphEvent(uint32_t aFormId, const std::string& acEvent);
    // A get-on of a mount left in a pair: pa_HorseEnter refused (its refusal's PairEnd ends the pair), the mount
    // procedure (39669) calls InitiateMountPackage (37905) again, and the next try starts the ride.
    std::map<uint32_t, uint32_t> MountRetryOf; // rider -> mount
    uint32_t RefusedGetOns{};
    // A mount's one ExtraInteraction (12074): its first rider's; any rider's Detach (41539) empties it, and a ridden
    // one is what ActivateMount (37864) and CanRide (38706) refuse (player-mount.md).
    std::map<uint32_t, uint32_t> MountSlots;
    // Riders SeatRider (40358) ran for: 41533, a quick get-on. Its REFRSyncController (20318) carries their reference;
    // a linked copy at sit state 3 without it stayed where it got on (the duo, 2026-11-20).
    std::set<uint32_t> SeatedRiders;
    // A test's lever: the quick get-on's ActionActivate refused (41530's result 2; what refuses it is not read). Linked
    // already, its rider's link ends at the interaction's next update (40356: a rider not seated).
    bool RefuseQuickSeat = false;
    // Mounts the player steers: its own get-on's, or a passenger's handed the reins (PlayerTakeReins). Skyrim, probe
    // 4c: the driver's ride ending leaves the horse on the driver's package, the passenger steering nothing.
    std::map<uint32_t, uint32_t> Reins;
    [[nodiscard]] bool PlayerSteers(uint32_t aMount) const
    {
        return GetActorMount(kPlayer) == aMount && Reins.contains(aMount) && Reins.at(aMount) == kPlayer &&
               MountSlots.contains(aMount) && MountSlots.at(aMount) == kPlayer && !Synced(aMount);
    }
    // NPC bases that are mounts (flags2 kIsAMount): a horse.
    std::set<uint32_t> MountBases{0x109E3D};
    bool IsActorMount(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && MountBases.contains(it->second.Base);
    }
    // flags2 kGettingOnOffMount, set by InitiateMountPackage (37905) and Dismount (37906): a copy runs neither
    // package, so on a copy it stays (in Skyrim until its next ride).
    std::set<uint32_t> GettingOnOff;
    // A character activating a mount (Character::ActivateMount 37864): its own mount -> Dismount; free ->
    // InitiateMountPackage (an own actor rides, our hook raising MountEvent); ridden by another -> nothing.
    bool ActivateMount(uint32_t aActivator, uint32_t aMount);
    // Actor::Dismount (37906) on a rider: its exit clip ends the ride aFrames later (FinishDismount 38696).
    void StartDismount(uint32_t aRider, uint32_t aFrames = 3) noexcept;
    // StopRidingQuick (38705) -> StopRiding (41531), no Dismount. Mode 2 (jail, a fine, a load, a low-process mount,
    // a refused ride) forces 40352's finish: off at once, the exit clip only shown. Mode 0 (the dismount procedure,
    // 39670) ends at its exit clip, aFrames later.
    void StopRiding(uint32_t aRider, uint32_t aMode = 2, uint32_t aFrames = 3) noexcept;
    // RefrInteraction::Detach (41539), every ride's end: off its mount; our hook tells this game's services.
    void EndRide(uint32_t aRider) noexcept;
    std::map<uint32_t, uint32_t> DismountIn; // rider -> frames until its exit clip ends its ride
    bool Teleported = false;
    float Heading = 0.f; // the player's (SetPlayerHeading)
    size_t Travels = 0;  // the player's teleports, ever
    // Each cell's COC marker (TESObjectCELL::GetCOCPlacementInfo), where a respawn puts the player; an exterior
    // without one: its centre.
    std::map<uint32_t, glm::vec3> CocMarkers;
    // The calendar, run each frame as Calendar::Update does (a guest's is the host's, GuestEngine::SetCalendar), and
    // the weather (a guest's held to the host's, GuestEngine::HoldWeather).
    TimeModel Calendar;
    GameId Weather{};
    bool WeatherHeld = false;
    TimeModel GetGameTime() const noexcept override { return Calendar; }
    // Magic. A spell's kind as MagicService asks it (SpellItem's casting type and archetypes).
    struct SpellForm
    {
        bool Concentration = false;
        bool Healing = false;
        bool Buff = false;
        bool Damage = false; // its effect damages health
        bool Invisibility = false; // fire and forget, on its caster
        uint32_t Summons = 0; // its effect makes this NPC base, commanded by the caster (SummonCreatureEffect)
    };
    static constexpr uint32_t kConjureFamiliar = 0x000640B6;
    // VoiceUnrelentingForce3 as Dragonborn.esm has it: DLC2VoiceDisintegrateFFAimed (0401FF2B) among its effects.
    static constexpr uint32_t kUnrelentingForce3 = 0x00013F3A;
    static constexpr uint32_t kDisintegrateEffect = 0x0401FF2B;
    // Actor::SetCriticalStage (37615) under our hook's rule; 37616 takes the 3D of an actor at GooEnd or
    // DisintegrateEnd. This game's player left without it stalls the frame (FrameStalled).
    void SetCriticalStage(uint32_t aFormId, uint32_t aStage);
    // Measured: a player left with no 3D froze its game, Main::Update waiting for the frame's jobs (35409) and a job
    // in WaitForJobTask (69516) for good; which wait job, inferred (death-disintegrate.md). Nothing of it runs again.
    bool FrameStalled = false;
    // The camera's switch of the player's two roots (PlayerCharacter::ShowFirstPersonRoot 40476, read): opposite
    // kHidden bits, written only if they are equal or the target root is not yet as wanted; then our hook keeps an
    // ash-dead player's third-person root hidden.
    void ShowFirstPersonRoot(bool aFirstPerson);
    // A camera state that switches the roots (FirstPersonState::OnEnterState 50722, ThirdPersonState::Update).
    void CameraEnters(bool aFirstPerson);
    bool CameraFirstPerson = false;
    bool AshDead = false; // CriticalStage.cpp's mark: this game's player died to an ash or goo end
    void KeepBodyHidden();
    // Actor.AttachAshPile (54655) under our hook's rule: the actors a pile was placed at (AttachAshPileFunctor).
    void AttachAshPile(uint32_t aFormId);
    std::vector<uint32_t> AshPiles;
    // Effects whose script turns their target to ash if it dies while they last (DLC2AttachAshPileOnDeath25's
    // OnDying: critical stage DisintegrateStart, AttachAshPile, DisintegrateEnd), and how long they last (ms).
    std::map<uint32_t, uint64_t> AshPileEffects{{kDisintegrateEffect, 1000}};
    // Each actor's such effect: the tick it ends at.
    std::map<uint32_t, uint64_t> AshPileUntil;
    void StartsAshPileEffect(uint32_t aTarget, uint32_t aEffect);
    static constexpr uint32_t kFamiliarEffect = 0x000640B4;
    static constexpr uint32_t kFamiliar = 0x000CAE0B; // stands for the familiar's base
    std::map<uint32_t, SpellForm> SpellForms{{0x00012FCD, {.Concentration = true}},                  // Flames
                                             {0x00012FCC, {.Concentration = true, .Healing = true}}, // Healing
                                             {0x0005AD5C, {.Buff = true}},                           // Oakflesh
                                             {0x00027EB6, {.Invisibility = true}},                   // Invisibility
                                             {0x0001C789, {.Damage = true}},                         // Firebolt
                                             {kUnrelentingForce3, {.Damage = true}},
                                             {kConjureFamiliar, {.Summons = kFamiliar}}};
    // A cast this game's engine made (ActorMagicCaster::SpellCast), and an effect it applied (MagicTarget::AddTarget).
    struct Cast
    {
        uint32_t Caster = 0;
        uint32_t Spell = 0;
        uint32_t Source = 0;
        uint32_t Target = 0;
    };
    std::vector<Cast> Casts;
    // A concentration spell held, by caster and hand: state 6 until MagicCaster::InterruptCast (34408).
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> Channels;
    // The actor lets go of its channel: the graph's InterruptCast event (InterruptCastHandler -> 34408 -> 34140), which
    // our hook raises only for this game's actor (HookInterruptCast).
    void StopsCasting(uint32_t aCaster, uint32_t aSource);
    void InterruptCast(uint32_t aFormId, uint32_t aCastingSource) noexcept override { Channels.erase({aFormId, aCastingSource}); }
    struct Effect
    {
        uint32_t Target = 0;
        uint32_t Caster = 0;
        uint32_t Spell = 0;
        uint32_t EffectId = 0;
    };
    std::vector<Effect> Effects;
    std::vector<std::pair<uint32_t, uint32_t>> Reactions; // victim, attacker (Actor::ReactToAttack)
    // The actor casts by itself: our hook raises it unless the caster is a copy (HookSpellCast).
    void CastsSpell(uint32_t aCaster, uint32_t aSpell, uint32_t aSource, uint32_t aTarget = 0);
    // This game's spell lands on an actor (HookAddTarget): on another game's actor, our cast is a request for its
    // owner and is not applied here; on our own, applied.
    void SpellHits(uint32_t aTarget, uint32_t aCaster, uint32_t aSpell, uint32_t aEffect, float aMagnitude);
    // A summon effect's actor (SummonCreatureEffect::CreateSummon 34993): one of aBase at its caster, commanded by it;
    // the caster's earlier summon dismissed (AIProcess::AddCommandedActor's limit, 1 without Twin Souls).
    uint32_t MakesSummon(uint32_t aCaster, uint32_t aBase);
    bool IsConcentrationSpell(uint32_t aFormId) const noexcept override { return Spell(aFormId).Concentration; }
    bool IsHealingSpell(uint32_t aFormId) const noexcept override { return Spell(aFormId).Healing; }
    bool IsInvisibilitySpell(uint32_t aFormId) const noexcept override { return Spell(aFormId).Invisibility; }
    bool IsBuffSpell(uint32_t aFormId) const noexcept override { return Spell(aFormId).Buff; }
    bool IsMagicItem(uint32_t aFormId) const noexcept override { return SpellForms.contains(aFormId); }
    void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                   uint32_t aDesiredTargetFormId) noexcept override
    {
        Casts.push_back({aActorFormId, aSpellFormId, aCastingSource, aDesiredTargetFormId});
        if (Spell(aSpellFormId).Concentration)
            Channels[{aActorFormId, aCastingSource}] = aSpellFormId;
    }
    void AddMagicTargetEffect(uint32_t aTargetFormId, uint32_t aCasterFormId, uint32_t aSpellFormId,
                              uint32_t aEffectFormId, float aMagnitude, bool aIsDualCasting, bool aApplyHealPerkBonus,
                              bool aApplyStaminaPerkBonus) noexcept override
    {
        Effects.push_back({aTargetFormId, aCasterFormId, aSpellFormId, aEffectFormId});
        StartsAshPileEffect(aTargetFormId, aEffectFormId);
        if (Spell(aSpellFormId).Summons && !HoldSession) // a guest's CreateSummon (34993) makes nothing
            MakesSummon(aCasterFormId, Spell(aSpellFormId).Summons);
        // A damaging effect's value goes through the damage hook while applied (HookApplyActorEffect).
        if (Spell(aSpellFormId).Damage)
            TakesDamage(aTargetFormId, aCasterFormId, aMagnitude, true);
    }
    uint32_t GetKillerFormId(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && it->second.Dead ? it->second.KilledBy : 0;
    }
    void ReactToAttack(uint32_t aVictimFormId, uint32_t aAttackerFormId) noexcept override
    {
        Reactions.emplace_back(aVictimFormId, aAttackerFormId);
    }
    // A player's progression (docs/design/dragonborn.md, "The party's calls"): what its natives write. Words are the
    // world's (form flags, in a save); souls, factions and spells each actor's; the rewards this game's player's.
    std::set<uint32_t> WordsKnown;
    std::set<uint32_t> WordsUnlocked;
    std::map<uint32_t, float> Souls;                          // DragonSouls (133) by actor
    std::map<uint32_t, std::map<uint32_t, int32_t>> Factions; // by actor: each faction it is in, at its rank
    std::map<uint32_t, std::set<uint32_t>> AddedSpells;       // by actor (Actor::AddSpell)
    std::map<uint32_t, std::map<uint32_t, uint32_t>> QuestAliasReferences;
    std::set<uint32_t> BodyQuests;                            // started by a player's body here (a beast form's)
    uint32_t WordsLearnedShown = 0; // "Word of power learned" (51648): Game.TeachWord's HUD
    uint32_t SoulsAbsorbedShown = 0; // "Dragon soul absorbed" (51649): the soul sink, this game's player's gains
    float RewardMultiplier = 1.f;    // this game's experience mods' (AdvanceSkill's input times it)
    std::map<int32_t, float> RewardExperience, RewardStats; // by actor value: skill experience, permanent stats
    std::map<int32_t, uint32_t> RewardLevels;
    int32_t RewardPoints = 0;
    bool IsKnockedDown(uint32_t aFormId) const noexcept override { return Actors.contains(aFormId) && Actors.at(aFormId).KnockedDown; }
    // Units a stepped, colliding copy in furniture is pushed off its goal, at most 3 of these: the duo's 0.3 to 1.2 (actor-movement.md).
    static constexpr float kFurnitureOffset = 0.4f;
    bool IsOnGround(uint32_t aFormId) const noexcept override { return Actors.contains(aFormId) && !Actors.at(aFormId).InAir; }
    bool CapsuleCollides(uint32_t aFormId) const noexcept override { return !Actors.contains(aFormId) || Actors.at(aFormId).Collides; }
    void SetCapsuleCollision(uint32_t aFormId, bool aCollides) noexcept override
    {
        if (Actors.contains(aFormId))
            Actors.at(aFormId).Collides = aCollides;
    }
    uint8_t BodiesLayer(uint32_t aFormId) const noexcept override { return Actors.contains(aFormId) ? Actors.at(aFormId).BodiesLayer : 0; }
    bool GraphsStepPhysics(uint32_t aFormId) const noexcept override { return !Actors.contains(aFormId) || Actors.at(aFormId).GraphsStepPhysics; }
    void SetCopyPhysics(uint32_t aFormId, uint8_t aBodiesLayer, bool aGraphsStep) noexcept override
    {
        if (!Actors.contains(aFormId) || !Actors.at(aFormId).Copy ||
            !::ReplayState::TakesOwnerBodies(Actors.at(aFormId).KnockedDown, Actors.at(aFormId).InKillMove))
            return;
        Actors.at(aFormId).BodiesLayer = aBodiesLayer;
        Actors.at(aFormId).GraphsStepPhysics = aGraphsStep;
    }
    // Its bodies in the world driven by its ragdoll driver (63562: only with its graph in the physics world), so
    // simulated, off its animation's pose; a seated actor's stay keyframed (40063 detaches its graphs).
    [[nodiscard]] bool BodiesDriven(uint32_t aFormId) const noexcept
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && it->second.BodiesLayer != 0 && it->second.GraphsStepPhysics;
    }
    // This game's crosshair on an actor it faces (CrosshairPickData::Pick): a seated or sleeping one only by its bodies
    // in the world (its capsule, collision off, is out of the pick's cast: 78553), any other by its capsule.
    [[nodiscard]] bool CrosshairTakes(uint32_t aFormId) const noexcept
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && !it->second.Dead && (!it->second.InFurniture || it->second.BodiesLayer != 0);
    }
    Perks GetPlayerPerks() const noexcept override;
    void SetCopyPerks(uint32_t aFormId, const Perks& acPerks) noexcept override;
    void StaggerActor(uint32_t aFormId, float aMagnitude, uint32_t aHitterId) noexcept override
    {
        Staggers.emplace_back(aFormId, aHitterId, aMagnitude);
    }
    std::vector<std::tuple<uint32_t, uint32_t, float>> Staggers;
    std::map<uint32_t, std::vector<PlayerRecord::Perk>> ActorPerks;
    std::set<uint32_t> RewardPerks;
    std::vector<LocalCall> RewardCalls; // the skill, perk and stat natives this game's player took
    uint32_t BarterMerchant{};
    // A script's call of them (RunScript), its stack's cause aCause (0: no player's action) and story; aQuest: the
    // script's quest (a body's, when in BodyQuests).
    void TeachesWord(uint32_t aWord, uint32_t aCause = 0);  // Game.TeachWord
    void UnlocksWord(uint32_t aWord, uint32_t aCause = 0);  // Game.UnlockWord
    void SpendsSoul(uint32_t aWord);                         // the Magic menu's unlock (SpendSoulCallback): no script
    void ModifiesSouls(uint32_t aActor, float aAmount, uint32_t aCause = 0); // Actor.ModActorValue("DragonSouls")
    void SetsFaction(uint32_t aActor, uint32_t aFaction, int32_t aRank, uint32_t aCause = 0, bool aStory = false,
                     uint32_t aQuest = 0); // Actor.SetFactionRank; -1: RemoveFromFaction
    void AddsSpell(uint32_t aActor, uint32_t aSpell, bool aAdded, uint32_t aCause = 0, bool aStory = false, uint32_t aQuest = 0);
    // The actor readies a spell in a hand (0 left, 1 right), as EquipSpellHook (38928) then the engine: selected, held
    // with 3D; a local actor's change raised (EquipmentChangeEvent).
    void EquipsSpell(uint32_t aActor, const GameId& acSpell, uint32_t aHand);
    // The actor's player selects a shout or power (none: clears it), as the favorites menu does (EquipShout).
    void EquipsShout(uint32_t aActor, const GameId& acShout);
    // The native's own write on aActor, as the engine makes it.
    void TakeProgress(const LocalCall& acCall, uint32_t aActor);
    // Objects: an actor activates one by itself (opens a door, pulls a lever), as our hook on TESObjectREFR::Activate
    // raises it; a guest's activation run on the host's game (HostGame::Activate), recorded.
    void Activates(uint32_t aActor, uint32_t aObject);
    std::vector<std::pair<uint32_t, uint32_t>> Activations; // object, activator
    uint32_t LootMenuOf{}; // the dead actor whose loot menu the player opened (TESNPC::Activate -> OpenMenu 51140)
    uint32_t ChestMenuOf{}; // the chest whose menu the player opened (TESObjectCONT::Activate 17887)
    bool InventoryMenuOpen{}; // the player's InventoryMenu (InventoryMenu::ProcessMessage 51848's kShow)
    uint32_t BarterStock{};   // what an open BarterMenu sells: TESDataHandler +0xDB8's owner (50960)
    // An open item menu lists its holders' stacks by their own lists (16106 at 0x14023929d). TESObjectREFR::RemoveItem
    // (19689, our bridges' path) queues only that item's update, which ContainerMenu keeps for its target until a full
    // update (51126; measured on a corpse), so a target's listed list freed is read again by the next take or hover.
    // Measured re-listed at once: the player's own stacks (ContainerMenu's player side, InventoryMenu) and a script's
    // or the console's removal (LosesItem). Barter's is not modelled. Marked to rebuild (Update_Impl 51031), the selection is hidden
    // until the UI update rebuilds it (51144, at Frame). One item menu is open at a time.
    bool ItemMenuPending{};
    int ItemMenuFreedLists{}; // listed lists freed since the menu last listed
    int ItemMenuFreedReads{}; // a take or hover read a freed list (the 2026-11-22 guest crashes, GetExtraData 12329)
    [[nodiscard]] ItemMenuView ItemMenuOf() const noexcept
    {
        if (LootMenuOf || ChestMenuOf)
            return {.Open = true, .Lists = {LootMenuOf ? LootMenuOf : ChestMenuOf, kPlayer}};
        if (BarterMerchant)
            return {.Open = true, .Lists = {BarterStock, kPlayer}};
        return {.Open = InventoryMenuOpen, .Lists = {0, kPlayer}};
    }
    // The player hovers or takes the menu's highlighted entry (GetSelectedItem's lists: ContainerMenu 51130 / 51132).
    void HoversInItemMenu() noexcept
    {
        if (ItemMenuOf().Open && !ItemMenuPending && ItemMenuFreedLists)
            ++ItemMenuFreedReads;
    }
    // A change to aHolder's stacks: our hooks first (ItemMenuWatch::RebuildFirst, at RemoveItem 16059: the item's update
    // and Update_Impl), then the engine's, a stack with extra data gone freeing its list (16059).
    template <class Change> void ChangesStacks(uint32_t aHolder, Change&& aChange)
    {
        if (MustRebuildFirst(ItemMenuOf(), aHolder))
            ItemMenuPending = true;
        const auto cLists = [&] {
            const auto cActor = Actors.find(aHolder);
            return cActor == Actors.end() ? 0 : std::ranges::count_if(cActor->second.Items.Entries, [](const auto& acEntry) { return acEntry.ContainsExtraData(); });
        };
        const auto cBefore = cLists();
        aChange();
        const auto cView = ItemMenuOf();
        if ((LootMenuOf || ChestMenuOf) && !ItemMenuPending && aHolder == cView.Lists[0])
            ItemMenuFreedLists += static_cast<int>(std::max<std::ptrdiff_t>(0, cBefore - cLists()));
    }
    // The chest's menu closes (the player, or our auto-close on a disabled chest): the lid's Close plays (ContainerMenu
    // 51125 -> OpenOrClose 14284); with no 3D only its flag closes.
    void ClosesChestMenu();
    // A reference's 3D load, queued by Enable (TESQueuedHandles::QueueReference), finishes: its lid at its open state,
    // part open if that changed while it had no 3D (measured 2026-11-18: 0.116 of 0.128 rad, every run).
    void Finishes3D(uint32_t aReference);
    static constexpr float kPartOpen = 0.9f;
    // SetOpenState with its snap (14287): the lid at its open flag at once, once enabled and loaded.
    bool PoseObjectOpenState(uint32_t aObjectFormId) noexcept override
    {
        const auto it = References.find(aObjectFormId);
        if (it == References.end())
            return true;
        if (it->second.Disabled || !it->second.Has3D)
            return false;
        if (it->second.Chest)
            it->second.Lid = it->second.Open ? 1.f : 0.f;
        it->second.Unposed = false;
        return true;
    }
    bool ObjectHasLock(uint32_t aObjectFormId) const noexcept override
    {
        const auto it = References.find(LockHolderOf(aObjectFormId));
        return it != References.end() && (it->second.Locked || it->second.LockLevel);
    }
    bool ObjectIsLocked(uint32_t aObjectFormId) const noexcept override
    {
        const auto it = References.find(LockHolderOf(aObjectFormId));
        return it != References.end() && it->second.Locked;
    }
    void RunsActivation(uint32_t aObjectFormId, uint32_t aActivatorFormId) noexcept
    {
        Activations.emplace_back(aObjectFormId, aActivatorFormId);
        if (!ActivateMount(aActivatorFormId, aObjectFormId))
        {
            ToggleDoor(aObjectFormId);
            Harvest(aObjectFormId);
        }
    }
    // An activated door swings: the host's records it, a guest's opens here only (the host's state follows).
    void ToggleDoor(uint32_t aObject);
    // An activated plant is picked (for any actor, 14864): the host's records it (CHANGE_OBJECT_EMPTY), a guest's here only.
    void Harvest(uint32_t aObject);
    // Combat. A melee hit, as our hook on Actor::DamageActor (Actor.cpp) takes it: raised as a hit, and a hit of this
    // game's player on another's actor raised as a health change and not applied here (its owner applies it).
    void Strikes(uint32_t aHitter, uint32_t aHittee, float aDamage);
    // Damage reaching an actor as HookDamageActor takes it (HitRules), from a swing or an effect being applied.
    void TakesDamage(uint32_t aHittee, uint32_t aHitter, float aDamage, bool aApplyingEffect);
    [[nodiscard]] HitRules::Party PartyOf(uint32_t aFormId) const noexcept;
    // A projectile this game's actor launched (our hook on Projectile::Launch raises it); one another game's actor
    // launched, relaunched here (LaunchProjectile).
    void Shoots(uint32_t aShooter, uint32_t aSpell, const glm::vec3& acOrigin);
    struct Shot
    {
        uint32_t Shooter = 0;
        uint32_t Spell = 0;
        glm::vec3 Origin{};
    };
    std::vector<Shot> Shots;
    void LaunchProjectile(uint32_t aShooterFormId, uint32_t aCellFormId, float aOriginX, float aOriginY, float aOriginZ,
                          uint32_t aProjectileBaseId, uint32_t aWeaponId, uint32_t aAmmoId, uint32_t aSpellId,
                          uint32_t aCastingSource, float aZAngle, float aXAngle, float aYAngle, int32_t aArea,
                          float aPower, float aScale, bool aAlwaysHit, bool aNoDamageOutsideCombat, bool aAutoAim,
                          bool aDeferInit, bool aForceConeOfFire, bool aUnkBool1, bool aUnkBool2) noexcept override
    {
        Shots.push_back({aShooterFormId, aSpellId, {aOriginX, aOriginY, aOriginZ}});
    }
    std::vector<uint32_t> HealthBarsShown; // HUD's enemy health (ShowEnemyHealth)
    void ShowEnemyHealth(uint32_t aActorFormId) noexcept override { HealthBarsShown.push_back(aActorFormId); }
    // Voice lines (Actor::SpeakSound 37542): it reads the actor's process unchecked, so one without 3D crashes the game.
    std::vector<std::pair<uint32_t, std::string>> Lines;
    uint32_t SpokeWithout3D = 0;
    std::vector<uint32_t> LinesStopped; // actors told to stop their line
    void ActorStopCurrentDialogue(uint32_t aFormId) noexcept override
    {
        LinesStopped.push_back(aFormId);
    }
    void ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept override
    {
        if (!ActorHas3D(aFormId))
            ++SpokeWithout3D;
        Lines.emplace_back(aFormId, acFilename);
    }
    // AI: the actor fights a target by itself (its combat AI); our hooks raise AiStateChangeEvent (AiEvents.cpp).
    void Fights(uint32_t aActor, uint32_t aTarget);
    // Actor::StartCombat (38561) as an attack runs it on its victim (AIProcess::StartAssault): the victim takes a combat
    // controller of its own, whose tree CombatUpdateJob (46902) runs. A copy refuses (HookStartCombat).
    void AttackedBy(uint32_t aVictim, uint32_t aAttacker);
    // AI: the actor looks at a target for a reason (a headtrack slot, HighProcessData::headTrackTarget), 0 none; the
    // ProcessTracking hook raises AiStateChangeEvent.
    static constexpr uint32_t kDialogueHeadTrack = 4;
    void LooksAt(uint32_t aActor, uint32_t aSlot, uint32_t aTarget);
    // AI: an owner sits in a furniture, or stands (0), as AIProcess::SetSitSleepState 39912 does: sit state, its
    // furniture's graphs linked to its own (63364), TESFurnitureEvent.
    void SitsIn(uint32_t aActor, uint32_t aFurniture);
    void EndScene(uint32_t aActor);
    ActorAiFields GetActorAiFields(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        ActorAiFields fields = it != Actors.end() ? it->second.Ai : ActorAiFields{};
        if (it != Actors.end() && it->second.Copy)
        {
            const uint32_t cNativePackage = fields.PackageId ? fields.PackageId : it->second.ExtraPackageFallback;
            fields.PackageId = CopyPackage::Read(true, it->second.Ai.PackageId, cNativePackage);
        }
        // The rider's native horse has no current package, including when an older copy snapshot just arrived.
        if (it != Actors.end() && !it->second.Copy)
            if (const auto cMount = Mounts.find(kPlayer); cMount != Mounts.end() && cMount->second == aFormId)
                fields.PackageId = 0;
        return fields;
    }
    void SetCopyAiFields(uint32_t aFormId, const ActorAiFields& acFields) noexcept override
    {
        if (const auto it = Actors.find(aFormId); it != Actors.end())
        {
            it->second.Ai = acFields;
            it->second.OwnerCombatTarget = acFields.CombatTargetId;
            // Actor::LinkFurnitureGraphs: only with the furniture's graph here.
            it->second.LinkedFurniture = FurnitureUnloaded.contains(acFields.FurnitureId) ? 0 : acFields.FurnitureId;
            if (!acFields.PackageId)
            {
                it->second.ProcedureType = CopyPackage::CreateDoNothing([&] { return &(it->second.DoNothing = {}); },
                                                                        [](FakePackage* apPackage) { apPackage->Procedure = 23; })
                                               ->Procedure;
                it->second.RunningPackageType = 27;
            }
        }
    }
    // Asking the running package's procedure: GetCurrentAIProcedure (21521) and a former copy's CheckForNewPackage (39213)
    // read a table at this index with no check, and -1 reads what lies before it: Skyrim crashes.
    bool ReadsProcedure(uint32_t aActor) const { return Actors.at(aActor).ProcedureType >= 0; }
    // Detection (stealth.md): a request queued once (42712), a pass installs and clears it (42704, DetectionRules);
    // while queued, 42702 answers the state at any age. Kept by target: the HUD asks of the player each frame, the
    // host of each guest's copy (HostEye).
    struct WitnessKnowledge
    {
        std::optional<DetectionRules::Pass> State;
        uint32_t UpdatedAt = 0; // DetectionFrame of its pass
        bool Queued = false;
    };
    std::map<uint32_t, std::map<uint32_t, WitnessKnowledge>> DetectionOf; // by target, then witness
    std::map<uint32_t, WitnessKnowledge>& KnowledgeOfPlayer = DetectionOf[kPlayer];
    std::map<uint32_t, std::map<uint32_t, int32_t>> SeesOf; // what a witness's pass computes of a target (37774), by target
    std::map<uint32_t, int32_t>& Sees = SeesOf[kPlayer];
    // An actor's lowest stealth points over this game's fights (46877; its combat groups' targets): none, 100.
    std::map<uint32_t, float> StealthPoints;
    bool PlayerInCombat = false; // the player's IsInCombat (vfunc 0xE3): the eye shows its fights' stealth
    // A guest's held reading of its player from the host (GuestEngine::HoldEye): the eye's reads answer it.
    std::optional<DetectionRules::EyeReading> HeldEye;
    uint32_t DetectionFrame = 0;
    // The witness judged the player in a pass already installed (a copy before its flags, as after a cog swap).
    void JudgedPlayer(uint32_t aWitness, int32_t aLevel);
    // Actor__ForceDetected (37765): the player talked to the witness, a conversation with it ended, or an alarm: level
    // 100 through 42705, which keeps a queued request (no pass, no hook of ours).
    void ForcesDetected(uint32_t aWitness);
    void SetActorNotOnStealthMeter(uint32_t aFormId, bool aNotShown) noexcept override
    {
        if (const auto it = Actors.find(aFormId); it != Actors.end())
            it->second.NotOnStealthMeter = aNotShown;
    }
    // The HUD's eye: the highest fresh detection of the player among loaded actors (40593 through 41408).
    int32_t StealthEye();
    // 41408 of any target (the player's skips its flagged witnesses), with StealthEye's hook on a guest's player.
    int32_t HighestDetection(uint32_t aTarget, uint32_t& aSeeing);
    // 46877, with the same hook.
    [[nodiscard]] float LowestStealthPoints(uint32_t aTarget) const;
    // What the HUD's eye shows (40593, DetectionRules::EyeShows).
    int32_t EyeShows();
    [[nodiscard]] const DetectionRules::Pass* FreshState(uint32_t aWitness, uint32_t aTarget = kPlayer);
    void DetectionPass(); // DoDetectionJob 42742: each queued request answered
    // Actor::ProcessTracking (38009), each frame: an actor whose look is this game's player asks its own detection of
    // that player (37758) and, with a state of level 0 or less, drops its default slot (slot 0).
    void ProcessTracking();
    uint32_t CopiesFightingPlayer = 0; // the player's combat state counts them (CombatState hook)
    void SetCopiesFightingPlayer(uint32_t aCount) noexcept override { CopiesFightingPlayer = aCount; }
    // Effect visuals: an effect with hit visuals (burning) starts or ends on the actor (EffectEvents raises it).
    void Burns(uint32_t aActor, uint32_t aEffect, bool aOn);
    std::map<uint32_t, uint32_t> EffectVisualFlags;
    std::vector<std::pair<uint32_t, uint32_t>> FlashedVisuals;
    void StartsHitVisuals(uint32_t aActor, uint32_t aEffect, float aDuration, uint32_t aCastingType);
    bool FlashEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override
    {
        if (!Actors.contains(aFormId) || !Actors.at(aFormId).Loaded ||
            !HitVisualsRules::Allowed(EffectVisualFlags[aEffectFormId], false))
            return false;
        FlashedVisuals.emplace_back(aFormId, aEffectFormId);
        return true;
    }
    std::vector<ActiveEffectInfo> GetActorActiveEffects(uint32_t aFormId) const noexcept override;
    bool EffectHasHitVisuals(uint32_t) const noexcept override { return true; }
    // Nothing plays on an actor with no 3D (SkyrimBridge: no NiNode, ApplyHitVisuals has nothing to attach to).
    bool PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override
    {
        return Actors.contains(aFormId) && ActorHas3D(aFormId) && Actors.at(aFormId).ShownVisuals.insert(aEffectFormId).second;
    }
    void StopEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override
    {
        if (const auto it = Actors.find(aFormId); it != Actors.end())
            it->second.ShownVisuals.erase(aEffectFormId);
    }
    bool IsPlayingEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) const noexcept override
    {
        return Actors.contains(aFormId) && Actors.at(aFormId).ShownVisuals.contains(aEffectFormId);
    }
    // The player's map marker: placed in the map by the player (our hook on SetWaypoint raises it), or another
    // player's applied through the engine's own call (no event).
    struct Marker
    {
        glm::vec3 Position{};
        uint32_t WorldSpace = 0;
    };
    std::optional<Marker> Waypoint;
    void PlacesMarker(const glm::vec3& acPosition, uint32_t aWorldSpace);
    void RemovesMarker();
    void SetPlayerWaypoint(float aX, float aY, float aZ, uint32_t aWorldSpaceFormId) noexcept override
    {
        Waypoint = Marker{{aX, aY, aZ}, aWorldSpaceFormId};
    }
    void RemovePlayerWaypoint() noexcept override { Waypoint.reset(); }
    PartyMarkerSet PartyMarkers;
    void SetPartyMarkers(const PartyMarkerSet& acMarkers) noexcept override { PartyMarkers = acMarkers; }
    // The clock's speed: this game's own wish (a kill cam, raised by our BSTimer hook) and the party's, the slower
    // running (BSTimer::SetSharedMultiplier).
    float WantedTimeScale = 1.f;
    float SharedTimeScale = 1.f;
    void SlowsTime(float aMultiplier);
    [[nodiscard]] float TimeMultiplier() const noexcept { return std::min(WantedTimeScale, SharedTimeScale); }
    void SetSharedTimeScale(float aMultiplier) noexcept override { SharedTimeScale = aMultiplier; }
    // NPC bases that go down instead of dying, even at the console's kill (measured: Lydia, owner and copy).
    std::set<uint32_t> EssentialBases{0x000A2C8E};

    // A worldspace's exterior cells: (x, y) for |x|, |y| <= aRadius, 4096 units each (GridCellCoords), form ids
    // from aFirstCell on.
    void Exterior(uint32_t aWorldSpace, uint32_t aFirstCell, int32_t aRadius);
    struct ExteriorCell
    {
        uint32_t WorldSpace = 0;
        int32_t X = 0;
        int32_t Y = 0;
    };
    std::map<uint32_t, ExteriorCell> Exteriors; // by cell form id
    [[nodiscard]] uint32_t CellAt(uint32_t aWorldSpace, const glm::vec3& acPosition) const noexcept;
    uint32_t LookupCellByCoords(uint32_t aWorldSpaceFormId, int32_t aX, int32_t aY) const noexcept override;
    // This game's player stands at acPosition in aCell (a save loaded there), or walks to acPosition outdoors: the
    // loaded cells follow (outdoors the 5 by 5 grid around its cell), no teleport.
    void PlayerStands(uint32_t aCell, const glm::vec3& acPosition);
    void PlayerWalks(const glm::vec3& acPosition);

    // An NPC this game's cell loads (as `placeatme`: a temporary reference of aBase); its form id.
    uint32_t LoadNpc(uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition = {});
    // An NPC placed in the plugins (the same form id in every game), in aCell at acPosition.
    void PlaceNpc(uint32_t aFormId, uint32_t aBase, uint32_t aCell, const glm::vec3& acPosition);
    // A reference still loading finishes: kStillLoading clears, then its 3D loads (TESObjectLoadedEvent).
    void FinishLoading(uint32_t aFormId);
    // Each game's engine runs its NPCs' AI on its own clock (Skyrim 2026-11-09, actor-ai-and-copies.md, "A guest's own
    // NPCs run"): its package walks it to acPosition in aCell (UpdateAI; unloaded, the virtual path follower), its 3D
    // loading as it enters a loaded cell. False: its AI is cut (a copy, or held on a guest).
    bool NpcWalks(uint32_t aFormId, uint32_t aCell, const glm::vec3& acPosition);
    // Load3D (19727) for an actor in a cell loaded here: a cell attaching, an NPC walking in, a load finishing. Held:
    // refused, as a disabled reference's.
    void Load3D(uint32_t aFormId);
    // A hit of aDamage on an actor, as the engine's: at 0 health it dies, or goes down if essential; a copy is in
    // deferred kill and does neither (actor-ai-and-copies.md, "A copy is in deferred kill").
    void Hit(uint32_t aFormId, float aDamage);
    // A made actor's 3D loads at once; false: later, when the test sets its Loaded (Skyrim loads it on a worker).
    bool MadeActors3D = true;
    // How many actors of each NPC base this game has made (SpawnActorFromNpc), ever.
    std::map<uint32_t, size_t> Spawned;
    // The actors that are copies of aBase here (0 for any base), by form id.
    [[nodiscard]] std::vector<uint32_t> CopiesOf(uint32_t aBase = 0) const;

    // IActorBridge, IPlayerBridge: the actor model's.
    std::vector<uint32_t> GetLoadedActorFormIds() const noexcept override;
    bool ActorHas3D(uint32_t aFormId) const noexcept override;
    // AIProcess::equippedObjects as the fake has them: the hand spells (weapons not modelled).
    uint32_t GetEquippedWeaponFormId(uint32_t aActorFormId, uint32_t aSlot) const noexcept override;
    // EquipSpell (38928) for each hand: the spell selected, and held only with a process (3D here).
    void EquipSpells(uint32_t aFormId, const MagicEquipment& acMagic);
    // Actor::Set3D(null) (37178) on an NPC: RevertSelectedSpell (38768) clears each hand, selected and held.
    void RevertSelectedSpells(uint32_t aFormId);
    // Its 3D unloads where it stands (Loaded off, hands cleared, head gone); Loaded = true loads it again.
    void Unload3D(uint32_t aFormId);
    // Its head goes with its 3D: the next is built again from its base (TESNPC::BuildHead 24731), unpainted.
    void DropHead(uint32_t aFormId);
    // Each actor whose 3D is there and has no head gets one, raised as the hook on 24724 does.
    void BuildHeads();
    bool IsActorFacePainted(uint32_t aFormId) const noexcept override;
    bool PaintActorFace(uint32_t aFormId, const Tints& acTints) noexcept override;
    // A copy's skeleton is posed by its ragdoll: lying knocked down, or dead with the owner's ragdoll held.
    [[nodiscard]] bool RagdollPosed(uint32_t aFormId) const noexcept;

    bool ActorExists(uint32_t aFormId) const noexcept override { return Actors.contains(aFormId); }
    void SetActorCopy(uint32_t aFormId, bool aCopy) noexcept override;
    bool IsActorCopy(uint32_t aFormId) const noexcept override;
    void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept override;
    uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept override;
    bool SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept override;
    std::string GetActorDisplayName(uint32_t aFormId) const noexcept override;
    void SetActorDisplayName(uint32_t aFormId, const std::string& acName) noexcept override;
    // Base 0x7's name, as the engine's GetFormName reads it: the character's.
    std::string GetFormName(uint32_t aFormId) const noexcept override;
    [[nodiscard]] std::string PlayerCharacterName() const noexcept { return Player.Name.empty() ? Name : Player.Name; }
    float GetActorBaseWeight(uint32_t aFormId) const noexcept override;
    void SetActorBaseLevel(uint32_t aFormId, uint16_t aLevel) noexcept override
    {
        if (const auto it = Npcs.find(GetActorBaseFormId(aFormId)); it != Npcs.end())
            it->second.Level = aLevel;
    }
    uint32_t GetActorRaceFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActorParentCellFormId(uint32_t aFormId) const noexcept override;
    uint32_t GetActorWorldSpaceFormId(uint32_t aFormId) const noexcept override;
    void GetActorPosition(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    // Dead with a ragdoll, or its ragdoll's bodies awake (knocked down): as Actor::LiesInRagdoll.
    bool IsLyingInRagdoll(uint32_t aFormId) const noexcept override;
    void GetActorRotation(uint32_t aFormId, float& aX, float& aY, float& aZ) const noexcept override;
    bool IsActorDead(uint32_t aFormId) const noexcept override;
    void KillActor(uint32_t aFormId) noexcept override;
    void RespawnActor(uint32_t aFormId) noexcept override;
    void ResurrectActor(uint32_t aFormId) noexcept override;
    void ResumeActorAnimation(uint32_t aFormId) noexcept override
    {
        MainLoopTasks::Queue([this, aFormId] {
            if (auto it = Actors.find(aFormId); it != Actors.end() && !it->second.Dead)
            {
                it->second.GraphUpdating = true;
                it->second.GraphState.clear();
            }
        });
    }
    bool DamageActorHealth(uint32_t aFormId, uint32_t aAttackerFormId, float aDamage) noexcept override;
    bool IsBleedingOut(uint32_t aFormId) const noexcept override;
    bool IsActorBleedingOut(uint32_t aFormId) const noexcept override { return IsBleedingOut(aFormId); }
    // The dying player's ragdoll comes to rest this many frames after its death (TESDeathEvent, dead).
    static constexpr uint32_t kDyingFrames = 30;
    // fPlayerDeathReloadTime (5 s): a dead player not held loads the last save then (Reloaded).
    static constexpr uint32_t kDeathReloadFrames = 300;
    bool DeathReloadHeld = false;
    bool Reloaded = false;
    bool IsOtherPlayer(uint32_t aFormId) const noexcept override;
    bool IsTemporaryActor(uint32_t aFormId) const noexcept override { return IsActorTemporary(aFormId); }
    bool SetActorPersistent(uint32_t aFormId, bool aPersistent = true) noexcept override;
    void SetActorPlayerFaction(uint32_t aFormId) noexcept override;
    bool ApplyRemoteMovement(uint32_t aFormId, const float aPosition[3], const float aRotation[3], float aDirection,
                             const AnimationVariables& aVariables, CopyMove aMove) noexcept override;
    float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorTemporaryValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept override;
    void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept override;
    int32_t GetActorValueCount() const noexcept override { return 164; } // ActorValueInfo::kActorValueCount
    bool IsActorDisabled(uint32_t aFormId) const noexcept override;
    bool IsActorStillLoading(uint32_t aFormId) const noexcept override;
    bool IsActorDeleted(uint32_t aFormId) const noexcept override;
    void EnableActor(uint32_t aFormId) noexcept override;
    // Enable 19800 resets an actor's inventory before it has a process: each worn outfit item goes through
    // RemoveItem 16059's no-process unequip (19694). A worn list 16059 expects to survive that the unequip frees is
    // freed again unless 19694 says it is gone (inventory-changes.md, "the list freed twice").
    uint32_t WornListDoubleFrees = 0;
    // Worn lists an enable's reset unequipped that merged into an equal twin (freed), whoever answered for them.
    uint32_t WornListsMerged = 0;
    void DisableActor(uint32_t aFormId, bool aDelete) noexcept override;
    void SetActorHidden(uint32_t aFormId, bool aHidden) noexcept override;
    // An enable parent made from its file or a save (InitItemImpl, InitLoadGame): 20271 sets each child's flag raw
    // (TESForm::SetDisabled 14646, no Enable), which the plugin refuses to clear for a hidden copy.
    void EnableParentLoads(uint32_t aParent, bool aDisabled);
    // A quest's Delete with no Disable: a placed reference loses its process (UpdateTempChangeList), and nothing ends
    // its conversation (Skyrim 2026-11-06, dialogue.md).
    void MarkForDelete(uint32_t aFormId);
    uint32_t SpawnActorFromNpc(uint32_t aNpcFormId) noexcept override;
    uint32_t CreateCustomActor(const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept override;
    // The player's base save data is its record's (Player.Base, BaseFlags), its tints too.
    uint64_t GetAnimationGraphDescriptorHash(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() ? it->second.GraphHash : 0;
    }
    void SetAnimationGraphDescriptorHash(uint32_t aFormId, uint64_t aHash) noexcept override
    {
        if (const auto it = Actors.find(aFormId); it != Actors.end())
            it->second.GraphHash = aHash;
    }
    bool BlockAppearance = false;
    mutable std::vector<uint32_t> WorkerCellLoads;
    std::vector<uint32_t> WorkerQuestWrites;
    bool AtMainLoop = false;
    // The engine's frame length (GetRealFrameSeconds), steady while our update lands unevenly in it (S10);
    // 0: the wall time since our last update.
    double FrameMs = 0.0;
    // With FrameMs: the engine's own time, its frames summed from the session's time when set; Havok steps by it.
    double EngineMs = 0.0;
    [[nodiscard]] uint64_t PhysicsTime(uint64_t aSessionTick) const noexcept { return FrameMs > 0.0 ? static_cast<uint64_t>(EngineMs) : aSessionTick; }
    uint64_t LastFrameTick = 0; // Frame's previous frame tick: the graphs' step without FrameMs
    std::deque<std::function<void()>> MainTasks;
    uint32_t AppearanceAttempts = 0;
    bool ApplyActorLooks(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept override;
    uint32_t GetActorNpcTemplateBaseFormId(uint32_t aFormId) const noexcept override;
    uint32_t SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept override;
    void LoadBase(uint32_t aBase, const std::string& acAppearance, uint32_t aChangeFlags);
    void MarkActorNpcChanged(uint32_t aFormId, uint32_t aFlags) noexcept override;
    uint32_t GetPlayerRaceFormId() const noexcept override { return GetActorRaceFormId(kPlayer); }
    Tints GetPlayerFaceTints() const noexcept override { return Player.FaceTints; }
    void MoveActorTo(uint32_t aFormId, uint32_t aCellId, float aX, float aY, float aZ) noexcept override;
    void SetActorRotation(uint32_t aFormId, float aX, float aZ) noexcept override;
    void DeleteActor(uint32_t aFormId) noexcept override;
    bool IsActorTemporary(uint32_t aFormId) const noexcept override { return aFormId >= 0xFF000000; }
    bool IsActorNpcTemporary(uint32_t aFormId) const noexcept override;
    Inventory GetActorInventory(uint32_t aFormId) const noexcept override;
    void SetActorInventory(uint32_t aFormId, const Inventory& acInventory) noexcept override;
    void SetActorValues(uint32_t aFormId, const ActorValues& acValues) noexcept override;
    bool OpenFollowerInventory(uint32_t aNpcFormId) noexcept override;
    std::vector<Followers::State> PartyFollowers;
    std::vector<Followers::Order> FollowerOrders;
    std::vector<Followers::Target> FollowerTargets;
    uint32_t FollowerTradeNpc{};
    uint32_t GetPlayerFormId() const noexcept override { return kPlayer; }
    uint32_t GetPlayerParentCellId() const noexcept override { return Actors.at(kPlayer).Cell; }
    uint32_t GetPlayerParentCellFormId() const noexcept override { return Actors.at(kPlayer).Cell; }
    uint32_t GetPlayerWorldSpaceId() const noexcept override { return Actors.at(kPlayer).WorldSpace; }
    bool IsPlayerLoaded() const noexcept override { return true; }
    void GetPlayerPosition(float& aX, float& aY, float& aZ) const noexcept override { GetActorPosition(kPlayer, aX, aY, aZ); }
    bool ConsumeTeleported() noexcept override { return std::exchange(Teleported, false); }
    // The actors passing through each other (PlayerCollision's collision filter).
    std::vector<uint32_t> PassingThrough;
    void SetPassThrough(const std::vector<uint32_t>& acActorFormIds) noexcept override { PassingThrough = acActorFormIds; }
    // A stepped NPC copy overlapping the player shoves it out unless they pass through (actor-movement.md).
    void ShovePlayer() noexcept;
    uint32_t PlayerShoves = 0;
    float GetPlayerHeading() const noexcept override { return Heading; }
    void SetPlayerHeading(float aHeading) noexcept override { Heading = aHeading; }
    bool IsPlayerMounted() const noexcept override { return Mounts.contains(kPlayer); }
    void PlayerStopRiding() noexcept override { StopRiding(kPlayer); }
    bool PlayerSitOnMountQuick(uint32_t aMount) noexcept override;
    void PlayerTakeReins(uint32_t aMount) noexcept override
    {
        ActorEndMountPair(aMount); // SkyrimBridge::PlayerTakeReins sends PairEnd too
        MountSlots[aMount] = kPlayer;
        Reins[aMount] = kPlayer;
    }
    bool IsPlayerAlive() const noexcept override { return !Actors.at(kPlayer).Dead; }
    float GetPlayerActorValue(int32_t aIndex) const noexcept override { return GetActorValue(kPlayer, aIndex); }
    void ForcePlayerActorValue(int32_t aForceMode, int32_t aIndex, float aValue) noexcept override
    {
        ForceActorValue(kPlayer, aForceMode, aIndex, aValue);
    }
    uint32_t GetDifficulty() const noexcept override { return Difficulty; }
    void SetDifficulty(uint32_t aDifficulty) noexcept override { Difficulty = aDifficulty; }
    float GetGreetDistance() const noexcept override { return GreetDistance; }
    void SetGreetDistance(float aDistance) noexcept override { GreetDistance = aDistance; }
    void HoldPlayerDeathReload(bool aHold) noexcept override { DeathReloadHeld = aHold; }
    void RevivePlayer() noexcept override;
    void EndPlayerRagdoll() noexcept override { QueuedRagdollEnds.push_back(kPlayer); }
    // The engine's task pool's ragdoll removals (TaskQueueInterface 36924): run after the step, before the next listing.
    std::vector<uint32_t> QueuedRagdollEnds;
    // Its ragdoll out of the world at once, in our update (63622, as the revive did): the step's pass puts it back.
    void RemoveRagdollAtOnce(uint32_t aFormId) { Actors.at(aFormId).RagdollInWorld = false; }
    // Where its skeleton's pelvis stands above its feet: its animation's ~71, or on its ragdoll's bodies while a
    // living actor's are in the world (58293 with the standing graph's rigid-body controls).
    [[nodiscard]] float PelvisAboveFeet(uint32_t aFormId) const;
    // A frame's ragdoll phases, at its start: the step for last frame's listed graphs, the task pool, the deaths' adds,
    // the next listing.
    void StepDeathRagdolls();
    uint32_t CameraTarget = kPlayer;
    uint32_t DeathControlsBefore{};
    bool DeathControlsHeld{};
    void SetSpectatorTarget(uint32_t aFormId) noexcept override { CameraTarget = aFormId ? aFormId : kPlayer; }
    bool FirstPerson{}; // the camera's view: in its target's head (PlayerCamera::KeepSpectating)
    void KeepSpectatorView() noexcept override
    {
        if (CameraTarget != kPlayer)
            FirstPerson = false;
    }
    void HoldDeathControls(bool aHold) noexcept override;
    bool IsScriptCameraHeld() const noexcept override { return (Holds.HeldExcept(0xFFFFFFFE) & PartyRules::kMovement) != 0; }

    // Equipment. An item's form: a weapon held in a hand, or an armor and the biped slots it covers.
    struct ItemForm
    {
        uint8_t Type = 0;   // FormType: kWeapon or kArmor
        uint32_t Slots = 0; // an armor's (BGSBipedObjectForm)
    };
    static constexpr uint8_t kWeapon = 41;
    static constexpr uint8_t kLight = 31;
    static constexpr uint8_t kArmor = 26;
    static constexpr uint8_t kLeveledItem = 53;
    static constexpr uint32_t kRightHand = 0x13F42; // DefaultObjectManager's equip slots
    static constexpr uint32_t kLeftHand = 0x13F43;
    // Each NPC base's container entries (TESContainer): only its leveled lists (form type kLeveledItem) are modelled.
    // As Skyrim showed them (inventory_entry, 2026-11-19): counted with the actor's own change of them (16108),
    // listed by TESObjectREFR::GetInventory as they are, and left by RemoveAllItems (SetActorInventory).
    std::map<uint32_t, std::map<uint32_t, int32_t>> BaseContainers;
    std::map<uint32_t, ItemForm> ItemForms{{0x00012EB7, {kWeapon}},          // Iron Sword
                                           {0x00013989, {kWeapon}},          // Steel Sword
                                           {0x0001397E, {kWeapon}},          // Iron Dagger
                                           {0x00012E49, {kArmor, 0x4}},      // Iron Armor (body)
                                           {0x00013952, {kArmor, 0x4}},      // Steel Armor (body)
                                           {0x00012E4D, {kArmor, 0x1 | 0x2}}}; // Iron Helmet (head, hair)
    // The actor equips or unequips by itself (the player's menu, an NPC's AI), as EquipManager::Equip (38894) and
    // UnEquip (38901) through our hooks: a local actor's change is raised (EquipmentChangeEvent), a copy refuses it.
    // Equip does nothing for an item a copy does not carry.
    void RebuildCopyEquipment(uint32_t aActor, uint32_t aRemainingItem);
    void Equips(uint32_t aActor, uint32_t aItem, uint32_t aSlot = kRightHand);
    void Unequips(uint32_t aActor, uint32_t aItem, uint32_t aSlot = kRightHand);
    // The actor draws or sheathes by itself; ActorState's weapon state, the animation not modelled.
    void Draws(uint32_t aActor, bool aDraw) { Actors.at(aActor).Weapon = aDraw ? WeaponState::Drawn : WeaponState::Sheathed; }
    WeaponState GetWeaponState(uint32_t aFormId) const noexcept override;
    ActorStateFields GetActorStateFields(uint32_t aFormId) const noexcept override;
    // Actor::GetFactions / SetFactions over Factions (Skyrim.esm factions: mod 0).
    ::Factions GetActorFactions(uint32_t aFormId) const noexcept override;
    void SetActorFactions(uint32_t aFormId, const ::Factions& acFactions) noexcept override;
    void SetCopyActorStateFlags(uint32_t aFormId, uint32_t aFlags1, uint32_t aFlags2) noexcept override;
    void SetCopyNoise(uint32_t aFormId, int32_t aNoise) noexcept override;
    // Actor::SetActionValue (37711) as its engine calls it (a swing, a cast, the player's bump), with HookSetActionValue.
    void SetActionValue(uint32_t aFormId, int32_t aNoise) noexcept;
    void CopyEnterKillMove(uint32_t aFormId, uint32_t aKillerFormId) noexcept override;
    void CopyLeaveKillMove(uint32_t aFormId) noexcept override;
    void ActorHoldKill(uint32_t aFormId, bool aHold) noexcept override;
    void KillActorBy(uint32_t aFormId, uint32_t aKillerFormId) noexcept override;
    void ActorYieldPair(uint32_t aOwnFormId, uint32_t aCopyFormId) noexcept override;
    bool ActorPerformPlainly(uint32_t aFormId, uint32_t aActionId) noexcept override;
    [[nodiscard]] bool ActorCanPair(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && !it->second.Dead && it->second.ProcessMe && it->second.Commander != kPlayer;
    }
    [[nodiscard]] bool ActorKillCounted(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && it->second.KillCounted;
    }
    [[nodiscard]] bool ActorKillQueued(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && it->second.KillQueued;
    }
    void SetActorCommandingActor(uint32_t aFormId, uint32_t aCommanderFormId) noexcept override
    {
        if (const auto it = Actors.find(aFormId); it != Actors.end())
            it->second.Commander = aCommanderFormId;
    }
    uint32_t GetActorCommandingActorFormId(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() ? it->second.Commander : 0;
    }
    [[nodiscard]] bool IsPlayerSummon(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        return it != Actors.end() && it->second.Commander == kPlayer;
    }
    // Actor::SetWeaponDrawnEx: 37279 takes the draw (state 2 or 5), its own end comes WeaponClipFrames later; refused
    // on a copy on its owner's stream (1 or 4).
    uint32_t WeaponClipFrames = 4;
    void SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept override;
    // Actor::ShowWeaponDrawn: the weaponDraw or weaponSheathe event's result (42824, 37169), no action.
    void ShowActorWeaponDrawn(uint32_t aFormId, bool aDrawn) noexcept override;
    // Items. The actor gets or loses some by itself (a script's AddItem, a sale, eating), as our hooks on
    // AddInventoryItem and RemoveItem: raised (InventoryChangeEvent) unless under ScopedInventoryOverride; the last of
    // a worn item comes off first (through UnEquipHook).
    void GetsItem(uint32_t aActor, uint32_t aItem, int32_t aCount);
    // One with extra data (an enchantment, tempering...), its own stack.
    void GetsItem(uint32_t aActor, const Inventory::Entry& acItem);
    void LosesItem(uint32_t aActor, uint32_t aItem, int32_t aCount);
    // A smithing or enchanting table improves one of the stack in place (InventoryChanges 16145 temper, 16146
    // EnchantObject): split off a counted stack and changed, no add or remove for our item hooks to see.
    void Improves(uint32_t aActor, const Inventory::Entry& acStack, const std::function<void(Inventory::Entry&)>& acChange);
    // A quest's script gives it (AddItem from a quest, alias, dialogue line or scene): raised as the story's.
    void QuestGivesItem(uint32_t aActor, uint32_t aItem, int32_t aCount);
    // The engine puts an NPC's outfit item back on after removeallitems: added with no InventoryChangeEvent (our item
    // hooks never see it), then equipped as Equips does (save-load-gate, 2026-10-30: Lydia's copy lost her armor).
    void OutfitPutsOn(uint32_t aActor, uint32_t aItem);
    // Dropped into the world, as HookDropObject: raised as a drop, the removal under ScopedInventoryOverride.
    void Drops(uint32_t aActor, uint32_t aItem, int32_t aCount);
    [[nodiscard]] int32_t CountOf(uint32_t aActor, uint32_t aItem) const;
    // Every item the actor carries (server ids) and how many.
    [[nodiscard]] std::map<std::pair<uint32_t, uint32_t>, int32_t> Carried(uint32_t aActor) const;
    // TESObjectREFR::AddOrRemoveItem: another game's change (InventoryService, under ScopedInventoryOverride).
    void ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept override;
    void AddOrRemoveStack(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept;
    void SetCopyItemCount(uint32_t aActorFormId, const GameId& acItem, int32_t aCount, const std::vector<Inventory::Entry>& acStacks) noexcept override;
    void SetCarriedStacks(uint32_t aActorFormId, const GameId& acItem, int32_t aCount, const std::vector<Inventory::Entry>& acStacks) noexcept;
    // What the actor wears: each worn item (its server id) and whether in the left hand.
    [[nodiscard]] std::set<std::tuple<uint32_t, uint32_t, bool>> Worn(uint32_t aActor) const;
    uint8_t GetFormType(uint32_t aFormId) const noexcept override;
    bool IsRightHandEquipSlot(uint32_t aEquipSlotFormId) const noexcept override { return aEquipSlotFormId == kRightHand; }
    Inventory GetActorEquipment(uint32_t aActorFormId) const noexcept override;
    Inventory GetActorWornArmor(uint32_t aActorFormId) const noexcept override;
    // EquipManager's, ours: under ScopedEquipOverride, as EquipManager::Equip and UnEquip set it.
    void ActorResetEquipment(uint32_t aActorFormId, const MagicEquipment& acMagic) noexcept override;
    void ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t aCount) noexcept override;
    void ActorEquipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept override;
    void ActorUnequipShout(uint32_t aActorFormId, uint32_t aShoutFormId) noexcept override;
    void ActorUnequipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t aCount) noexcept override;

    [[nodiscard]] bool Has(uint32_t aFlags) const noexcept
    {
        return (Controls & aFlags) == aFlags;
    }

    // The engine of the game World::Get() is now.
    static FakeEngine& Current() noexcept;

  private:
    uint32_t m_nextStack = 1; // the stacks this game's boxes wait in
    uint32_t m_openedBox = 0; // the box the running script opened (Message.Show)
    bool m_unannouncedMove = false; // PlayerTravelsUnannounced
    bool m_mirroredMove = false; // the player's move is another game's party call's: party travel leads no one with it
    // A new temporary form id: the lowest a deleted reference gave back, as the engine reuses them, else the next.
    uint32_t NewTemporary() noexcept;

  public:
    // The next temporary form id this game makes (two games' ids meeting: each numbers its own).
    void NextTemporaryIs(uint32_t aFormId) noexcept { m_nextTemporary = aFormId; }

  private:
    // At 0 health: dead, or down if essential; a copy is in deferred kill.
    void CheckHealth(uint32_t aFormId, Actor& aActor) noexcept;
    // The engine's equip and unequip behind EquipHook and UnEquipHook (EquipManager.cpp).
    void Equip(uint32_t aActor, uint32_t aItem, uint32_t aSlot);
    void Unequip(uint32_t aActor, uint32_t aItem, uint32_t aSlot);
    [[nodiscard]] SpellForm Spell(uint32_t aFormId) const noexcept
    {
        const auto it = SpellForms.find(aFormId);
        return it != SpellForms.end() ? it->second : SpellForm{};
    }
    // The actor's inventory entry of aItem (a form id), added with a count of 1 if it has none.
    Inventory::Entry& EntryOf(Actor& aActor, uint32_t aItem);
    // aCount more (or fewer) of the item; an entry at 0 goes, a worn one unequipped first.
    void AddItems(uint32_t aActor, uint32_t aItem, int32_t aCount);
    // An entry with extra data is its own stack (merged only with one alike), as the engine's extra data lists are.
    void AddEntry(uint32_t aActor, const Inventory::Entry& acItem);
    // The host's record of a loose item it made or took (kCreate, the deletion).
    void RecordCreated(uint32_t aObject);

    struct QueuedCall
    {
        LocalCall Call;
        uint32_t Cause;
        std::function<void(LocalValue)> OnReturn;
    };
    std::deque<QueuedCall> m_queued;
    RagdollPending m_pendingRagdolls;
    // A copy's ragdoll's frames from its owner, after its graph's driveToPose (as CopyRagdollPhysics).
    void DriveCopyRagdoll(uint32_t aCopy, const std::vector<RagdollPending::Drive>& acDrives) noexcept;
    [[nodiscard]] BodyKey RagdollKey(const BodyKey& acKey, uint32_t aActor) const noexcept;
    std::map<uint32_t, RagdollRest> m_copyRests;
    uint64_t m_frameCount = 0; // frames run: the fake's 60 a second
    bool m_inSystemQuest = false; // a system quest's function runs (CallsQuest): its natives are its instance's
    std::chrono::steady_clock::time_point m_lastFrame;
    // An actor's cell changed from aFrom: this game's player in another location raises CLOC.
    void CellChanged(uint32_t aFormId, uint32_t aFrom);
    // The cells loaded around the player at aCell and acPosition (an interior, or outdoors its grid); the others its
    // last place loaded unload.
    void LoadAroundPlayer(uint32_t aFrom);
    std::set<uint32_t> m_playerCells;
    uint32_t m_nextTemporary = 0xFF000800;
    uint32_t m_made = 0;
    std::set<uint32_t> m_freedTemporary;
};

// Game.DisablePlayerControls / EnablePlayerControls with Papyrus' defaults for the rest: movement, fighting, camera
// switch, looking, sneaking, menu, activate, journal tabs, POV type.
LocalCall DisablePlayerControls(bool aMovement = true, bool aFighting = true, bool aCamSwitch = false,
                                bool aLooking = false, bool aSneaking = false, bool aMenu = true,
                                bool aActivate = true);
LocalCall EnablePlayerControls(bool aMovement = true, bool aFighting = true, bool aCamSwitch = true,
                               bool aLooking = true, bool aSneaking = true, bool aMenu = true, bool aActivate = true);

// Session ids of this game's form ids, one load order (FormIds.h's ToSession).
[[nodiscard]] GameId SessionIdOf(uint32_t aFormId) noexcept;
[[nodiscard]] uint32_t FormIdOf(const GameId& acId) noexcept;

// The process holds several games: World::Get() is this one while it runs (Party::As).
void SetCurrentWorld(World* apWorld) noexcept;
