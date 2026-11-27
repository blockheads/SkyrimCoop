# Saving and loading a game (the join flow's engine side)

IDA 1.6.1170, 2026-10-23; names from CommonLibSSE-NG (`BGSSaveLoadManager.h`) and SKSE (`GameData.h`). Used by
`engine/SaveGameHooks.cpp` (guests refuse saves) and the join flow's save transfer
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
chat; a host's selected save goes through the party's transfer and rejoin (also how a wipe ends: the host chooses). The bridge's internal `Load` calls
the original trampoline so authorized party and join loads pass. The five-argument signature was checked against
CommonLibSSE-NG and the 1.6.1170 disassembly at `0x140610480`.

Quickload bypasses `Load_Impl`. `QuickSaveLoadHandler::ProcessButton` (52251) queues `0xD0000400` through 35769
(`0x140612D70`, `void(BGSSaveLoadManager*, uint32_t)`). `Thread::ProcessEvents_Internal` (35772) inlines the load,
selecting the character's `SaveEntry::lastQuicksave` through 35789 (`0x140617390`,
`SaveEntry*(BGSSaveLoadManager*, const uint32_t*)`). `SaveEntry` is CommonLibSSE-NG's per-character catalogue;
`lastQuicksave` has checked offset `0x40`. `LoadMostRecentSaveGame` (35766) has no direct callers in this build.
The real quickload scenario checks the guest's own level rolls back, as well as the clock and saved positions.

The manager is `BGSSaveLoadManager::singleton` (403340).

## Inside a load (1.7.104, 2026-11-23)

Read in IDA 1.7.104 for the save/load explanation (PR #110); names and comments in the database.

- **Quickload and a menu load are one load.** `Load_Impl` (35728) and quickload's request (`0xD0000400`, handled in
  `Thread::ProcessEvents_Internal` 35772) both hand the file to the save utility's vfunc `0xB8` with the same
  parameters. Quickload first waits two passes of the queue (`g_quickloadFramesWaited`, its HUD line on the first)
  and picks `SaveEntry::lastQuicksave`; a quicksave (`0xF0000200`) waits the same way, then `Save_Impl` device 4.
- **The load runs on the main thread** as request `0xD0000010` (35772 -> `LoadGame_HookTarget_Enter` 442580 ->
  `Thread::LoadGame_HookTarget` 35600). In 35600, in order:
  1. the save's plugin list checked (`BGSSaveLoadGame__ReadPluginList` 442571), then `PrepareForLoad` (35779): VM
     frozen, master file loads cancelled, queued model handles dropped, audio killed, menus closed;
  2. global data (`LoadGlobalData` 35648), then a first pass over the save's change forms: each looked up and
     `TESForm::Revert` (vfunc `0x90`, slot `0x12`, CommonLibSSE-NG) puts it back to its plugin state;
  3. forms the running game changed that the save does not list are reverted too (`RevertFormsNotInSave` 35566):
     a load inside a game is a revert plus the save's changes, not a new world;
  4. a second pass: `LoadGame` (vfunc `0x78`) with each form's data;
  5. `InitAndFinishLoadedForms(wait=1)` (35567): `InitLoadGame` (`0x80`) on each, then the IO manager's queue drained
     on the main thread (`IOManager` 75708, priority 5, at `0x140615d01`), then `FinishLoadGame` (`0x88`) on each;
  6. back in 442580, `FinishLoad` (35780): master file loads resume, terrain, the nearby actor list, the story
     teller, the VM thawed.
- **Read, not measured:** step 5 is where a load can wait on queued IO. Which jobs a given load waits for (the cells
  and models around the player) is an inference; nothing here measured it.

## Back to the main menu (2026-11-18)

