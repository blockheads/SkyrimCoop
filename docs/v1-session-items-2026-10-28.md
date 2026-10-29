# V1 session and item work — 2026-10-28

Scope: remove old debug shortcuts, finish ordinary session exits, prevent duplicate container takes, and test knowledge across save/rejoin. The subsequent authorized bug-fix pass fixes guest knowledge persistence and corpse looting. This work starts at dev `e4085691`. It does not include the uncommitted dialogue, story, scene or vampirism work in `.worktrees/party-scripts-2`.

## Implemented

- Removed F6's direct connection toggle, F9's hosting toggle, empty F7/F8 handlers and the unused actor-placement helper. Removed the older F2/Right Control input-capture shortcuts too. The F3 code remains; repairing or verifying it is outside this work.
- `SessionService` now owns intentional shutdown. Native Quit to Main Menu cleans up before world teardown. Explicit leave, kick, host ending the session and exhausted reconnects clean up transport, hosting, Steam tunnels/lobby and join state, then use Skyrim's main-menu transition. Existing join notices give the reason. Loading a save and temporary connection loss retain their existing paths. No new leave UI or autosave was added.
- A host's intentional exit carries ENet's new `SessionEnded` disconnect reason. An unexpected lost connection still uses the existing bounded reconnect loop.
- Guests' native container-menu transfers request a grant from the host. The host transfers only the stock actually present, between the container and the guest's copy, then sends the actual amount to the owner and other copies. Browsing is not locked. Partial takes, storing, actor inventories, extra data and the shared purse use the existing inventory/world-state paths. Native menu restrictions still run before the transfer request.
- Guest character records now carry learned ingredient effects, ingredient use counts and known enchantments. Joining applies those after loading the host's world, replacing the host's knowledge with the guest's own snapshot. An explicitly empty snapshot differs from an older record without one. Older records remain readable; knowledge already lost by an older build cannot be reconstructed.
- A copy that reaches its death ragdoll now completes the native dying-to-dead transition. Native NPC activation rejects an actor still dying, which prevented corpse looting. The completion waits for a ragdoll and excludes an active kill move.
- Read-only harness knowledge queries and native scenarios learn through eating/disenchanting; persistence tests wait until the host has received the guest's updated record before saving.

## Evidence

Before the fixes, fast tests reproduced guest/host Quit and kick leaving the session active, and both a host/guest pair and two guests obtaining two swords from a chest containing one. Logs are in the implementation worktree's ignored build directory (`exits-red.log`, `container-red.log`).

`scripts/check.sh` passes: ClientTests (738 assertions), HostAuthorityTests (1902), encoding tests (287), GameTests (1985), harness/ragdoll comparisons, UI fixtures and the Windows plugin build. The added flows cover intentional exit/rejoin, terminal reconnects, concurrent takes, partial takes/storing, shared gold and NPC inventory transfers. Knowledge tests cover serialization through the host's co-save, old record compatibility, bringing a character's knowledge, later learning/rejoin, and a new character with no knowledge inheriting none of the host's.

Native two-game assertions passed for:

- Guest System > Quit > Main Menu, followed by a join.
- Host System > Quit > Main Menu: the guest returns to the main menu with the host-left reason; loading the checkpoint and joining works.
- Kick and explicit leave, followed by joining from the main menu.
- Take All and shared container gold.
- Taking a tempered, enchanted, named sword through the native chest menu, retaining its extra data on the owner and the host's copy.
- The guest opens a corpse's menu and uses Take All; the host's corpse and its guest copy lose the granted items exactly once.
- Guest ingredient and enchantment knowledge survives a host save and a fresh guest process joining that world.
- On a fresh duo, the quest-death scenario still runs its script on the host only, and the host's kill move remains paired on the guest with the victim dying on its timeline (`death-regression.log`).

These native assertions also ran directly through the scenario functions. The general scenario runner reported seven QASmoke `CoverPlate01` pose differences after save loads; those broad world comparisons are not counted as passes. The existing container extra-data storage scenario passed.

The isolated test build/prefixes are under `build` in `.worktrees/v1-session-items`. Follow-up logs: `bugs-check.log`, `bugs-native-final.log`, `knowledge-red.log`, `knowledge-games.log`. Earlier logs: `check-reviewed.log`, `v1-direct.log`, `native-extra-leave2.log`, `knowledge-final.log`, `knowledge-restart-controls.log`. `scripts/net_check.sh` was not run. Real Steam accounts and a 3–4-player Skyrim run remain playtest checks.

