# Saving and loading a game (the join flow's engine side)

IDA 1.6.1170, 2026-10-23; names from CommonLibSSE-NG (`BGSSaveLoadManager.h`) and SKSE (`GameData.h`). Used by
`Games/Skyrim/SaveGameHooks.cpp` (guests refuse saves) and the join flow's save transfer
(`docs/design/join-flow.md`, "As built").

## One choke point for every save

`BGSSaveLoadManager::Save_Impl` (35727, `0x14060FD40`; CommonLibSSE-NG `Save_Impl(int32 deviceID, uint32
outputStats, const char* fileName)`, SKSE `Save_Internal`) is called by every save:

| Caller | Device | |
|---|---|---|
| `ConsoleFunc::handler::SaveGame` (`save <name>`) | 2 | with the name |
| the Journal's save (`sub_140978D20`, `sub_140979FE0`) | 2 | name made from the character |
| autosave, `BGSSaveLoadManager` 35732 (`Sky::Update`, then save) | 3 | load doors, fast travel, waiting |
| `Thread::ProcessEvents_Internal`, event `0xF0000200` (the quicksave key, `QuickSaveLoadHandler`) | 4 | |
| `Thread::ProcessEvents_Internal`, event `0xF0000800` / `0xF0000080` | 5 | queued saves |

