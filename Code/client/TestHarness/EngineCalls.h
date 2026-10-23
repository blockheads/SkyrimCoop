#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The test harness's own engine calls (scripts/coop_test.py drives them through the harness's commands):
// staging, the player's input, and engine state the scenarios check. Test-only: the client's services
// go through the bridges, never through these.
namespace TestHarness::EngineCalls
{
// --- Console and menus ---

// SKSE's AddUITask (SKSETaskInterface), for console commands run where typed ones run.
void SetUITaskQueue(void (*apAddUITask)(void*)) noexcept;
// Runs the line now, as the console does (Console::ExecuteCommand).
void ExecuteConsoleCommand(const char* acpLine) noexcept;
// Runs the line where SKYRIMCOOP_CONSOLE_PHASE says: "ui" where typed commands run (the UI's message
// processing), otherwise queued for this frame; a cell change with the game paused, as the open console does.
void RunConsoleCommand(const std::string& acLine) noexcept;
// Lifts the pauses of the lines queued last frame (they have run). Once a frame, before new commands.
void EndConsolePauses() noexcept;
bool IsMenuOpen(const char* acpMenu) noexcept;
// Shows or hides a menu through the UI message queue, as the game does (the console: "Console").
void SetMenuShown(const char* acpMenu, bool aShown) noexcept;
// Names of the open menus (what blocks input: popups, console, menus).
std::vector<std::string> GetOpenMenus() noexcept;
// A kill cam plays: the game ignores the player's buttons until it ends.
bool IsKillCamPlaying() noexcept;
// How fast the game's clock runs (BSTimer, 1 normal): a kill cam, Slow Time or a bow zoom slow it.
float GetGlobalTimeMultiplier() noexcept;
// The marker the player placed on the map: where, and its worldspace (an interior's cell); none without one.
struct MapMarker
{
    float X, Y, Z;
    uint32_t WorldOrCell;
};
std::optional<MapMarker> GetPlayerMapMarker() noexcept;
// The map is open and takes input (its controls are ready, as MapMenu::PlaceMarker 53113 requires).
bool IsMapReady() noexcept;
// Removes it, as the map's "Remove It" does (40536).
void ClearPlayerMapMarker() noexcept;
// The hours the open Sleep/Wait Menu's slider shows (its SWF's HoursSlider), none while it is closed or loading.
std::optional<double> GetWaitMenuHours() noexcept;
// A number or boolean (1/0) in an open menu's movie ("_root.Menu_mc.bFadedIn"); none: no such menu or value.
std::optional<double> GetMenuVariable(const char* acpMenu, const char* acpPath) noexcept;

// --- Quests ---

// A quest as the engine holds it now (TESQuest): what its change form in a save carries, read live.
struct QuestState
{
    uint16_t Flags;
    uint16_t CurrentStage;
    bool AlreadyRun;
    std::vector<std::pair<uint16_t, bool>> Stages;      // executed stages: index, done
    std::vector<std::pair<uint16_t, uint8_t>> Objectives; // index, state
    struct Alias
    {
        uint32_t Id;
        std::string Name;
        uint32_t Ref;  // the reference filling it (GetAliasedRef 25066), 0 for none or a location alias
        uint32_t Base; // that reference's base form
        uint32_t Location; // a location alias's fill (25067), 0 for none or a reference alias
    };
    std::vector<Alias> Aliases;
};
std::optional<QuestState> GetQuestState(uint32_t aQuestId) noexcept;
// Fills the quest's reference alias with the reference, read from its plugin first when it is not in memory (indoors:
// aParentId its cell; outdoors: its worldspace and grid cell); false if either is missing.
bool ForceQuestAlias(uint32_t aQuestId, uint32_t aAliasId, uint32_t aRefId, uint32_t aParentId, int16_t aGridX, int16_t aGridY) noexcept;
// A global variable's value (TESGlobal 0x34), none for no such global.
std::optional<float> GetGlobalValue(uint32_t aGlobalId) noexcept;
// The calendar's whole days passed (TimeData::rawDaysPassed), which its clock counts on from.
float GetRawDaysPassed() noexcept;

// --- Magic ---

// The power or shout the actor has selected (Actor::equippedShout), 0 for none.
uint32_t GetSelectedPower(uint32_t aFormId) noexcept;

// --- Form state (world-state sync) ---

// What a form would write to a save (FormState::Save): for aChangeFlags, or its recorded changes.
struct SavedForm
{
    uint32_t FormId;
    uint32_t ChangeFlags;
    std::string Data;
};
std::optional<SavedForm> SaveFormState(uint32_t aFormId, bool aBaseForm, std::optional<uint32_t> aChangeFlags) noexcept;
bool LoadFormState(uint32_t aFormId, uint32_t aChangeFlags, const std::string& acData) noexcept;

// --- Scripts ---

// The state a script on that form is in (Object::currentState; "" is the empty state), as the VM binds it
// (FindBoundObject, its vfunc 0x1C).
std::optional<std::string> GetScriptState(uint32_t aFormId, const char* acpScript) noexcept;
// Puts the script back in a state (Object::currentState, as GotoState leaves it, without its OnEndState and
// OnBeginState): a scenario's reset of a script that runs once per state.
bool SetScriptState(uint32_t aFormId, const char* acpScript, const char* acpState) noexcept;

// A script variable's value (an auto property's is "::<Name>_var"). Type is the VM's Variable::Type:
// 0 none, 1-5 object/string/int/float/bool; an object's is its form (0 for None or an object type's raw value).
struct ScriptValue
{
    uint64_t Type{};
    int32_t Int{};
    float Float{};
    bool Bool{};
    std::string String;
    uint32_t Form{};
};
std::optional<ScriptValue> GetScriptVariable(uint32_t aFormId, const char* acpScript, const char* acpVariable) noexcept;
// Sets an int, float or bool variable, keeping its type (a scenario's control of a script's flag).
bool SetScriptVariable(uint32_t aFormId, const char* acpScript, const char* acpVariable, double aValue) noexcept;
// Sets an object variable or property to a form (BSScript::Variable::SetForm).
bool SetScriptVariableForm(uint32_t aFormId, const char* acpScript, const char* acpVariable, uint32_t aValueFormId) noexcept;

// The VM's running stacks (allRunningStacks: running, waiting in a latent call like Utility.Wait, or queued),
// each as its frames' "Script.Function", innermost first. Read on the game thread outside the VM's update.
struct ScriptStack
{
    uint32_t Id{};
    std::vector<std::string> Frames;
};
std::vector<ScriptStack> GetRunningScripts() noexcept;

// A script class's global natives, each with whether it acts on this game's player (NativeReach), as the VM has
// them (ObjectTypeInfo's global functions); none for a class the VM has not loaded.
struct GlobalNative
{
    std::string Name;
    bool ReachesThisPlayer{};
};
std::optional<std::vector<GlobalNative>> GetGlobalNatives(const char* acpClass) noexcept;

// --- Inventories ---

// How many of that item the reference holds (TESObjectREFR::GetContainerCount); none for no container or item.
std::optional<int64_t> GetItemCount(uint32_t aObjectFormId, uint32_t aItemFormId) noexcept;
// Every item the reference's container lists or its changes hold, with its count (a leveled list's not rolled);
// none for no container.
std::optional<std::vector<std::pair<uint32_t, int32_t>>> GetContainerItems(uint32_t aObjectFormId) noexcept;
// Whether the reference is enabled (its disabled flag, TESForm 0x800); none: no such reference.
std::optional<bool> IsReferenceEnabled(uint32_t aFormId) noexcept;

// --- Staging and the player's own input ---

// The player's buttons, by the user event they are mapped to.
enum class Button : uint8_t
{
    RightHand,  // "Right Attack/Block": swing, block, or charge and cast the right hand's spell
    LeftHand,   // "Left Attack/Block"
    ReadyWeapon, // "Ready Weapon": draw or sheathe
    Sneak,       // "Sneak": toggles sneaking (the default mapping)
    Forward,     // "Forward": walks or swims where the camera looks
    Shout        // "Shout": the equipped shout or power
};
// Sends the button through the game's input dispatch, as its device would (docs/re/systems/input.md):
// down with aHeldSecs 0 is the press, down with aHeldSecs > 0 the event a device sends every frame while
// the button stays down, and up the release after aHeldSecs. Everything that listens to input sees it,
// the same as a player's. False if the input system is not up.
bool SendPlayerButton(Button aButton, bool aDown, float aHeldSecs) noexcept;
// A keyboard key by DirectInput scan code, as the keyboard sends it: given the user event the key is mapped
// to in the open menu or gameplay (ControlMap), then through the same dispatch.
bool SendKey(uint32_t aScanCode, bool aDown) noexcept;
// A mouse button (0 left, 1 right) and a mouse move by a delta, the same way.
bool SendMouseButton(uint32_t aButton, bool aDown) noexcept;
bool SendMouseMove(int32_t aDeltaX, int32_t aDeltaY) noexcept;
// Where the menus' cursor points while it shows.
std::optional<std::pair<float, float>> GetMenuCursor() noexcept;
void ForcePlayerFirstPerson() noexcept;
// Turns the player toward an actor, yaw and pitch, in first person (aimed spells fly where the camera
// looks).
bool FacePlayerToward(uint32_t aTargetFormId) noexcept;
// Moves the player aDistance from a reference (an actor, a trigger), on the line toward where the player
// stands, and faces it.
// A move, not a teleport: no cell arrival.
bool PlacePlayerBefore(uint32_t aTargetFormId, float aDistance, std::optional<float> aBearingDegrees = std::nullopt) noexcept;
// Unequips everything in this game only, nothing sent (a copy that drifted).
void UnequipAllUnsynced(uint32_t aFormId) noexcept;
// As the game does them, through its own entry points, so our hooks see them as they see the game's
// (the bridge's Activate and lock replays call past the hooks): the player activates the object
// (pressing E on it), and the object's lock changes.
void PlayerActivates(uint32_t aObjectFormId) noexcept;
bool ChangeLockAsGame(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept;
// Freezes the actor in this game, as PapyrusExtender's FreezeActor does: out of furniture, no AI (kProcessMe), restrained (no
// movement), no velocity, and its controller neither pushed nor colliding with characters, so it stands still and
// players pass through it; it still takes hits. Thawing puts back what freezing changed.
bool FreezeActor(uint32_t aFormId, bool aFrozen) noexcept;
// Takes the actor out of any furniture and moves it to that spot in the player's cell, wherever it was (loaded or
// not), as MoveTo does (56626).
bool MoveActorToPlayer(uint32_t aFormId, const float acPosition[3]) noexcept;
// Takes the actor out of any furniture and back to where the game placed it (its editor location, 19516).
bool MoveActorHome(uint32_t aFormId) noexcept;
// Opens or closes the door in this game only, as ObjectReference.SetOpen does (snapped: no animation).
bool SetDoorOpen(uint32_t aObjectFormId, bool aOpen, bool aSnap) noexcept;

// --- Engine state the scenarios check ---

// The doors, containers and furniture in the player's cell, with where they stand.
struct CellObject
{
    uint32_t Id;
    float Position[3];
};
std::vector<CellObject> GetCellObjects() noexcept;

// A reference in a loaded cell as this game holds and shows it: everything world-state sync could carry
// (scripts/coop_test.py's world diff compares the two games').
struct ReferenceState
{
    uint32_t Id;
    uint32_t Base;
    uint8_t BaseType; // FormType
    bool Actor;
    uint32_t Cell;
    uint32_t Flags; // TESForm::flags: disabled 0x800, deleted 0x20, harvested 0x2000
    float Position[3];
    float Rotation[3];
    float Scale;
    bool Has3D;
    bool Hidden;            // its 3D's kHidden
    float Position3D[3];    // its 3D's world position (what the player sees)
    bool HasLock;
    bool Locked;
    uint8_t LockLevel;
    uint8_t OpenState;      // doors only (TESObjectREFR::OpenState)
    uint32_t Owner;         // ExtraOwnership, 0 none
    uint32_t Changes;       // its change record's flags: what this game keeps when its cell unloads
    std::optional<std::vector<std::pair<uint32_t, int32_t>>> Items; // not actors' (GetContainerItems)
    uint32_t Leveled{};
    bool Stored{};       // its changes in a stored buffer (TESForm::HasStoredChanges)                                              // its base container's leveled entries

    // With details: the side effects a change has on what the player sees and touches.
    struct Node
    {
        std::string Name;
        float Position[3];
        float Rotate[9]; // world rotation, row-major
    };
    std::vector<Node> Pose; // its 3D's named nodes: where any animation left them
    struct Body
    {
        std::string Node;
        bool InWorld; // added to the physics world
        float Position[3];
    };
    std::vector<Body> Collision;                      // its 3D's physics bodies
    std::vector<std::pair<std::string, uint32_t>> Graph; // its active behavior graph's variables, raw
    std::vector<uint32_t> Effects;                    // effect shaders and art objects playing on it
};
// Every reference in the loaded cells: the interior, or the attached exterior grid (TES). With aDetails, each
// one's pose, physics, graph and effects too (larger).
std::vector<ReferenceState> GetLoadedReferences(bool aDetails) noexcept;

// The effect shaders and art objects the engine is playing on the actor.
std::vector<uint32_t> GetPlayingVisualFormIds(uint32_t aFormId) noexcept;
// The actor whose health bar the HUD shows, or 0.
uint32_t GetEnemyHealthTarget() noexcept;
// The player's last used combat skill (an actor value, -1 none yet) and its experience: where shared XP goes.
std::pair<int32_t, float> GetCombatSkillExperience() noexcept;
// The magic caster's state for a casting source (MagicCaster::State; 3 = charged), or -1.
int32_t GetCasterState(uint32_t aFormId, uint32_t aCastingSource) noexcept;
// A boolean of the actor's behavior graph by name ("IsEquipping"), false if it has none.
bool GetGraphBool(uint32_t aFormId, const char* acpName) noexcept;
// The actor is in a kill move, either half (Actor::IsInKillMove 38615).
bool IsInKillMove(uint32_t aFormId) noexcept;
// What decides a kill move for an attack (the ShouldAttackKill condition, 21744).
struct KillMoveCheck
{
    bool ShouldAttackKill; // Actor::sub_38559: a fatal hit, a killable victim, an attacker in combat
    bool Killable;         // Character::sub_37229(victim, attacker)
    bool EngagingInCombat; // Actor::EngagingInCombat (38563), the attacker
    bool InKillMove;       // Actor::IsInKillMove (38615), the attacker
    uint32_t CrosshairActor; // the actor under the player's crosshair (CrosshairPickData)
    uint32_t AttackTarget;   // whom an attack starting now targets (Actor::sub_47272 in reach, as PlayerControls 42350
                             // picks it)
};
KillMoveCheck CheckKillMove(uint32_t aAttackerFormId, uint32_t aVictimFormId) noexcept;
// Each condition of an idle and its parents (TESIdleForm::parentIdle), evaluated alone as the engine does
// for the subject on the target: which one keeps an animation (a kill move) from being picked.
struct IdleCondition
{
    uint32_t Idle;
    uint16_t Function; // the condition function (CommonLib FUNCTION_DATA::FunctionID)
    uint8_t OpCode;    // TESConditionItem::OpCode: == != > >= < <=
    bool Or;           // grouped with the next item
    uint8_t RunOn;     // TESConditionItem::RunOn
    float Comparison;  // a global's value when it compares against one
    bool True;
};
std::vector<IdleCondition> CheckIdleConditions(uint32_t aIdleFormId, uint32_t aSubjectFormId,
                                               uint32_t aTargetFormId) noexcept;
// The idle and its parents up the idle tree: the behaviour event each sends (ENAM; empty on a branch) and
// whether its whole condition list holds for the subject on the target, as the engine groups it.
struct IdleNode
{
    uint32_t Idle;
    std::string Event;
    bool Passes;
};
std::vector<IdleNode> CheckIdleTree(uint32_t aIdleFormId, uint32_t aSubjectFormId, uint32_t aTargetFormId) noexcept;
// Actor::soundCallBackSet: 3 queued, -1 playing (or none), 1/2 ending; -2 without the actor.
int32_t GetVoiceLineState(uint32_t aFormId) noexcept;
// Seconds left of the actor's voice line once it has started (Actor::fVoiceTimer), else -1.
float GetVoiceTimer(uint32_t aFormId) noexcept;
// The actor's AI process level (0 high .. 3 low), -1 without a process; and whether the engine updates its
// AI at all (kProcessMe).
int32_t GetProcessLevel(uint32_t aFormId) noexcept;
// The voice line's sound (HighProcessData::soundHandles[0]): its id (-1 none), and its duration in ms
// (BSSoundHandle::GetDuration, 0 until the audio system knows it).
struct VoiceSound
{
    uint32_t Id = UINT32_MAX;
    uint64_t DurationMs = 0;
};
VoiceSound GetVoiceSound(uint32_t aFormId) noexcept;
bool IsProcessMe(uint32_t aFormId) noexcept;
// The type of the actor's current package (TESPackage::packType: 27 do-nothing), -1 without one.
int32_t GetPackageType(uint32_t aFormId) noexcept;
// The form id of the package in the actor's running slot, as plugins read it inline (0: none).
uint32_t GetRunningPackage(uint32_t aFormId) noexcept;
// An actor's AI state as conditions and Papyrus read it (a copy answers its owner's), form ids, 0 for none.
struct AiReadout
{
    uint32_t Package = 0;       // Actor::GetCurrentPackage (37399)
    uint32_t PackageTarget = 0; // the running package's
    uint32_t Scene = 0;         // Actor::GetCurrentScene (37247)
    uint32_t Furniture = 0;     // MiddleProcess::occupiedFurniture
    uint32_t FollowTarget = 0;  // AIProcess::followTarget
    uint32_t Combat = 0;        // the condition GetCombatState (21740)
    uint32_t CombatTarget = 0;  // Actor::currentCombatTarget
};
AiReadout GetAiReadout(uint32_t aFormId) noexcept;
// Puts the package in the actor's running slot and sends its package event, as package evaluation would
// (ActorPackage::InitFromPackage 38959, TESPackageEvent): staging on the owner, with the actor frozen so evaluation
// does not replace it.
bool PutPackage(uint32_t aFormId, uint32_t aPackageId) noexcept;
// Actor::IsTalking (37266): what the condition IsTalking and the dialogue menu read.
bool IsTalking(uint32_t aFormId) noexcept;
// The condition IsInCombat on the actor (21576), as a dialogue line reads it: a copy answers its owner's.
bool IsInCombat(uint32_t aFormId) noexcept;
// The condition GetIsCurrentPackage (21529) on the actor: what dialogue lines read.
std::optional<bool> IsCurrentPackage(uint32_t aFormId, uint32_t aPackageId) noexcept;
// The actor's sneaking flag (ActorState1::sneaking), what detection reads; false without the actor.
bool IsSneaking(uint32_t aFormId) noexcept;
// ActorState1::sitSleepState: 0 normal, 3 sitting, 7 sleeping (CommonLibSSE-NG SIT_SLEEP_STATE).
uint8_t GetSitSleepState(uint32_t aFormId) noexcept;
// The actor in water: how deep (Actor::GetSubmergedLevel, 0 dry .. 1; Actor::Update counts the head under at
// 0.875), swimming (ActorState1), the head under (boolFlags kUnderwater) and for how long (underWaterTimer).
struct Water
{
    float Submerged = 0.f;
    bool Swimming = false;
    bool HeadUnder = false;
    float SecondsUnder = 0.f;
};
Water GetWater(uint32_t aFormId) noexcept;
// The subtitle on screen over the actor (SubtitleManager), empty if none.
std::string GetSubtitle(uint32_t aFormId) noexcept;
// Whose subtitle the HUD shows now (SubtitleManager's current speaker), 0 for none.
uint32_t GetSubtitleOnScreen() noexcept;
// The actor's behaviour graph state as a spawn sends it (TESObjectREFR::SaveAnimationGraphState), and the
// microseconds the save took; empty without the actor or its graphs. Measuring graph replication.
std::string SaveGraphState(uint32_t aFormId, int64_t& aMicroseconds) noexcept;
// Loads a saved state into the actor now, as a spawn does (LoadAnimationGraphState); false if it failed.
bool LoadGraphState(uint32_t aFormId, const std::string& acState, int64_t& aMicroseconds) noexcept;
// The variables each of the actor's graphs writes itself (GraphVariableWriters), with the node writing it and
// the project-wide name of the node's index, to check how sub-behaviours index variables.
struct GraphWriter
{
    std::string Project;
    std::string Variable;
    std::string ProjectName; // the project's variable at the node's index
    std::string Behaviour;
    std::string Node;
    int32_t Index = -1;
};
std::vector<GraphWriter> GetGraphWriters(uint32_t aFormId) noexcept;
// The character controller's state (hkpCharacterStateType: 0 on the ground, 2 in the air), or -1.
int32_t GetCharacterState(uint32_t aFormId) noexcept;
// The player's conversation (MenuTopicManager): the speaker and the options in menu order.
struct DialogueOption
{
    std::string Text;
    uint32_t Topic = 0;
    uint32_t Info = 0;
    uint32_t Quest = 0;
};
struct DialogueState
{
    bool MenuOpen = false; // the manager's flag
    uint32_t Speaker = 0;  // form id, 0 without one
    bool SkipHead = false; // the list's first node is not an option
    std::vector<DialogueOption> Options;
    // The menu's SWF (DialogueMenu.as), -1 without the menu: whether a pick is taken, its eMenuState, the selection.
    int32_t AllowProgress = -1;
    int32_t SwfState = -1;
    int32_t Selected = -1;
    uint32_t CursorTopic = 0; // the highlighted option's topic (its entry's topicIndex in the list)
    uint32_t CurrentInfo = 0; // MenuTopicManager's currentTopicInfo and lastTopicInfo, form ids
    uint32_t LastInfo = 0;
};
DialogueState GetDialogue() noexcept;
// Says the option at this index as the menu's click does, with the menu open or not (MenuTopicManager::Pick).
bool PickDialogue(uint32_t aIndex) noexcept;
// The actor starts a conversation with the player, as a ForceGreet package ends (Actor::SetDialogueWithPlayer
// 37200 with force greet, through its hook).
bool ForceGreet(uint32_t aActorFormId) noexcept;
// Whether the line is marked said (TESTopicInfo +0x3A), and setting it back (none: no such line).
std::optional<bool> SetInfoSaid(uint32_t aInfo, std::optional<bool> aSaid) noexcept;
// The player's actor value (a skill's level) and that skill's experience toward its next level.
float GetPlayerActorValue(uint32_t aIndex) noexcept;
// Any actor's (a copy's: what this game's scripts read of it).
float GetActorValue(uint32_t aActor, uint32_t aIndex) noexcept;
float GetPlayerSkillExperience(uint32_t aIndex) noexcept;
// How many times the player trained this level (PlayerCharacter +0x938), and setting it back.
uint32_t GetPlayerTrainings() noexcept;
void SetPlayerTrainings(uint32_t aCount) noexcept;
// The reference's owner (ExtraOwnership), loaded or not; 0: none or no such reference.
uint32_t GetOwner(uint32_t aRefFormId) noexcept;
// Whether the player knows the form (a word of power learned: TESForm flag 0x40), and setting it back
// (TESForm::SetPlayerKnows, 14639).
std::optional<bool> GetPlayerKnows(uint32_t aFormId) noexcept;
bool SetPlayerKnows(uint32_t aFormId, bool aKnows) noexcept;

// What the player's camera shows: its mode, where it is and looks (the world camera's transform), what the
// crosshair is on, and, given an actor, where its head is on screen and how far from the middle of the view.
struct CameraView
{
    uint32_t State = 0;         // TESCameraState::Id
    float Position[3]{};
    float Yaw = 0.f;            // degrees clockwise from north, as an actor's heading
    float Pitch = 0.f;          // degrees, positive looks down
    uint32_t Crosshair = 0;     // the reference under the crosshair, 0 for none
    bool TargetOnScreen = false;
    float TargetScreen[2]{};    // 0..1 from the left and the bottom
    float TargetOffCenter = 0.f; // degrees between the view and the target's head
};
std::optional<CameraView> GetCameraView(uint32_t aTargetFormId) noexcept;
} // namespace TestHarness::EngineCalls