A guest leaving goes back with `papyrus::Game::QuitToMainMenu` (55490, `0x140a253e0`; read in IDA, 1.7.104). It
queues kShow for the Mist Menu (`UI_ShowMistMenu`, 52316, `0x140965710`: InterfaceStrings `+0x178`), then a FaderData
message (`UI_QueueFaderMessage`, 52848, `0x14098DDD0`) carrying a `FadeThenMainMenuCallback`. Its Run (55646, read)
only sets `Main::resetGame` (`Main+0x11`, CommonLibSSE-NG); the main loop's reset then opens the main menu. Inferred, not
read: that the fader runs the callback once its fade ends (measured: the guest's main menu came about 2 s after the
leave). So the menu opens frames later: `IsAtMainMenu` stays false meanwhile, and a harness step that leaves must wait for
the menu (`guest_leaves_to_main_menu`) before joining from it.

## The character a save belongs to (2026-11-08)

IDA 1.7.104. The engine keeps no character id inside a save: **the id is in the file's name**.

- Saves are named `Save|Autosave|Quicksave<N>_<id hex>_<modded>_<name hex>_<place>_<playtime>_<yyyymmddhhmmss>_<x>_1`
  (9 `_` fields), the id the manager's `currentCharacterID` (`+0xD0`, CommonLibSSE-NG; `Save_Impl` files the save
  under it, `GetSaveEntry` 35789).
- `BSSaveDataSystemUtility::Entry` parses a name (109348; the prefixes by `_strnicmp`, the last field must be 1, 9
  fields): the id at `+0x180`. Its constructor (109294) leaves 0 there, so any other name parses to id 0.
- A load (`LoadGame_HookTarget_Enter` 442580) parses the loaded file's name and, once loaded, sets
  `currentCharacterID` and `displayCharacterID` (`+0xD8`, copied to `BSWin32SaveDataSystemUtility+0x64`) to
  `{that id, modded}`: `SetCurrentCharacter` (35751) inlined; `SetDisplayCharacter` (35752) is the display half
  (the console's `SetDisplayPlayerID`). The catalogue's entries (`characterID` `+0x6C`) come from the same parse.
- So loading `SkyrimCoopWipe` (or `SkyrimCoopJoin`) leaves id 0: saves made afterwards are named and filed under 0
  (and the wipe's old automatic pick of the newest save, removed 2026-11-16, took another character's, 2026-11-08).

Ours: `SaveNames::CharacterId` is the same parse; after the party's scratch load the host calls `SetCurrentCharacter`
(35751) with the checkpoint's id (`HostService::OnGameLoaded`, `JoinBridge::SetCharacter`), keeping the load's modded
flag. A guest's join load still leaves its id 0 (it makes no saves in a session).

## Continue and the Load list (2026-11-27)

IDA 1.7.104, read for "Continue showed another playthrough's saves"; names and comments in the database.

- **Continue loads the newest save of any character.** The main menu's CONTINUE (52114) asks Flash to confirm, which
  calls `ContinueLastSavedGame` (52117) -> 52898: `PopulateSaveList` (35760) rebuilds `saveGameList` with the
  character filter `-1` (every character, `BuildSaveList` 35743), and queues `0xD0000100`. `Thread::ProcessEvents_Internal`
  (35772) loads that request's name (manager `+0x240`) through the save utility's vfunc `0xB8`, not `Load_Impl`, so none
  of our load hooks see it. The name is the list's last entry: `BuildSaveList` scans `<Saves>/*.ess` (35799) and inserts
  each by file time (35784, comparing FILETIME at `+0x50` with 35795): the newest file wins, whatever its name.
  `PopulateSaveList` keeps the list it has only when one is built (`+0x118`) and its last filter id (`+0xE0`, set by
  35925) is 0; Continue's own filter is -1, so the next Continue rescans. 35775's event type 0 (which event: not read)
  drops the list and sets the current and display character to -1.
- **The Journal's list is the current character's.** Its SAVE and LOAD buttons (registered by 52902) call
  `OpenSaveLoadList` (52924): `SetDisplayCharacter` with `currentCharacterID` (`+0xD0`, the id and the modded flag),
  then the list fills from `saveGameList` newest first (52894). The fake's `LoadList` compares the id only.
- **So a scratch save is a playthrough of its own.** `SkyrimCoopJoin` (a guest's join load) and `SkyrimCoopWipe` (a
  host load) were written to the Saves folder and kept: the newest file, Continue loaded it, its name gave character id
  0 (above), and the Load list then showed every save filed under 0 (other playthroughs' saves made after such a load,
  older builds' included). Ours: `JoinService` deletes every `SaveNames::kScratch` save once any load ends or fails,
  and on reaching the main menu (a crash mid-load, an older build); a write that fails part way is deleted at once;
  `JoinBridge::MakeSave` already deleted the transfer's. Deleted before the player has control again, so no list
  built later holds one. Continue stays vanilla's: the newest player save, whichever playthrough (a host that loads
  an older playthrough and quits without saving continues the newest one). GameTests `[saves]`, the verb
  `expect_no_scratch_saves`.

## Only this playthrough's saves (2026-11-27)

IDA 1.7.104 and the Journal's own ActionScript (`quest_journal.swf` from `Skyrim - Interface.bsa`, decompiled with
JPEXS FFDec: `SystemPage`, `SaveLoadPanel`), read for "in a game, Load lists only the playthrough's saves".