None of the callers reads its result: returning false from a hook saves nothing and breaks nothing (the console
prints nothing; the quicksave's "Quicksaving..." HUD line comes from the caller after the call).

What it does: refuses (false, and "You cannot save right now" unless an autosave) when the player is in combat
states it rejects, VATS, a barter, furniture camera and similar; else builds the save in memory
(`Thread::SaveGame_HookTarget`, where SKSE writes its co-save) and writes it: **at once for device 2**, on the save
thread (`SaveOperationRequest`) for 3, 4 and 5. So `Save_Impl(2, 0, name)` (CommonLibSSE-NG's `Save(name)`) has the
`.ess` and the `.skse` on disk when it returns.

## Loading

`BGSSaveLoadManager::Load_Impl` (35728, `0x140610480`; `Load_Impl(fileName, deviceID, outputStats,
checkForMods)`), as CommonLibSSE-NG's `Load(name)`: `Load_Impl(name, -1, 0, true)`. It hands the file to
`BSWin32SaveDataSystemUtility` (vfunc 0x10 to open it, 0x88 to load it). `TESLoadGameEvent` follows the load
(`LoadGameEvents`), after SKSE's co-save load.

`SaveLoadManager.cpp` hooks this entry and quickload requests (35769) for [host loads](../../design/host-loads.md): a guest's own load is refused in
chat; a host's selected save goes through the party wipe's transfer and rejoin. The bridge's internal `Load` calls
the original trampoline so authorized party and join loads pass. The five-argument signature was checked against
CommonLibSSE-NG and the 1.6.1170 disassembly at `0x140610480`.

Quickload bypasses `Load_Impl`. `QuickSaveLoadHandler::ProcessButton` (52251) queues `0xD0000400` through 35769
(`0x140612D70`, `void(BGSSaveLoadManager*, uint32_t)`). `Thread::ProcessEvents_Internal` (35772) inlines the load,
selecting the character's `SaveEntry::lastQuicksave` through 35789 (`0x140617390`,
`SaveEntry*(BGSSaveLoadManager*, const uint32_t*)`). `SaveEntry` is CommonLibSSE-NG's per-character catalogue;
`lastQuicksave` has checked offset `0x40`. `LoadMostRecentSaveGame` (35766) has no direct callers in this build.
The real quickload scenario checks the guest's own level rolls back, as well as the clock and saved positions.

The manager is `BGSSaveLoadManager::singleton` (403340).

## Where a save or load can run (2026-10-24)

- Our update (`Plugin::Update`, every service) runs inside the Papyrus VM's update job (`HookVMUpdate`, 53926), on a
  job thread, while the main thread waits for it (`Main::sub_36555` 36555, `WaitForJobTask(0, 2)`). `Save_Impl` called
  there hung the host: a save serializes the VM, the main thread waited on the job, the job on the save (seen as the
  HangWatchdog's main loop stalled in 36555, every job thread idle). The console's `save` works because the console
  runs on the main thread.
- So the join flow's save and load run from `MainLoopTasks`, drained by `HookMainLoop` (36564) on the main thread
  between frames (`JoinBridge::SaveNow` answers on a later update; `WriteAndLoad` loads on the next frame). The fake
  engine follows the same rule (`FakeEngine::RunMainLoop`).
- The VM's update does not tick at the main menu: there `HookMainLoop` runs our update itself (`s_vmUpdated`), so a join
  from the main menu, its transport and timers, runs.
- The save path: `BSWin32SaveDataSystemUtility::PrepareFileSavePath` (109357, vfunc 2 of the utility from 109278) joins
  the My Games folder, `Saves\` and the name with `.ess` (109284). SKSE's co-save is the same path with `.skse`.
- Save_Impl's devices: 2 menu and console (written at once), 4 quicksave, 3 and 5 the engine's own autosaves
  (`((device - 3) & ~2) == 0` inside it). Our hook refuses all of them for a guest; autosaves say so once a session.

## The player in a save (2026-10-24)

`docs/design/join-flow.md`, "Where the engine keeps the player": `TESNPC::SaveGame` (24777) by change flag for the
base `0x7`; `PlayerCharacter::SaveGame` (vtable 0x0E, 0x14073A2F0) after `Actor::SaveGame` writes the player's own data
in one unlabelled run: perks with ranks `0x4B8` (`PlayerCharacter::AddPerk` 40681 adds there and to `0x4D0`;
`RemovePerk` 40682 queues a task), standing stone perks `0x4E8`, skills (`PlayerSkills` at `0x9B8`, written by
`sub_14077B810`: level experience, threshold, per skill level, experience, threshold, legendary), perk points
`0xB09` (`Game.AddPerkPoints` 55451), tints `0xB18`, mixed with the quest log `0x578`, objectives `0x588`, crime maps,
map markers. `Actor::AddSpell` 38716, `Actor::SwitchRace` 37925, `Actor::DoReset3D` 40255 (`Actor::QueueUpdate`).

Found by `guest-makes-character-then-joins` (2026-10-24):

- **Weight is saved with the actor, not its base.** `TESNPC::SaveGame` (24777) and `TESActorBase::SaveGame` (24387)
  never write `TESNPC+0x1FC`; `Actor::SaveGame` (37649) writes its base's weight in the actor's own data (after
  `+0x264`, every save), and `Actor::LoadGame` (37650) puts it back on the base, taking the biped's parts off when it
  changed (so worn meshes are made again at the new weight). Checked 2026-10-24: `player.setnpcweight 13`, save, a new
  process loads it: 13. The race menu's `ChangeWeight` (`sub_1409524D0`) writes the base directly (slider × 100), as
  RaceMenu's presets do (`PresetInterface::ApplyPreset`: `npc->weight`). skymp sends it apart (`appearance.ts`). Ours:
  `PlayerRecord::Weight`, and `Weight` on `AnnounceCharacterRequest` / `NotifyCharacterSpawn`, set on a copy's new
  base before its actor (`CreateCustomActor`).
- **Tint masks belong to a race and sex.** The player's masks (`0xB18`, count `0xB28`) point at the race's
  `TintLayer.file` for its sex (`TESRace+0x4A8` face data, `+0x90` tint layers). `sub_140749E90` (40696) rebuilds them
  from the race for the base's sex, at default colours (the race menu's slider load calls it); 40697 does that and
  then takes an NPC's tint layers. After the host's save loads, the player has the save's player's masks:
  `JoinBridge::ApplyPlayer` calls 40696 after `SwitchRace`, then puts the record's colours on by texture name (masks
  loaded from a save have type 0).
- **The base's flags in a session:** respawn mode sets `IS_ESSENTIAL` on the player's base (`TESActorBaseData` flags,
  section 0x2), so a character's base data differs from its record there while it plays.
- **The skills section has garbage in it.** `TESNPC::SaveGame` (24777) builds the 0x200 section (52 bytes) on its
  stack: skills and offsets (`+0x190`), health, magicka, stamina (`+0x1B4`), the u16 at `+0x1BA` as a float at 0x2C,
  the byte at `+0x241` at 0x30; it never writes 0x2A-0x2B and 0x31-0x33, so two saves of the same base can differ
  there. Byte comparisons mask them (`coop_test.py unpadded_skills`).
- **Two tint lists.** `PlayerCharacter` keeps its own masks (`0xB18`) and, while in an overlay race (a vampire),
  overlay masks (`0xB30`, `GetTints` prefers them): the head shows the overlay's colours (a vampire's pale skin,
  `0x7A86A7` on a Nord) while the save keeps the own masks. Copies are painted from what the head shows
  (`GetPlayerFaceTints`); the join record keeps the own masks (`JoinBridge::ReadPlayer`).

## The VM drain is not a gameplay update (2026-10-29)

Before serializing, `FreezeAndWait` (54015, `0x1409C9DB0`) sets the VM frozen through 53956, then
calls `ProcessRegisteredUpdates` (53926) until the outstanding VM work drains. That call can run
on the saving thread while the regular VM job is still in our update. `HookVMUpdate` must forward
the native call but skip `Plugin::Update` while `SkyrimVM::isFrozen` (`+0x680`) is true.

The global saving/loading flags are not sufficient: the failing single-save regression observes
a frozen VM and two active plugin updates while those global flags are still clear. Running the
full co-op update there races the shared registry, containers and network. The main-menu fallback
stays separate; a frozen VM call still counts as a VM tick for that frame.

`save-does-not-update-frozen-world` exercises this actual engine boundary and fails before the
guard. The fake engine's save bridge does not simulate Skyrim's VM drain. See the
[crash investigation](../crashes/2026-10-29-save-vm-reentry.md) for IDA evidence and red/green results.
