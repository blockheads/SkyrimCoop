#pragma once
#include <Followers/State.h>

// One game's engine in tier 2 (docs/design/testing.md): the player's controls (ControlMap's bitmask), a script VM
// running Game.DisablePlayerControls / EnablePlayerControls through what PartyNatives' glue does around them, the
// world's references (locks, containers' items) with the change records the engine keeps, and the actors (the
// player, placed and placeatme NPCs, copies: where, health, death, bleedout, enabled).
#include "StillBridges.h"
#include "../FakeFlight.h"

#include <Bridges/IJoinBridge.h>
#include <Bridges/IBodyBridge.h>
#include <Physics/RagdollPending.h>

#include <Games/QuestChanges.h>
#include <Games/HitVisualsRules.h>
#include <MainLoopTasks.h>
#include <Games/Skyrim/HitRules.h>
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

struct FakeEngine : StillBridges, IJoinBridge, IBodyBridge
{
    static constexpr uint32_t kAllControls = 0xFFFFFFFF;

    uint32_t Controls = kAllControls; // ControlMap::enabledControls
    float ShakeSeconds = 0.f;         // the camera's shake left (Game.ShakeCamera), a frame a 60th of a second
    // A script's calls on a body (cutscenes.md step 2): the idles each actor played (Actor.PlayIdle), and whether the
    // player is AI-driven (Game.SetPlayerAIDriven). Its moves are travels (ObjectReference.MoveTo, MoveActorTo).
    std::map<uint32_t, std::vector<uint32_t>> Idles;
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
    float GreetDistance = 150.f;
    // The join flow's engine (IJoinBridge). A save carries this game's player as a record and the co-save; loading one
    // makes the player the save's (the host's) until JoinService applies the joining player's own.
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
    bool WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept override;
    // The engine's main loop between frames, before our update (HookMainLoop's MainLoopTasks).
    void RunMainLoop() noexcept;
    std::optional<SaveFiles> ReadSave(const std::string& acName) noexcept override;
    std::optional<SaveFiles> PreviousSave() noexcept override;
    std::map<std::string, SaveFiles> SavedFiles;
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
    [[nodiscard]] LocalValue Answer(const LocalCall& acCall, bool aAsksParty) const;
    // This game's player answers the box in aStack: its stack returns (ReturnFromLatent's hook: an original's result
    // goes to its copies, CallEndedEvent).
    void AnswerBox(uint32_t aStack, int32_t aButton);
    // A call the VM runs on its next frame, its stack tagged aCause (GlobalCall::QueueCall): a quest's function
    // (QuestFunctions) or a native.
    void Queue(LocalCall aCall, uint32_t aCause);
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
    // One frame: the shake wears off, knocked-down ragdolls come to rest, the VM runs what is queued; copies are drawn,
    // then their graphs update (one lying in its ragdoll put on its root body, as the engine keeps any actor).
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
        bool Open = false; // a door's (BGSOpenCloseForm's open state)
        glm::vec3 Position{}; // where it stands (a marker MoveTo goes to)
        uint32_t RecordFlags{};
        bool operator==(const Reference&) const = default;
    };
    std::map<uint32_t, Reference> References;
    std::set<uint32_t> PendingPlacements; // controlled by shared-rest reconciliation tests
    std::set<uint32_t> Attached;       // the cells loaded here
    std::set<uint32_t> InMemory;       // the cells loaded here once: their references stay in memory
    std::set<uint32_t> Changed;        // the references this game's change records hold
    std::vector<ObjectChange> Recorded; // the host's: its world's changes since HostGame::TakeObjectChanges
    bool IsGuest = false;              // a guest's engine raises its player's changes as events (GuestWorldState)
    bool DecidesWorld = true;          // GuestEngine::SetDecidesWorld

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
    // The host's game makes a loose item where the placement says (HostGame::CreateItem): none while that cell is not
    // loaded here, an empty id for a missing form. And takes one out of its world (HostGame::TakeItem).
    std::optional<GameId> CreateLoose(const Inventory::Entry& acItem, uint32_t aCell);
    std::optional<Inventory::Entry> TakeLoose(const GameId& acObject);
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
        bool Held = false;    // keyframed by the host's frames (a guest's; a reference's freed at its rest), or by a guest's hold (the host's)
        size_t Drives = 0;    // frames of the host's followed
        size_t Rests = 0;     // rests of the host's placed
        uint64_t Tick = 0;    // the host's: where its flight is
        uint32_t RagdollOf = 0; // an actor's ragdoll body: its animation drives it while the actor lives
    };
    std::map<BodyKey, Body> Bodies;
    // A ragdolled actor's heading as this engine turns it with its root body: where the body's y axis points, about z.
    static float HeadingOf(const glm::quat& acRotation) noexcept;
    // The host's player knocks it: it wakes and flies off at that velocity.
    void Knock(const BodyKey& acKey, const glm::vec3& acVelocity, const glm::vec3& acSpin = {});
    // This game's physics steps up to aTick (HostGame::TakeBodies, a guest's GuestEngine::TakeOwnBodies): the awake
    // bodies, and those that fell asleep; acWhich: only those (a guest's player's ragdoll).
    HostBodies TakeBodies(uint64_t aTick, const std::function<bool(const BodyKey&)>& acWhich = {});
    // This game's player's hand (a guest's: GuestEngine::TakePushes, ReadHeld): what its character pushed since the
    // last read (every body a mass of 1), and what it holds. Another game's hand on this game's bodies comes through
    // IBodyBridge (a push, a held body played keyframed, so not awake, then let go).
    std::vector<BodyPush> PlayerPushes;
    std::optional<BodyKey> Grabbed;
    // A guest's own physics moved these (GuestEngine::TakeOwnBodies): its own blast, not the host's.
    std::vector<BodyPose> OwnMoves;
    // IBodyBridge: the bodies other games own, played here as Skyrim's would be (BodyBridge).
    void SetReferences(std::function<uint32_t(const GameId& acId)>) noexcept override {}
    std::optional<BodyPose> ReadPose(const BodyKey& acKey) noexcept override;
    bool Drive(const BodyFrame& acFrame, uint32_t aCopyFormId = 0) noexcept override;
    bool Rest(const BodyPose& acPose, uint32_t aCopyFormId = 0) noexcept override;
    bool Free(const BodyFrame& acFrame) noexcept override;
    bool Push(const BodyPush& acPush) noexcept override;
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
        uint32_t Cell = 0;
        uint32_t WorldSpace = 0;
        float Position[3]{};
        float Rotation[3]{};
        bool Dead = false;
        uint64_t GraphHash = 0;
        bool GraphUpdating = true; // KillImpl clears kShouldAnimGraphUpdate; resurrection leaves it stopped.
        bool KnockedDown = false;  // a living copy lying in its ragdoll with its owner's (BodyBridge::KnockedDown); an
                                   // owner on its ragdoll before it simulates (knock state 2)
        uint8_t KnockIn = 0;       // frames until its queued knock lands (KnockExplosion on the task queue): drawn first
        bool Bleeding = false; // essential and down (ActorState's bleedout)
        bool Disabled = false;
        bool Deleted = false;    // marked deleted (DisableActor's aDelete)
        bool Created = false;    // made by Actor::Create (a copy): deleted with its cell unless persistent
        bool Persistent = false; // kept when its cell unloads (SetActorPersistent)
        bool Essential = false;  // goes down at 0 health instead of dying
        bool Copy = false;   // another game owns it (SetActorCopy)
        bool Player = false; // a player's character, this game's or a copy (SetActorPlayer)
        bool Initializing = false; // TESForm::kStillLoading: no 3D yet, FinishLoading ends it
        bool Loaded = true;  // its 3D is loaded (in the process lists)
        uint32_t DyingFrames = 0;  // the player's: dying until its ragdoll comes to rest, then dead
        uint32_t DeadFrames = 0;   // the player's: dying or dead, the reload's countdown (40748)
        WeaponState Weapon = WeaponState::Sheathed;
        uint32_t KilledBy = 0;            // the attacker of the hit that killed it (the engine's kill credit)
        ActorAiFields Ai;                 // package, scene, combat (a copy's set by AiStateService)
        std::set<uint32_t> Visuals;       // an owner's effects with hit visuals on (burning)
        std::set<uint32_t> ShownVisuals;  // a copy's visuals played for its owner's
        ActorValues Values = FullValues();
        std::map<uint32_t, float> Bases = FullValues().ActorMaxValuesList;
        Inventory Items;
        ActionEvent LatestAnimation;
        std::string GraphState;
        bool BlockResurrect = false;
        uint32_t ResurrectCalls = 0;
        uint32_t ValueWrites = 0;
        uint32_t Made = 0; // which actor at this form id: a reused id is a new one (a fresh ActorExtension)
        // Where it stood at its last graph update (Frame). A skeleton its ragdoll poses, its actor moved after the graph
        // update, is drawn in its old pose at the new place for a frame: a snap.
        float GraphPlace[4]{}; // x, y, z, heading
        uint32_t Snaps = 0;
        float WorstSnap = 0.f; // units: the move, and the turn at a body's reach (kSnapReach)
    };
    static constexpr float kSnapReach = 60.f;
    struct Npc
    {
        std::string Name;
        uint32_t Race = 0;
        float Weight = 50.f; // this game's player's is its record's (Player.Weight)
        std::string Looks;   // a copy's: the save data it was made from (CreateCustomActor)
        uint16_t Level = 1;
    };
    std::map<uint32_t, Npc> Npcs;
    std::map<uint32_t, Actor> Actors{{kPlayer, Actor{.Base = 0x7, .Player = true}}};
    static constexpr uint32_t kPlayer = 0x14;
    mutable std::map<std::pair<uint32_t, int32_t>, uint32_t> ValueReads;
    mutable std::map<std::pair<uint32_t, int32_t>, uint32_t> MaximumReads;
    bool HoldDeathActions = false;
    bool RefuseDeathActions = false;
    void ReplayDeathAction(uint32_t aFormId, const ActionEvent& acAction);
    void FinishDeath(uint32_t aFormId);
    ActionEvent GetActorLatestAnimation(uint32_t aFormId) const noexcept override;
    std::string SaveActorGraphState(uint32_t aFormId) const noexcept override
    { const auto it = Actors.find(aFormId); return it == Actors.end() ? std::string{} : it->second.GraphState; }
    bool LoadActorGraphState(uint32_t aFormId, const std::string& aState) noexcept override
    {
        const auto it = Actors.find(aFormId);
        if (it == Actors.end()) return false;
        it->second.GraphState = aState;
        return true;
    }
    void SetActorLatestAnimation(uint32_t aFormId, const ActionEvent& acAction) noexcept override;

    // This game's player travels by itself (a door, coc): to aCell at aPosition, that cell loaded and the one it
    // left unloaded, a teleport the engine reports once (ConsumeTeleported).
    void PlayerTravels(uint32_t aCell, const glm::vec3& acPosition);
    // The native mount interaction, replayed for copies by CharacterService.
    std::map<uint32_t, uint32_t> Mounts;
    void ActorPutOnMount(uint32_t aRider, uint32_t aMount) noexcept override { Mounts[aRider] = aMount; }
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
    };
    std::map<uint32_t, SpellForm> SpellForms{{0x00012FCD, {.Concentration = true}},                  // Flames
                                             {0x00012FCC, {.Concentration = true, .Healing = true}}, // Healing
                                             {0x0005AD5C, {.Buff = true}},                           // Oakflesh
                                             {0x0001C789, {.Damage = true}}};                        // Firebolt
    // A cast this game's engine made (ActorMagicCaster::SpellCast), and an effect it applied (MagicTarget::AddTarget).
    struct Cast
    {
        uint32_t Caster = 0;
        uint32_t Spell = 0;
        uint32_t Source = 0;
        uint32_t Target = 0;
    };
    std::vector<Cast> Casts;
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
    bool IsConcentrationSpell(uint32_t aFormId) const noexcept override { return Spell(aFormId).Concentration; }
    bool IsHealingSpell(uint32_t aFormId) const noexcept override { return Spell(aFormId).Healing; }
    bool IsBuffSpell(uint32_t aFormId) const noexcept override { return Spell(aFormId).Buff; }
    bool IsMagicItem(uint32_t aFormId) const noexcept override { return SpellForms.contains(aFormId); }
    void CastSpell(uint32_t aActorFormId, uint32_t aSpellFormId, uint32_t aCastingSource,
                   uint32_t aDesiredTargetFormId) noexcept override
    {
        Casts.push_back({aActorFormId, aSpellFormId, aCastingSource, aDesiredTargetFormId});
    }
    void AddMagicTargetEffect(uint32_t aTargetFormId, uint32_t aCasterFormId, uint32_t aSpellFormId,
                              uint32_t aEffectFormId, float aMagnitude, bool aIsDualCasting, bool aApplyHealPerkBonus,
                              bool aApplyStaminaPerkBonus) noexcept override
    {
        Effects.push_back({aTargetFormId, aCasterFormId, aSpellFormId, aEffectFormId});
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
    // The native's own write on aActor, as the engine makes it.
    void TakeProgress(const LocalCall& acCall, uint32_t aActor);
    // Objects: an actor activates one by itself (opens a door, pulls a lever), as our hook on TESObjectREFR::Activate
    // raises it; another game's activation replayed here (ActivateObjectFromActor), recorded.
    void Activates(uint32_t aActor, uint32_t aObject);
    std::vector<std::pair<uint32_t, uint32_t>> Activations; // object, activator
    bool ObjectHasLock(uint32_t aObjectFormId) const noexcept override
    {
        const auto it = References.find(aObjectFormId);
        return it != References.end() && (it->second.Locked || it->second.LockLevel);
    }
    bool ObjectIsLocked(uint32_t aObjectFormId) const noexcept override
    {
        const auto it = References.find(aObjectFormId);
        return it != References.end() && it->second.Locked;
    }
    void ActivateObjectFromActor(uint32_t aObjectFormId, uint32_t aActivatorFormId, uint8_t) noexcept override
    {
        Activations.emplace_back(aObjectFormId, aActivatorFormId);
        ToggleDoor(aObjectFormId);
    }
    // An activated door swings: the host's records it, a guest's opens here only (the host's state follows).
    void ToggleDoor(uint32_t aObject);
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
    void ActorSpeakSound(uint32_t aFormId, const std::string& acFilename) noexcept override
    {
        if (!ActorHas3D(aFormId))
            ++SpokeWithout3D;
        Lines.emplace_back(aFormId, acFilename);
    }
    // AI: the actor fights a target by itself (its combat AI); our hooks raise AiStateChangeEvent (AiEvents.cpp).
    void Fights(uint32_t aActor, uint32_t aTarget);
    ActorAiFields GetActorAiFields(uint32_t aFormId) const noexcept override
    {
        const auto it = Actors.find(aFormId);
        ActorAiFields fields = it != Actors.end() ? it->second.Ai : ActorAiFields{};
        // The rider's native horse has no current package, including when an older copy snapshot just arrived.
        if (it != Actors.end() && !it->second.Copy)
            if (const auto cMount = Mounts.find(kPlayer); cMount != Mounts.end() && cMount->second == aFormId)
                fields.PackageId = 0;
        return fields;
    }
    void SetCopyAiFields(uint32_t aFormId, const ActorAiFields& acFields) noexcept override
    {
        if (const auto it = Actors.find(aFormId); it != Actors.end())
            it->second.Ai = acFields;
    }
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
    bool PlayEffectHitVisuals(uint32_t aFormId, uint32_t aEffectFormId) noexcept override
    {
        return Actors.contains(aFormId) && Actors.at(aFormId).ShownVisuals.insert(aEffectFormId).second;
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
    // A hit of aDamage on an actor, as the engine's: at 0 health it dies, or goes down if essential; a copy is in
    // deferred kill and does neither (actor-ai-and-copies.md, "A copy is in deferred kill").
    void Hit(uint32_t aFormId, float aDamage);
    // How many actors of each NPC base this game has made (SpawnActorFromNpc), ever.
    std::map<uint32_t, size_t> Spawned;
    // The actors that are copies of aBase here (0 for any base), by form id.
    [[nodiscard]] std::vector<uint32_t> CopiesOf(uint32_t aBase = 0) const;

    // IActorBridge, IPlayerBridge: the actor model's.
    std::vector<uint32_t> GetLoadedActorFormIds() const noexcept override;
    bool ActorHas3D(uint32_t aFormId) const noexcept override;
    // A copy's skeleton is posed by its ragdoll: lying knocked down, or dead with the owner's ragdoll held.
    [[nodiscard]] bool RagdollPosed(uint32_t aFormId) const noexcept;

    bool ActorExists(uint32_t aFormId) const noexcept override { return Actors.contains(aFormId); }
    void SetActorCopy(uint32_t aFormId, bool aCopy) noexcept override;
    bool IsActorCopy(uint32_t aFormId) const noexcept override;
    void SetActorPlayer(uint32_t aFormId, bool aPlayer) noexcept override;
    uint32_t GetActorBaseFormId(uint32_t aFormId) const noexcept override;
    bool SetActorBaseFormId(uint32_t aFormId, uint32_t aBaseFormId) noexcept override;
    std::string GetActorBaseName(uint32_t aFormId) const noexcept override;
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
                             const AnimationVariables& aVariables, bool aPlace) noexcept override;
    float GetActorValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorPermanentValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    float GetActorBaseValue(uint32_t aFormId, int32_t aIndex) const noexcept override;
    void SetActorValue(uint32_t aFormId, int32_t aIndex, float aValue) noexcept override;
    void ForceActorValue(uint32_t aFormId, int32_t aForceMode, int32_t aIndex, float aValue) noexcept override;
    int32_t GetActorValueCount() const noexcept override { return 164; } // ActorValueInfo::kActorValueCount
    bool IsActorDisabled(uint32_t aFormId) const noexcept override;
    bool IsActorStillLoading(uint32_t aFormId) const noexcept override;
    bool IsActorDeleted(uint32_t aFormId) const noexcept override;
    void EnableActor(uint32_t aFormId) noexcept override;
    void DisableActor(uint32_t aFormId, bool aDelete) noexcept override;
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
    bool AtMainLoop = false;
    std::deque<std::function<void()>> MainTasks;
    uint32_t AppearanceAttempts = 0;
    bool ApplyActorLooks(uint32_t aFormId, const std::string& acAppearance, uint32_t aChangeFlags, float aWeight) noexcept override;
    uint32_t SerializeActorNpc(uint32_t aFormId, std::string& aBuffer) noexcept override;
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
    float GetPlayerHeading() const noexcept override { return Heading; }
    void SetPlayerHeading(float aHeading) noexcept override { Heading = aHeading; }
    bool IsPlayerMounted() const noexcept override { return Mounts.contains(kPlayer); }
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
    static constexpr uint8_t kArmor = 26;
    static constexpr uint32_t kRightHand = 0x13F42; // DefaultObjectManager's equip slots
    static constexpr uint32_t kLeftHand = 0x13F43;
    std::map<uint32_t, ItemForm> ItemForms{{0x00012EB7, {kWeapon}},          // Iron Sword
                                           {0x00013989, {kWeapon}},          // Steel Sword
                                           {0x0001397E, {kWeapon}},          // Iron Dagger
                                           {0x00012E49, {kArmor, 0x4}},      // Iron Armor (body)
                                           {0x00013952, {kArmor, 0x4}},      // Steel Armor (body)
                                           {0x00012E4D, {kArmor, 0x1 | 0x2}}}; // Iron Helmet (head, hair)
    // The actor equips or unequips by itself (the player's menu, an NPC's AI), as EquipManager::Equip (38894) and
    // UnEquip (38901) through our hooks: a local actor's change is raised (EquipmentChangeEvent), a copy refuses it.
    void Equips(uint32_t aActor, uint32_t aItem, uint32_t aSlot = kRightHand);
    void Unequips(uint32_t aActor, uint32_t aItem, uint32_t aSlot = kRightHand);
    // The actor draws or sheathes by itself; ActorState's weapon state, the animation not modelled.
    void Draws(uint32_t aActor, bool aDraw) { Actors.at(aActor).Weapon = aDraw ? WeaponState::Drawn : WeaponState::Sheathed; }
    WeaponState GetWeaponState(uint32_t aFormId) const noexcept override;
    // Actor::SetWeaponDrawnEx (InventoryService's convergence): at once, as if its animation had played.
    void SetActorWeaponDrawnEx(uint32_t aFormId, bool aDraw) noexcept override;
    // Items. The actor gets or loses some by itself (a script's AddItem, a sale, eating), as our hooks on
    // AddInventoryItem and RemoveItem: raised (InventoryChangeEvent) unless under ScopedInventoryOverride; the last of
    // a worn item comes off first (through UnEquipHook).
    void GetsItem(uint32_t aActor, uint32_t aItem, int32_t aCount);
    // One with extra data (an enchantment, tempering...), its own stack.
    void GetsItem(uint32_t aActor, const Inventory::Entry& acItem);
    void LosesItem(uint32_t aActor, uint32_t aItem, int32_t aCount);
    // Dropped into the world, as HookDropObject: raised as a drop, the removal under ScopedInventoryOverride.
    void Drops(uint32_t aActor, uint32_t aItem, int32_t aCount);
    [[nodiscard]] int32_t CountOf(uint32_t aActor, uint32_t aItem) const;
    // Every item the actor carries (server ids) and how many.
    [[nodiscard]] std::map<std::pair<uint32_t, uint32_t>, int32_t> Carried(uint32_t aActor) const;
    // TESObjectREFR::AddOrRemoveItem: another game's change (InventoryService, under ScopedInventoryOverride).
    void ObjectAddOrRemoveItem(uint32_t aObjectFormId, const Inventory::Entry& aItem) noexcept override;
    // What the actor wears: each worn item (its server id) and whether in the left hand.
    [[nodiscard]] std::set<std::tuple<uint32_t, uint32_t, bool>> Worn(uint32_t aActor) const;
    uint8_t GetFormType(uint32_t aFormId) const noexcept override;
    bool IsRightHandEquipSlot(uint32_t aEquipSlotFormId) const noexcept override { return aEquipSlotFormId == kRightHand; }
    Inventory GetActorEquipment(uint32_t aActorFormId) const noexcept override;
    Inventory GetActorWornArmor(uint32_t aActorFormId) const noexcept override;
    // EquipManager's, ours: under ScopedEquipOverride, as EquipManager::Equip and UnEquip set it.
    void ActorEquipItem(uint32_t aActorFormId, uint32_t aItemFormId, uint32_t aEquipSlotFormId, int32_t aCount) noexcept override;
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
    bool m_mirroredMove = false; // the player's move is another game's party call's: party travel leads no one with it
    // A new temporary form id: the lowest a deleted reference gave back, as the engine reuses them, else the next.
    uint32_t NewTemporary() noexcept;
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

    std::deque<std::pair<LocalCall, uint32_t>> m_queued;
    RagdollPending m_pendingRagdolls;
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
