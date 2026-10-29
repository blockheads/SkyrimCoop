# Bundled first-time character start

The original join remains pending while a new friend makes a character. This start is built into the production
plugin and uses `SkyrimCoopStart.esp`; it does not execute or intercept Quick Start scripts or MQ101. Ordinary
New Game behavior is unchanged. The native bridge starts only after the compatible host returns `no_character`.

## Native bindings checked in IDA (Skyrim 1.6.1170)

- **40437 / 0x730250**, `PlayerCharacter::CenterOnCell_Impl(player, editorID, cell)`: confirmed against
  CommonLibSSE-NG `PlayerCharacter.cpp` and IDA. Exactly one of editorID/cell is required. It prepares the player
  target location and enters the engine's loading path, including a fresh world from the main menu.
- **52118 / 0x9426A0**, `NewGame_Enter()`: decompiled along with `MainMenu::Setup` (52113). It is the callback
  bound to `StartNewGame`, with no arguments consumed. It explicitly starts the INI's intro quest and generates
  a character ID. Our own start uses the cell startup instead, so it never queues the unsupported intro.
- **35757**, `BGSSaveLoadManager::GenerateCharacterID(manager)`: CommonLibSSE-NG binding and its call from
  52118 agree. Our start allocates its save identity before entering the room.
- **55576 / 0xA126F0**, `Game.SetInChargen(vm, stack, self, bool, bool, bool)`: IDA confirms only the three
  booleans are read, updating `PlayerCharacter::byCharGenFlag` at 0xB0A. The temporary start takes the save and
  wait flags, then releases them at finalization.
- **55580 / 0xA12780**, `Game.ShowRaceMenu()`: a no-argument thunk to 0x9533E0, which enqueues showing the
  race menu through `UIMessageQueue`. The ordinary race menu handles race, sex, appearance and naming.
- **52415 / 0x95A660**, `RaceSexMenu::ChangeName(menu, name)`: IDA and CommonLibSSE-NG agree on its arguments.
  It writes the NPC's name, marks FullName changed, emits the name notification and starts closing the menu.
  The hook calls the original and marks confirmation only during our own creator. Merely closing a menu never
  completes creation. Unrelated naming and message boxes are untouched.

Mutations run through `MainLoopTasks`, between main-loop frames. Polling observes the menu actually opening
before suspending the startup timeout. Naming, race-menu fade/close and text-entry/message-box closure precede
finalization. A later between-frame task finishes setup, then the join service captures through `ReadPlayer` /
`ReadKnowledge`. No temporary save or world transfer happens before this point.

The vanilla source installed in `Data/Scripts.zip`, `MQ101QuestScript.AddRaceSpells`, supplies the race's extra
learned spells (vanilla: Breton Conjure Familiar 640B6, Dunmer Sparks 2DD2A, Altmer Fury 4DEEB; it first removes the
others, `RemoveRaceSpells`). Our finalizer calls that function on MQ101's bound script (quest 3372B, `GlobalCall`'s
method call) and waits for its return, so a mod's patched version and its races apply; no race table of ours.
Racial abilities and powers come from the selected race's own spell list.
The default COC player's development inventory is replaced with prisoner tunic (3C9FE) and shoes (3CA00), worn,
before customization. Base Flames and Healing remain the player's starting spells.

## Bundled data

`GameFiles/Skyrim/SkyrimCoopStart.esp` is ESL flagged and depends only on Skyrim.esm. It contains a new interior
cell (local ID 800) and static scenery/lights with a COC marker, without quest, script, actor, door, container,
location or encounter data. Rebuild it with:

```
python3 scripts/build_start_room.py /path/to/Data/Skyrim.esm
```

The builder references the base game's small shack shell and lighting without modifying the original cell.
Both games install/enable this file. The harness does this automatically and retires its previously auto-enabled
external Quick Start. A missing room refuses creation at the main menu with installation instructions.

## Validation

Final fast/native/Steam results are in [the task report](../../reports/2026-10-29-first-time-join.md). Fast integration covers the original join, duplicate
requests, no early save, customization timeout suspension, startup/finalization deadlines, cancellation, host
loss, retry, automatic-host suppression, and both running-invite and launch-lobby identity retention.
Real Skyrim scenarios remain separate evidence from the fake Steam implementation and direct transport tests.

## Steam launches through SKSE

The real two-account closed-game test found that the standard SKSE loader loses `+connect_lobby`: its
`CreateProcess` has a null command line. Although the installed Steam DLL provides the Apps launch API,
Skyrim's `joinlobby` URL parameters did not appear there. `Code/launcher/Main.cpp` therefore preserves Steam's
original command line in the process-local `SKYRIMCOOP_LAUNCH_ARGS` environment variable, then starts SKSE.
Skyrim inherits that variable across SKSE's argument-free child creation. `SteamBridge::GetLaunchLobby` reads
it before trying the optional Apps API. No lobby is inferred from the test transport or injected by the harness.

Build the production launcher with `scripts/xmake-msvc.sh build SkyrimCoopLauncher`; it is independent of the
test-harness option. Install it beside SKSE and configure Steam's launcher entry to run it. The real test uses
a temporary Steam LaunchOptions wrapper only to redirect Skyrim's launcher to this compiled production binary
and keep the account in its isolated prefix. The wrapper preserves the actual Steam arguments unchanged.