- **What the Journal shows.** LOAD calls the engine's `LOAD` (52924): the current character's saves (state 1). When
  there are none it opens the character list instead (`CHARACTER_LOAD_STATE` 16, then 17), and from the save list
  the bottom bar's "Character Selection" (T, gamepad Y) opens it too.
- **The character list.** `PopulateCharacterList` (52907) posts message 5 to `UISaveLoadManager` (52882): a save-thread
  scan of the characters (request `0x50000008`, 35794), then message 6, `FillCharacterList` (52890). That clears
  the list, pushes "Show all saves" (`$ShowAllSave`, id 4294967295), and then one row per entry of the manager's
  character map (`+0x78`, {id, flags} -> name; `PushCharacterRows` 52917). Then it calls `onFillCharacterListComplete`.
  The map gets a row only for a nonzero id (35799) and is not cleared between scans. Picking a row
  (`SelectCharacter` 52919) sets the display character and builds that character's list.
- **The filter.** Every save list is `BuildSaveList` (35743) with `{id, flags}`: flags bit 1 filters by id, bit 2 is
  the modded flag (`SaveListFilter__Matches` 35924), bit 0 picks the folder. 52892 passes the display character's,
  -1 for "Show all saves".
- **Ours** (`engine/SaveListHooks.cpp`): `JoinService` sets a `SaveList::Limit` each update: every playthrough at the
  main menu, the current character in a game, nothing for a guest in a session. The 35743 hook replaces the filter
  with `{currentCharacterId, 2 | modded}` (and for a guest, empties the list, 35746), so any row picked lists this
  playthrough. The 52917 hook drops the rows of other characters and "Show all saves" before Flash sees the list.
  A console `load` of another character's save is refused in chat (`SaveService::OnLoadRequested`).
- **Measured** (scenario `load-lists-only-playthrough`, another character's save planted in the host's folder):
  vanilla (`disable_hooks=SaveListHooks`) lists the rows `FFFFFFFF` and `0C0FFEE0`; with the hooks none (the duo's
  QASmoke character is id 0, which never has a row), and Load still lists the current character's saves.

## Loading while dead (2026-11-16)

IDA 1.7.104, read for the wipe (the host chooses the save, `../../design/death.md`).

- `MenuOpenHandler::ProcessButton` (52249): `playerDyingOrDead` is ActorState1's life state (Actor `+0xC8` bits
  21-24) dying (1) or dead (2). It gates the Tween menu, Wait, Map and the inventory/magic/skills shortcuts, not the
  Journal/Pause buttons (UserEvents `+96`/`+128`): a dead player opens the Journal and its System -> Load.
