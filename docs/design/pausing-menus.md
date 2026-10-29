# Menus that pause the game

v1.0 feature 15. Built, verified and merged into `dev` on 2026-10-27 from
`.worktrees/pausing-menus`, branch `feature/pausing-menus`.

## Behavior

Personal menus keep the session running (chosen with the user, 2026-10-27).
The host's map, journal, crafting, lockpicking, barter, books and training must not stop its world
or the NPC copies on any guest. A guest's own menus must not stop the player its game simulates,
the other players' copies, or its NPC copies. Menu input still belongs to the menu: an open map
does not let the player move through the world with its navigation keys.

Shared wait and sleep retain their native pause and readiness barrier (`shared-wait-sleep.md`).
Loads retain their native pause and party travel (`host-loads.md`). Offline menus retain native
behavior. This feature adds no party-pause message or vote.

## Existing cause

`Games/Skyrim/Interface/UI.cpp` cleared three flags only for inventory, magic, skills, favourites,
containers, message boxes, tutorial, tween and console. Other pausing menus stopped their game's
simulation. The host owns every NPC: a paused host sends stationary NPCs to every guest. A paused
guest stops its own player's simulation even while the host and the other guests keep playing.
Crafting normally runs unpaused in vanilla; it is included explicitly in the policy so a menu's
pause flags do not decide the party's behavior.