## Knowledge persistence fixed in the follow-up

| Native action | Before restart | Original result | Fixed result |
| --- | --- | --- | --- |
| Guest eats Blue Mountain Flower (`00077E1C`) | first effect known (`1`) | unknown (`0`) | known (`1`) |
| Guest eats Wheat (`0004B0BA`) | first effect known (`1`) | unknown (`0`) | covered by the ingredient snapshot path; native fixed run used Blue Mountain Flower |
| Guest disenchants Fire Damage (`00049BB7`, base `0010FB95`) | known | unknown | known |

Both original host controls retained knowledge after restarting Skyrim from the host's own saved file: Wheat's effect stayed known and Fire Damage stayed known. The guest-only tests check that the host has not learned the chosen knowledge before saving. The two formerly failing guest persistence scenarios now pass.

## Reload freeze still unresolved

A host reload can initially restore both players and then freeze a few seconds later. The selected-save scenario's character/clock/position checks passed, but the subsequent quest-death scenario could not obtain a session id for a new actor because the host had frozen.

HangWatchdog puts the main thread in `SkyrimSE.exe+0x643F48`, waiting for the VM update job. That job is stuck in MSVC's map `_Tree_val::_Min` while copying actor values into a `NotifyCharacterSpawn`, through `CharacterService::BuildSpawnRequests`, `HostCharacters::BuildSpawn/CharacterPlace` and `HostFollowers::Update`. A GDB read confirms the map's sentinel has lost its nil flag and its left pointer loops to itself. This is a memory-corruption symptom; its originating write has not been established. No reload fix is claimed. Evidence: `host-reload-hang.log`, `reload-tree.txt`, `reload-map-nodes.txt`.

## Native bindings and integration

The new quit hooks are the native-menu and Papyrus `FadeThenMainMenuCallback::Run` functions, IDs `53290` (RVA `9909A0`) and `55646` (RVA `A15F20`) in Skyrim 1.6.1170. Both disassemble to setting Main's return-to-menu byte and returning; the callback is `void(void*)`. Returning programmatically queues Papyrus Game's `QuitToMainMenu`, ID `55490` (RVA `A0D1B0`), whose body reads no arguments and schedules that fade callback.

The existing removal hooks retain their signatures: `TESObjectREFR::RemoveItem` (`19689`, RVA `2E1750`) and `InventoryChanges::RemoveItem` (`16059`, RVA `228180`). Checked against CommonLibSSE-NG and the unpacked executable. The IngredientItem discovery field is a typed, offset-asserted member at `0x138`; the read-only harness observes it changing on native eating. Enchantment discovery reads TESForm's `kKnown` flag (bit 6), following the base enchantment.

Knowledge uses TESDataHandler's typed form arrays: ENCH (`0x15`) at `0x208`, INGR (`0x1E`) at `0x2E0`, checked against CommonLibSSE-NG. Applying ingredient changes marks `kIngredientUse` (bit 31); enchantments call `TESForm::SetPlayerKnows` (`14639`, RVA `1E0A90`), whose disassembly updates bit 6 and adds change flag 1 without a popup. Copy death completion calls `Actor::SetLifeState` (`37612`, RVA `680740`, `void(Actor*, uint32_t)`); disassembly and CommonLib agree on the argument. GDB traced corpse activation through `TESNPC::Activate` (`24715`) into `37559`, which rejects life state 1.

When integrating the other worktree, retain its routing/dialogue changes. Shared edit points include the new production `IJoinBridge` methods and fake, HostGame's container-transfer callback and its fake seam, and the scenario registration/rejoin changes in `coop_test.py`. Knowledge's native implementation is isolated in `JoinKnowledge.cpp`; the follow-up did not edit `JoinBridge.cpp` or CoSave. Actor.cpp's addition is only copy death upkeep, outside the skills/spawn work. The new scenarios live in `coop_v1.py`. No party-script design documents or progress rows were edited.

## Stability follow-up

The animation variable paths now check both word-array and metadata bounds, enforce scalar types, and invalidate cached descriptors when the graph project changes. Beast effects and reversion invalidate rather than guessing the next graph before it is installed. Acquired graph managers use scoped references, released after their locks; final release uses the native deleting destructor. F3 is retained with its missing reference release corrected; its UI is not an acceptance target.

