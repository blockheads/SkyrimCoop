#pragma once

#include <cstdint>
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
// processing), otherwise now (the top of the frame).
void RunConsoleCommand(const std::string& acLine) noexcept;
bool IsMenuOpen(const char* acpMenu) noexcept;
// Shows or hides a menu through the UI message queue, as the game does (the console: "Console").
void SetMenuShown(const char* acpMenu, bool aShown) noexcept;
// Names of the open menus (what blocks input: popups, console, menus).
std::vector<std::string> GetOpenMenus() noexcept;

// --- Staging and the player's own input ---

// The player's buttons, by the user event they are mapped to.
enum class Button : uint8_t
{
    RightHand,  // "Right Attack/Block": swing, block, or charge and cast the right hand's spell
    LeftHand,   // "Left Attack/Block"
    ReadyWeapon // "Ready Weapon": draw or sheathe
};
// Sends the button through the game's input dispatch, as its device would (docs/re/systems/input.md):
// down with aHeldSecs 0 is the press, down with aHeldSecs > 0 the event a device sends every frame while
// the button stays down, and up the release after aHeldSecs. Everything that listens to input sees it,
// the same as a player's. False if the input system is not up.
bool SendPlayerButton(Button aButton, bool aDown, float aHeldSecs) noexcept;
void ForcePlayerFirstPerson() noexcept;
// Turns the player toward an actor, yaw and pitch, in first person (aimed spells fly where the camera
// looks).
bool FacePlayerToward(uint32_t aTargetFormId) noexcept;
// Moves the player aDistance from an actor, on the line toward where the player stands, and faces it.
// A move, not a teleport: no cell arrival.
bool PlacePlayerBefore(uint32_t aTargetFormId, float aDistance) noexcept;
// Unequips everything in this game only, nothing sent (a copy that drifted).
void UnequipAllUnsynced(uint32_t aFormId) noexcept;
// As the game does them, through its own entry points, so our hooks see them as they see the game's
// (the bridge's Activate and lock replays call past the hooks): the player activates the object
// (pressing E on it), and the object's lock changes.
void PlayerActivates(uint32_t aObjectFormId) noexcept;
bool ChangeLockAsGame(uint32_t aObjectFormId, bool aLocked, uint8_t aLockLevel) noexcept;

// --- Engine state the scenarios check ---

// The doors and containers in the player's cell.
std::vector<uint32_t> GetCellObjectIds() noexcept;

// The effect shaders and art objects the engine is playing on the actor.
std::vector<uint32_t> GetPlayingVisualFormIds(uint32_t aFormId) noexcept;
// The actor whose health bar the HUD shows, or 0.
uint32_t GetEnemyHealthTarget() noexcept;
// The magic caster's state for a casting source (MagicCaster::State; 3 = charged), or -1.
int32_t GetCasterState(uint32_t aFormId, uint32_t aCastingSource) noexcept;
// A boolean of the actor's behavior graph by name ("IsEquipping"), false if it has none.
bool GetGraphBool(uint32_t aFormId, const char* acpName) noexcept;
// The character controller's state (hkpCharacterStateType: 0 on the ground, 2 in the air), or -1.
int32_t GetCharacterState(uint32_t aFormId) noexcept;
} // namespace TestHarness::EngineCalls