The map and journal were intentionally excluded by old comments about first-person rendering and
manual save crashes. Clearing their flags alone is insufficient. The reference is
[Skyrim Souls RE](https://github.com/Vermunds/SkyrimSoulsRE), already fetched by
`scripts/re/fetch_references.sh`. Its map, book and save fixes were read before checking Skyrim
1.6.1170 in IDA; its older instruction offsets must not be copied blindly.

## Engine checks and implementation

- `MenuPauseRules.h` holds the explicit list and strips only `kPausesGame`,
  `kFreezeFrameBackground` and `kFreezeFramePause`. Engine constants are asserted in `UI.cpp`.
  Names use the engine's case-insensitive comparison. Unknown menus, loading and Sleep/Wait keep their flags.
- The existing call at `UI::ProcessMessages` (82082, `0x140FA3FA0`) +`0x682` is a stack
  search (`0x140FA5CE0`), **not** an insertion. It returns a boolean and takes an output pointer to
  a stack entry. The hook now has that signature and changes flags only before the menu is on the
  stack, keeping the engine's pause counter paired with its eventual removal.
- `Main::UpdatePlayer` (36581, `0x1406474C0`) +`0x7A` calls the player's virtual `Get3D2`
  (`TESObjectREFR::GetNiNode`, slot `0x70`), then only tests the returned value for zero. The
  replacement predicate also permits the update while the session's unpaused map is open, following
  Skyrim Souls' first-person fix. Offline it returns the original predicate. The native first-person
  model (`PlayerCharacter::firstPerson3D`, checked offset `0x8F0`) is hidden for the map's render;
  its previous visibility is restored before the next native player update. A replaced model is
  never dereferenced through a saved pointer. Skyrim exposed visible arms over the map before this fix.
- Switching local view (53112, `0x140986EE0`) sets the background flag and starts a render-frame
  freeze through the call at +`0xC2` (`0x140CC9920`). Its call is skipped for the live map; its
  updater clears that flag before returning. The native controls-readiness and local-map update
  still run. The original return value is preserved. World-map readiness (`0x3058C`) and local-map
  readiness (`0x3045E`) are distinct; local view is `0x3045C`. `LocalMapMenu::Show` (`0x14097BF40`)
  writes the latter two in the embedded local map at `0x60`, as CommonLibSSE describes.
- `MapMenu::ProcessMessage` (53102, `0x140984A80`, the menu's virtual slot 4) handles generic
  per-frame updates without forwarding them to the native marker picker, following Skyrim Souls.
  Updates addressed to the map and its ordinary input messages still reach the native method.
- `BookMenu::ProcessMessage` (51049, `0x1408F0620`) calls the menu-mode predicate at +`0x6F`
  before creating its book model. That single call returns false in a session, allowing the model
  to be created while unpaused. Offline it calls the original predicate. Skyrim Souls' old
  +`0x78` patch is inside a different instruction on 1.6.1170.
- `BGSSaveLoadManager::Save_Impl` (35727) still refuses guest saves through `SaveService`.
  A session's synchronous journal save copies its filename and queues the original call through
  `MainLoopTasks`, between frames, as the join flow already does. An atomic pending flag prevents
  duplicate queued saves; authority is checked again when the task runs. It never serializes the
  live VM from the journal's UI update. Other save callers retain their existing behavior.

## What changes under a live menu (v1 checks, 2026-10-28)

Skyrim Souls RE is the reference: `AutoCloseManager`, `ItemMenuUpdater`, `SaveHandler`.

```text
a chest's or merchant's menu (LiveMenuHooks.cpp):
  kShow (ContainerMenu::ProcessMessage 51126, BarterMenu 50945): watch its target (handles 405962, 405823)
  each frame (Main::UpdatePlayer 36581; barter gets no kUpdate): MenuAutoClose::Check
    target disabled or deleted, an actor that dies, out of 400 units (or 100 past its closest if opened far): kHide
another game changes the open chest (ApplyContainerItems / ApplyContainerContents):
  SendInventoryUpdateMessage (52849) twice, whole list: the open list follows
the host saves while a guest's menu is open: written, the menu keeps running
```

Tests: ClientTests `menu_auto_close_tests.cpp`; Skyrim `host-/guest-chest-list-follows-changes`,
`host-/guest-menus-close-when-target-goes`, `host-saves-with-guest-menu-open` (`coop_menus.py`). A host's console save
with its own container open is not a player's path (no quicksave or console in that menu); its journal save is
`host-journal-save`.

## Validation

Tier 1: personal-menu pause flags, preserved input/saving flags, case-insensitive names, offline
behavior, and exclusions for loading/shared rest in `Code/tests/client/menu_pause_tests.cpp`.

Tier 3: `scripts/coop_menus.py` registers the `menus` tag (18 scenarios). The harness reads the
actual UI pause count, each open menu's flags, the first-person model's visibility and the map's
view/readiness. Map and journal use keyboard input; books, locks and crafting use native activation;
training and barter invoke the actual natives their dialogue scripts invoke. Every menu checks
the acting player's stamina regeneration and the host's advancing clock while it stays open.
The NPC check observes both the raised NPC and its raised copy before observing their fall with
both journals open. This prevents an old position sample from passing the test without a fall.
Other cases cover a real journal save/co-save, guest save refusal and the offline journal's pause.

Map checks cover both views, hidden first-person arms and restored camera/model visibility.
The map helpers wait for the opening fade, honor the remembered local/world view, and place the
cursor over land before a marker pick. Crafting cleanup waits for the get-up animation to finish
before baseline placement. These engine states replaced assumptions exposed by shuffled runs.

Results (one host and one guest, Skyrim 1.6.1170):

| Check | Result |
|---|---|
| ClientTests | PASS: 49 cases, 534 assertions (three menu cases) |
| HostAuthorityTests | PASS: 161 cases, 1761 assertions |
| TPTests | PASS: 14 cases, 225 assertions |
| GameTests except the baseline plate-carry failure | PASS: 132 cases, 1465 assertions |
| UI fixtures and Windows plugin build | PASS |
| `scenario --tag menus --shuffle` | PASS: all 18, 92.5 s |
| `map-marker-reaches-everyone --repeat 3` | PASS: 3/3, 50.6 s |
| Host/guest wait, host/guest sleep, wait cancellation | PASS: 5/5 |
| Host journal load takes the party along | PASS |
| Guest refuses console, quickload and journal loads | PASS (`guest-cannot-load`) |
| Guest buys training through native dialogue | PASS (`dialogue-picker-trains`) |

The full `scripts/check.sh` gate remains red: the existing plate-carry assertion at
`Code/tests/games/physics_flows.cpp:147` fails (about 75 units behind versus a permitted 20).
Rebuilt **unchanged `dev` at `85560546`** and reproduced that same failure. Its build/run evidence
is `build/pausing-baseline-build.log` and `build/pausing-baseline-physics.log`. The sharded gate
also had a corpse timing failure at line 403; its two isolated cases and the serial 132-case run
passed. No physics production or test code was changed.

`affected --check` reports 21 pre-existing files without scenario tags. Every source changed or
added here has a matching pattern; `menus` is included in the interface pattern.

Evidence: `build/pausing-menu-final.log`, `build/pausing-marker-final.log`,
`build/pausing-regressions.log` (rest and host load), `build/pausing-save-training-regressions.log`,
`build/pausing-client-final.log`, `build/pausing-games-without-baseline-failure.log`,
`build/check-pausing-menus-final.log` and `build/pausing-plugin.log`. Earlier map test failures in
the regression log predate the final view/fade/cursor helpers; the final menu and marker runs
above pass. The duo was stopped after validation. Merged locally into `dev`; no push was made.