- `QuickSaveLoadHandler::ProcessButton` (52251) queues quickload (`0xD0000400`, at `0x140961E6A`) once `BGSSaveLoadManager::CanLoadGame`
  (35736) allows it: false while the player's `Actor::someRefrHandle` (`+0x1F0`) names a live reference, a
  UI `+0x170` count is nonzero, or 82076 (a UI check, not read) says so. No life-state check in what was read. Quicksave's gate is 35734 (not read further).

- `Save_Impl` (35727) also asks `UI::IsSavingAllowed` (82077, RVA 0x1169700, called at 0x140622917; read 2026-11-24):
  every open menu must have `kAllowSaving` (0x800; `UI::numAllowSaving` +0x16C against the menu stack's size +0x120).
  With the Dialogue Menu open no save happens (measured; its flags not read), a join's transfer save included
  (`dialogue.md`, "A guest leaving mid-line").

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

## The file's compression, and when a save reaches disk (2026-11-16)

IDA 1.7.104, read for save sync (`docs/design/join-flow.md`, the world). Format names from UESP's "Save File Format".

- **Layout.** `TESV_SAVEGAME`, u32 header size, the header (its last u16 is `compressionType`), the screenshot
  (width × height × 4 from version 10, raw), then the body. `BGSSaveLoadFile__WriteHeader` (35672) writes all of it up
  to the body and returns the body's offset (`file+0xBC4`); `BGSSaveLoadFile__ReadHeader` (35673) reads it back and
  copies the type to `file+0xBC8` (`BGSSaveLoadFileEntry__GetCompressionType` 35542, entry `+0x66`).
- **Saving.** `Save_Impl` (35727) sets `file+0xBC8` from the setting `uiCompression:SaveGame`
  (`g_uiCompression_SaveGame`, 0 none, 1 zlib, 2 LZ4). `BGSSaveLoadFile__CompressBody` (109333) then replaces the raw
  body with u32 uncompressed length, u32 compressed length and the compressed bytes; for type 0 it returns and the
  body stays raw, with no length fields.
- **Loading.** `BGSSaveLoadFile__DecompressBody` (109334), called by the load (442580) after the header and
  screenshot: type 0 returns true at once and the body is read as it lies; 1 and 2 read the two lengths and decode;
  any other type fails the load. **So a file with type 0 and a raw body is a valid save**: what a guest writes
  from a synced save. The file location table's offsets already count as if the body were raw.
- **When the `.ess` is on disk.** Device 2 compresses and writes inside `Save_Impl` (35675, 109333, the save-data
  utility's vfunc `0x68`, `BSWin32SaveDataSystemUtility__WriteSaveFile` 109353). The file is made by
  `BGSSaveLoadManager__CreateSaveFile` (35724) as `<name>.ess.tmp`; 109353 writes it, then sends its event
  (`BSWin32SaveDataSystemUtility__SendEvent`) to `BGSSaveLoadManager__OnSaveDataEvent` (35773), which moves the old
  file to `.bak` and renames the `.tmp` into place, all before 109353 returns. Devices 3, 4 and 5 queue a `bgs::saveload::SaveOperationRequest` (id `0x40000001`, 35791)
  and the save thread runs the same three calls later in `BGSSaveLoadManager__Thread__RunRequest` (35794). SKSE's
  `.skse` is written inside `Save_Impl` in every case. A quicksave or autosave is therefore not readable when
  `Save_Impl` returns: 35794 is where it becomes so.
- **Measured, six saves of one session (2026-11-16, ~3.0 MB files, 7.9-8.4 MB raw bodies):** the LZ4 files share
  3-23% of 16 KiB content-defined blocks; the raw bodies, with the earlier save as a zstd prefix (level 9, long
  window), differ by 0.2 MB in one place and 1.1 MB after a move from an inn to the city. A raw body alone is 1.55 MB
  in zstd against 2.8 MB in the engine's LZ4.