Casting accepts absent actors/extensions and the native current-spell fallback; failed or unresolved projectile launches preserve the native result and emit no network event. Placed spawns and refreshes wait for the native `kStillLoading` flag to clear, woken by the reference's 3D load (`ReferenceLoadedEvent`; the flag clears first, `docs/re/systems/form-state.md`). The latest queued spawn wins, and removal or session exit cancels it.

A reproducible werewolf reversion defect was also fixed: `SwitchRace` recalculates the copy's values, so the existing copy must restore its owner's latest values afterward. The cache now retains later skill changes as well as pools, preventing restoration of stale spawn-time skills. Fast tests fail without these fixes. Initialization deferral, refresh, replacement, cancellation, scalar layout/type mismatches, null casting context and failed launch results have fast coverage.

Evidence is retained in `.worktrees/v1-stability/build/stability-*.log`; the key logs are mirrored under dev's `build/check/v1-stability/`. Native results:

- Both Fus Ro Dah directions pass through the normal scenario runner, including the actual three-word cast, knockdown on the victim's own game and its copy, displacement and recovery of walking control. Three consecutive paired runs passed; the stricter horizontal-displacement checks also pass.
- Ordinary host/guest Flames casting and interruption, Firebolt hitting the other player, and bow shots damaging the other player and updating its copy all pass on the production DLL. The final combined-build shouts/casts pass in `stability-final-native.log`; bow smoke evidence is `stability-bow-smoke.log`.
- Three host and three guest Beast Form/reversion cycles, interleaved with three host reloads, pass with temporary allocation diagnostics enabled (`stability-tracked3-cycles.log`). A subsequent production-DLL run passes both transformations, both shouts, both concentration casts, both Firebolt hits and a reload (`stability-production-native.log`).
- On the combined dev base `722b934d`, the production DLL passes three host transformations, three guest transformations and three interleaved reloads (`stability-final-cycles.log`). The directly granted QA power now initializes C00's original-human-race property, matching the prerequisite normally established by the Companions. A missing property had made one QA guest finish its reversion quest while still a werewolf; this fixture setup does not claim end-to-end coverage of acquiring Beast Form through the Companions.
- Reload acceptance now requires both games to advance frames after restoration and a new actor to spawn and reach the guest. This closes the former scenario's blind spot.
- Placed Riverwood wildlife takes its owner's base and remains loaded; travel through the inn door works in both directions with the host's NPCs appearing afterward (`stability-spawn-native.log`).
- The merged `scripts/check.sh` gate passes: ClientTests 101 cases, HostAuthorityTests 181, TPTests 26 and GameTests 203, plus the harness/ragdoll comparisons, UI fixtures and Windows DLL build. Dev's own Windows DLL also builds (`build/stability-dev-build.log` on dev).

**The save/reload corruption remains unresolved.** It reproduced on the production DLL during saving, before loading (`stability-save-hang-symbolized.log`, `stability-save-map.log`). The VM job was inserting into a fresh `ActorValues` map whose sentinel had become an ordinary value node. A later repeated-save run instead crashed while iterating `ActorValueService::ApplyCopyPools`' local set (`stability-watch-crash-symbolized.log`). Successful reload repeats do not establish that either failure is fixed. The historical guest graph crash on werewolf reversion has not been independently reproduced on the baseline in this follow-up, so that crash remains open too.

The temporary allocation tracker reported no duplicate live allocation or unmatched free in its final nine-scenario run. A separate Windows probe completed 800,000 actor-value map pairs across eight threads, including cross-thread destruction, without errors. These diagnostics do not identify the originating write or rule out a timing-dependent defect; no allocator replacement, speculative recovery, or diagnostic instrumentation is retained. `scripts/net_check.sh` was not run.

Native bindings checked against community layouts and the 1.6.1170 executable: `hkbVariableInfo` size `0x6`, type at `0x4`; graph metadata at `0x88`, its variable-info array at `0x20`; native manager scalar deleting destructor 63411 (`0xBA7200`, flag bit 1 frees the block). Casting hooks retain 34144 (`0x5B1290`), 34140 (`0x5B0B70`), 21622 (`0x332E80`), projectile launch 44108 (`0x7E46C0`) and `MagicTarget::AddTarget` 34526 (`0x5C3250`, data reference in RDX). `papyrus::Game::SetBeastForm` 55497 (`0xA0D3E0`) reads the entering flag from `R9B`; its forwarding signature is unchanged.
