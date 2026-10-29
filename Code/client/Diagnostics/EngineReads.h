#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct bhkWorldObject;
struct BSFixedString;
struct TESObjectREFR;

// Engine state read for bug reports and the test harness's status (typed engine structs, no writes): what a
// report's snapshot and coop_test.py's checks show of an actor, the menus and the clock.
namespace Diagnostics::EngineReads
{
struct RagdollBody
{
    std::string Node;
    bool InWorld; // added to the physics world
    float Position[3];
    // Its Havok body (docs/design/physics-sync.md): hkpMotion::MotionType (4 keyframed), awake (its island
    // active), rotation (x, y, z, w) and velocity in game units.
    uint8_t Motion{};
    bool Awake{};
    float Rotation[4]{};
    float Velocity[3]{};
    uint16_t Flags{};   // its bhkNiCollisionObject's
    bool Offset{};      // a bhkRigidBodyT: placed apart from its node
    bool HasBone{};     // the node it moves (TESHavokUtilities::FindCollidableObject 15870): what a person sees
    float Bone[3]{};
};

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

// The actor's base looks the race menu sets beside its race: female (TESActorBaseData flag 1) and weight
// (TESNPC, 0 to 100). None: no such actor or no NPC base.
struct BaseLooks
{
    bool Female{};
    float Weight{};
    uint16_t Level{};
};

// How an actor's body stands, as a person sees it: its feet's height above the floor under it (a ray down past its
// own collision), and its pelvis and head nodes' (NPC Pelvis [Pelv], NPC Head [Head]) above that floor. Standing:
// feet ~0, pelvis ~70, head ~120; lying: pelvis and head near the floor. None: no 3D, no floor found within 2000
// units, or the frame's picks are spent.
struct BodyPose
{
    float FloorGap{}; // its position above the floor: > 0 floats, < 0 sinks
    float Root{};     // its 3D's root node (where its body is placed)
    float Skeleton{}; // NPC Root [Root], under which the animation moves the bones
    float Pelvis{};
    float Head{};
    uint32_t Floor{}; // what it stands on (CastRay's reference), 0 for the cell's own
};

// Its kill held off (a deferred kill, Character 38619) or queued on the task queue (ActorProcess 40011): either way it
// stays alive at any health until that ends.
struct KillState
{
    bool Deferred;
    bool Queued;
};

// The marker the player placed on the map: where, and its worldspace (an interior's cell); none without one.
struct MapMarker
{
    float X, Y, Z;
    uint32_t WorldOrCell;
};

// The voice line's sound (HighProcessData::soundHandles[0]): its id (-1 none), and its duration in ms
// (BSSoundHandle::GetDuration, 0 until the audio system knows it).
struct VoiceSound
{
    uint32_t Id = UINT32_MAX;
    uint64_t DurationMs = 0;
};

// The actor in water: how deep (Actor::GetSubmergedLevel, 0 dry .. 1; Actor::Update counts the head under at
// 0.875), swimming (ActorState1), the head under (boolFlags kUnderwater) and for how long (underWaterTimer).
struct Water
{
    float Submerged = 0.f;
    bool Swimming = false;
    bool HeadUnder = false;
    float SecondsUnder = 0.f;
};

AiReadout GetAiReadout(uint32_t aFormId) noexcept;

std::optional<BaseLooks> GetBaseLooks(uint32_t aFormId) noexcept;

std::optional<BodyPose> GetBodyPose(uint32_t aFormId) noexcept;

// Those it plays at this game's camera on the actor's behalf (a script's VisualEffect attached to the camera).
std::vector<uint32_t> GetCameraVisualFormIds(uint32_t aFormId) noexcept;

// The magic caster's state for a casting source (MagicCaster::State; 3 = charged), or -1.
int32_t GetCasterState(uint32_t aFormId, uint32_t aCastingSource) noexcept;

// The character controller's state (hkpCharacterStateType: 0 on the ground, 2 in the air), or -1.
int32_t GetCharacterState(uint32_t aFormId) noexcept;

// The actor whose health bar the HUD shows, or 0.
uint32_t GetEnemyHealthTarget() noexcept;

// Its fade in or out (0: none; ActorProcess::GetFadeState 39451): nothing activates an actor while it fades.
uint32_t GetFadeState(uint32_t aFormId) noexcept;

// How fast the game's clock runs (BSTimer, 1 normal): a kill cam, Slow Time or a bow zoom slow it.
float GetGlobalTimeMultiplier() noexcept;

// A global variable's value (TESGlobal 0x34), none for no such global.
std::optional<float> GetGlobalValue(uint32_t aGlobalId) noexcept;

// What the player holds with Activate (a grabbed physics object): its form id, 0 for nothing.
uint32_t GetGrabbedObject() noexcept;

// A boolean of the actor's behavior graph by name ("IsEquipping"), false if it has none.
bool GetGraphBool(uint32_t aFormId, const char* acpName) noexcept;

KillState GetKillState(uint32_t aFormId) noexcept;

// ActorState1::knockState: 0 standing, 1 and 3 lying in the ragdoll, 6 getting up (CommonLibSSE-NG KNOCK_STATE_ENUM).
uint8_t GetKnockState(uint32_t aFormId) noexcept;

// Where the menus' cursor points while it shows.
std::optional<std::pair<float, float>> GetMenuCursor() noexcept;

// UI's pause count and the flags of one open menu, for live-menu regressions.
uint32_t GetMenuPauseCount() noexcept;

// Names of the open menus (what blocks input: popups, console, menus).
std::vector<std::string> GetOpenMenus() noexcept;

// The type of the actor's current package (TESPackage::packType: 27 do-nothing), -1 without one.
int32_t GetPackageType(uint32_t aFormId) noexcept;

std::optional<MapMarker> GetPlayerMapMarker() noexcept;

// The effect shaders and art objects the engine is playing on the actor's body.
std::vector<uint32_t> GetPlayingVisualFormIds(uint32_t aFormId) noexcept;

// The actor's AI process level (0 high .. 3 low), -1 without a process; and whether the engine updates its
// AI at all (kProcessMe).
int32_t GetProcessLevel(uint32_t aFormId) noexcept;

// An actor's ragdoll bodies in the ragdoll's order (Havok/Ragdoll.h), each with its node empty; none for no such actor.
std::optional<std::vector<RagdollBody>> GetRagdoll(uint32_t aActorFormId) noexcept;

// The calendar's whole days passed (TimeData::rawDaysPassed), which its clock counts on from.
float GetRawDaysPassed() noexcept;

// The form id of the package in the actor's running slot, as plugins read it inline (0: none).
uint32_t GetRunningPackage(uint32_t aFormId) noexcept;

// The power or shout the actor has selected (Actor::equippedShout), 0 for none.
uint32_t GetSelectedPower(uint32_t aFormId) noexcept;

// Every shader and art object in the engine's list on the actor, the finished ones still fading out too: a hit
// shader without FX Persist is finished from its start (ShaderReferenceEffect::Init) and plays one pass.
std::vector<uint32_t> GetShownVisualFormIds(uint32_t aFormId) noexcept;

// ActorState1::sitSleepState: 0 normal, 3 sitting, 7 sleeping (CommonLibSSE-NG SIT_SLEEP_STATE).
uint8_t GetSitSleepState(uint32_t aFormId) noexcept;

// The subtitle on screen over the actor (SubtitleManager), empty if none.
std::string GetSubtitle(uint32_t aFormId) noexcept;

// Whose subtitle the HUD shows now (SubtitleManager's current speaker), 0 for none.
uint32_t GetSubtitleOnScreen() noexcept;

// Actor::soundCallBackSet: 3 queued, -1 playing (or none), 1/2 ending; -2 without the actor.
int32_t GetVoiceLineState(uint32_t aFormId) noexcept;

VoiceSound GetVoiceSound(uint32_t aFormId) noexcept;

// Seconds left of the actor's voice line once it has started (Actor::fVoiceTimer), else -1.
float GetVoiceTimer(uint32_t aFormId) noexcept;

// The hours the open Sleep/Wait Menu's slider shows (its SWF's HoursSlider), none while it is closed or loading.
std::optional<double> GetWaitMenuHours() noexcept;

Water GetWater(uint32_t aFormId) noexcept;

// A mount interaction (ExtraInteraction): a rider on its mount, or a mount with its rider (CommonLibSSE Actor::IsOnMount).
bool HasMountInteraction(uint32_t aFormId) noexcept;

// Its getting on or off animation plays: until it ends, a dismount is refused (Actor::Dismount 37906).
bool IsGettingOnOrOffMount(uint32_t aFormId) noexcept;

// The condition IsInCombat on the actor (21576), as a dialogue line reads it: a copy answers its owner's.
bool IsInCombat(uint32_t aFormId) noexcept;

// A kill cam plays: the game ignores the player's buttons until it ends.
bool IsKillCamPlaying() noexcept;

// The map is open and takes input (its controls are ready, as MapMenu::PlaceMarker 53113 requires).
bool IsMapReady() noexcept;

bool IsMenuOpen(const char* acpMenu) noexcept;

bool IsProcessMe(uint32_t aFormId) noexcept;

// The actor's sneaking flag (ActorState1::sneaking), what detection reads; false without the actor.
bool IsSneaking(uint32_t aFormId) noexcept;

// Actor::IsTalking (37266): what the condition IsTalking and the dialogue menu read.
bool IsTalking(uint32_t aFormId) noexcept;

// The player may get on the mount now: what offers "Ride" and what InitiateMountPackage checks first (38706).
bool PlayerCanRide(uint32_t aMountFormId) noexcept;

// The node a physics body moves (a ragdoll body's bone), in world space: TESHavokUtilities::FindCollidableObject 15870.
bool BoneOf(const bhkWorldObject& acBody, float aPosition[3]) noexcept;

// A node of the reference's 3D by name (NiAVObject::GetObjectByName), in world space: the 3D on screen (the
// player's first-person one in first person), or the third-person body others see.
std::optional<glm::vec3> NodePosition(TESObjectREFR* apRef, const BSFixedString& acName, bool aBody = false) noexcept;
} // namespace Diagnostics::EngineReads
