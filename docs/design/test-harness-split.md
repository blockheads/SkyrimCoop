# Split the test harness out of the shipping client

**Status: done (2026-10-11), uncommitted for review.** Where things ended up:

- `Code/client/TestHarness/`: `TestHarness.{h,cpp}` (the only entry points: `Configure`, `Poll`,
  `Shutdown`), `Harness.h` (its state), `CommandServer`, `Commands.{h,cpp}` plus
  `CommandsStaging/Input/World/Session.cpp`, `Status` (reads the copies itself: `status["actors"]`
  comes from the world's `RemoteComponent` entities, no hook in `CharacterService`), `ControlFiles`,
  `PlayerInput` (held buttons, equip-animation count), `Session`, `EngineCalls` (the old
  `HotReload/Harness`, plus console, menus, the player activating and locking objects and the cell's
  objects, which left `IFormBridge` and `TESObjectREFR`).
- `Code/client/HotReload/`: `PluginUnload.cpp`, `Handoff.{h,cpp}`. `Handoff.h` compiles to no-ops
  without the harness, so `PapyrusService`, `LockOrder` and `SkseRipAllocator.h` are unchanged.
- `Code/client/PluginPaths.{h,cpp}`: the plugin directory and instance suffix (production).
- The movement-module reload is gone: `InterpolationSystem::Interpolate` (same math) is called
  directly; `MovementLoader`, `SkyrimCoopMovementCore`, `scripts/hot_reload.py`,
  `scripts/launch_dev_game.sh`, `HOT_RELOAD.md`, their tests and `request.txt` are deleted. The status
  file is written every 250 ms without it. The unused `cast` command is deleted.
- Build switch: xmake option `test_harness` (root `xmake.lua`, default off) defines
  `COOP_TEST_HARNESS` 0/1. On: `TestHarness/**` and `HotReload/**` are compiled and the
  `SkyrimCoopLoader` target exists. `scripts/xmake-msvc.sh f` passes `--test_harness=y`;
  `coop_test.py` refuses a DLL without the harness.
- Verified: the suite before (27/38, fresh duo at 088d78da) and after (28/38 once, then 3× shuffled)
  fails the same real bugs. A build with `--test_harness=n` has none of the harness's strings or
  exports, and booted in the game it opens no socket (under Wine the listener shows up as
  `wineserver`) and writes no status. `coop_test.py game` passes.
- Found on the way: **door-opens-for-both fails whenever lock-reaches-others ran first** in the same
  session (any shuffled run). The unlocked door keeps lock level 50, and `ObjectService::OnActivate`
  skips doors with a nonzero lock *level* instead of locked ones, so the activation is never relayed.
- Release mode (`-m release`) does not link with the clang-cl cross toolchain (xmake passes
  `/opt:ref` to the archiver); the player-build check used `-m dev --test_harness=n -o build/player`.

The rest of this page is the plan as handed over.

Handoff for the agent doing this work (written 2026-10-11). Read this whole page first, then
`CLAUDE.md` and the memory index. Ask the user before committing anything; they review every change.

## Why

Test and dev tooling has grown inside the client that players run, and every new test feature
widened production code to fit:

- `Code/client/HotReload/Runtime.cpp` (~780 lines) mixes the test command server, the status JSON
  the harness polls, control files, the whole-DLL reload, the old movement-module reload, and the
  plugin's paths.
- **It all runs in every build.** `Runtime::Poll` (called each frame from `SkyrimVM64.cpp:46`) starts
  a loopback TCP command server and polls control files every 250 ms. On a player's machine any
  local process can run console commands, press the player's buttons, kick players or disconnect the
  session (`HandleCommand`: `console`, `input`, `cast`, `strip`, `place`, `kick`, `disconnect`, ...).
- Test-only engine calls kept landing in the production bridges (`IActorBridge`, `IPlayerBridge`)
  with `ReplayBridge` stubs. They were moved out on 2026-10-11 to `HotReload/Harness.{h,cpp}`, which
  is still the wrong place.
- The harness's view of other players' copies is fed **from production movement code**:
  `CharacterService.cpp` (~line 1446) calls `CoopReload::Runtime::Get().Evaluate()` and `.Observe()`
  inside the remote-movement loop, and `scripts/coop_test.py` `actors_on()` reads exactly that
  (`status["actors"]`).

The user's concern: "we keep running into tech debt over and over that we are resolving." The fix
is structural: test code gets its own home and its own build switch, so it can't creep back.

## Goal

1. A release build of the client contains **no** test harness: no command server, no control
   files, no harness commands, no harness engine calls, no DLL-swap reload.
2. Test builds behave exactly as today: `scripts/coop_test.py` (net, game, duo, scenario, reload,
   console, status, crash, watch) works unchanged, or with matching harness edits.
3. Production code calls no harness code. The game loop and startup get one `#if` hook point each.
4. No behaviour change for players, apart from the harness disappearing.

## Current inventory

`Code/client/HotReload/` (client target, `Code/client/xmake.lua` globs `**.cpp`):

| File | What it is | Goes to |
|---|---|---|
| `CommandServer.{h,cpp}` | Loopback TCP JSON command server | test harness |
| `Runtime.cpp` `HandleCommand` | ~20 commands | test harness, split by area |
| `Runtime.cpp` `BuildStatus`, `Snapshot`, `WorldSnapshot`, `PlayerStatus`, `GameStatus`, `LocalActors`, `OpenMenus`, `LockOrderStatus`, `FrameStatus`, ... (~450 lines) | The status JSON the harness reads | test harness |
| `Runtime.cpp` `PollBootCommand`, `PollConsoleCommand`, `CloseTutorialPopups`, control dir `SkyrimCoopHotReload<suffix>`, `boot.txt`, `console.txt`, `request.txt`, `status.json` | Fast boot into a cell, console lines, status file | test harness |
| `Session.{h,cpp}` `PollSession` | Host/join driven by `session.txt` | test harness |
| `Harness.{h,cpp}` | The harness's engine calls (player input via `ButtonEvent`, face/place, caster state, graph booleans, character state, HUD target, visuals) | test harness |
| `Runtime.cpp` held buttons + equip-animation counter in `Poll` | Per-frame parts of the input injector | test harness |
| `PluginUnload.cpp`, `Handoff.{h,cpp}` | Swapping a rebuilt DLL into a running game (`coop_test.py reload`, with `Code/hotreload/Loader.cpp`, the `SkyrimCoopLoader` resident plugin) | dev reload, same build switch |
| `Runtime::Evaluate/Observe`, `MovementLoader` (`Code/hotreload/MovementLoader.cpp`, compiled into the client), `Code/hotreload/MovementCore.cpp`/`MovementMath.h`/`MovementApi.h`, target `SkyrimCoopMovementCore`, `scripts/hot_reload.py`, `request.txt` module names | The original hot-reloadable movement module. **The reload part appears unused**: the harness only writes `request.txt = builtin`, to switch on the status file. Verify, then delete the reload and call the movement code directly | production movement (direct) + delete the reload |
| `Runtime::PluginDirectory()`, `CoopReload::InstanceSuffix()` | Plugin paths; used for the log (`TiltedOnlineApp.cpp:40,45`) | production: a small plugin-paths utility |

Production call sites into all of this (find them with `grep -rn "CoopReload::" Code/client | grep -v HotReload/`):
- `skse_entry.cpp:135` `Runtime::Configure`, `:144` `SetUITaskQueue`
- `SkyrimVM64.cpp:46` `Runtime::Poll` (each frame)
- `TiltedOnlineApp.cpp:40,45` paths and log name, `:93` `Runtime::Shutdown`
- `Services/Generic/CharacterService.cpp` ~1446 `Evaluate`/`Observe`/`ValidMovement`
- `Services/Generic/PapyrusService.cpp` and `Games/Skyrim/BSCore/LockOrder.cpp`: `TakeHandoff`/`HandOff`
  (they keep state across a DLL swap; in a release build these become no-ops or go away)

Also test-only, outside `HotReload/`:
- `Ui/UiService.h` `GetPartyMembers()` / `IsPartyPanelShown()` (const getters the harness reads).
  Acceptable as service API; leave them unless they get in the way.
- The `cast` command still uses `CastSpellImmediate`, which skips `ActorMagicCaster::SpellCast` (the
  cast sync never sees it). Scenarios no longer use it for the player: delete it, or keep it only
  for NPC casters if a scenario needs that.
- `Games/Skyrim/BSCore/LockOrder.cpp` hooks every engine `BSReadWriteLock` (68233/68234/68239/68240)
  in every build. It is diagnostics; decide with the user whether it belongs under the same switch.
  It currently finds real bugs in scenarios, so the test builds must keep it.

## Target layout

```
Code/client/TestHarness/          only in test builds (xmake option, see below)
  CommandServer.{h,cpp}
  Commands*.cpp                   HandleCommand split by area: staging, input, session, world, ...
  Status.{h,cpp}                  the status JSON; reads copies itself (see "Observe" below)
  ControlFiles.{h,cpp}            boot.txt, console.txt, session.txt, status.json
  EngineCalls.{h,cpp}             today's HotReload/Harness.{h,cpp}
  TestHarness.{h,cpp}             Configure / Poll / Shutdown: the only entry points production calls
Code/client/HotReload/            dev DLL swap only (PluginUnload, Handoff), same switch
Code/client/PluginPaths.{h,cpp}   PluginDirectory(), InstanceSuffix() (production)
```

Names are suggestions; match the codebase's style (`CODE_GUIDELINES.md`, `CLAUDE.md` naming).

Build switch: an xmake option (e.g. `test_harness`, define `COOP_TEST_HARNESS=1`). It is on in the
configs the scripts use (`scripts/xmake-msvc.sh`, `build/msvc-config`) and off for release packaging.
Production code reaches the harness only through `TestHarness::Configure/Poll/Shutdown`, each wrapped
in `#if COOP_TEST_HARNESS` at its one call site, or called through a header that compiles to inline
no-ops without the option. The harness's `.cpp` files are added to the target only with the option
on, so a release build cannot even name them.

The user chose a compile-time switch over a runtime flag: then a player build never contains the
socket.

### Observe: the harness must stop depending on production movement

Today `status["actors"]` is filled by `Runtime::Observe` from inside `CharacterService`'s movement
loop: the requested position, the actual engine position, has-3D, generation and so on, per remote
actor. Replace this with the harness reading the copies itself when it builds the status: iterate
the world's `RemoteComponent` + `FormIdComponent` entities (or whatever `CharacterService` uses) and
read positions through the bridges. Keep the JSON fields `coop_test.py` uses (check every
`actors_on`/`wait_on` predicate; `requested`, `generation` and `observed_ms` may be unused once the
movement-module reload is gone). Removing this hook also removes the last harness call from a
service.

## Steps (keep the suite green after each; they can be separate commits)

1. **Baseline.** Run `scripts/coop_test.py scenario` once and record the pass/fail list. Today 26 or 27 of 37 pass;
   the failures are real bugs listed below. That list is the reference: the restructure must not
   change it.
2. **Plugin paths.** Move `PluginDirectory`/`InstanceSuffix` out of `Runtime` into
   `PluginPaths`, and update `TiltedOnlineApp.cpp` and `skse_entry.cpp`.
3. **Movement reload.** Confirm nothing uses the movement-module reload: grep `scripts/` for
   `request.txt` writes other than `builtin`, and for `hot_reload.py` users; ask the user if
   unsure. Then `CharacterService` calls the built-in movement (`MovementCore`) directly. Delete
   `MovementLoader`, the `SkyrimCoopMovementCore` target, `scripts/hot_reload.py` and `request.txt`
   handling. The status heartbeat must no longer need `request.txt`; drop that write in
   `coop_test.py` (~line 476).
4. **Observe → harness-side read** (above). Remove the `Observe` call from `CharacterService`.
5. **Create `TestHarness/`** and move the command server, commands, status, control files, session
   control and `Harness.*` there. Split `HandleCommand` by area. Pure moves first, in a commit of
   their own, then tidy.
6. **Build option.** Add it, gate the files and the call sites, and make the DLL-swap reload
   (`PluginUnload`, `Handoff`, `Code/hotreload/Loader.cpp` deployment) part of the same switch.
   Check `Handoff` users (`PapyrusService`, `LockOrder`) still build without it.
7. **Verify a release build.** Build with the option off. Check the DLL has no harness: no
   `CommandServer` symbols or strings (e.g. `strings` on the DLL for `"graph_bool"`,
   `"SkyrimCoopHotReload"`, `"console.txt"`), and nothing listening when the game runs.
8. **Docs.** Update `CLAUDE.md` (Testing section, project structure), `docs/re/systems/input.md`
   (paths), the `scripts/coop_test.py` docstring, and this page (mark it done or delete it).
9. **Full suite** with `--repeat 3 --shuffle`: the same pass/fail list as step 1.

## Commands

- Build the client: `scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL`
- Native bridge tests: `XMAKE_CONFIGDIR=build/hotreload-native-config xmake build BridgeTests && build/linux/x86_64/release/BridgeTests`
- Swap a build into the running games: `scripts/coop_test.py reload`. Memory grows ~70 MB per
  reload; when the games get flaky, run `scripts/coop_test.py stop`, then `scripts/coop_test.py duo --keep-running`.
- Scenarios: `scripts/coop_test.py scenario [names] [--tag x] [--repeat N --shuffle] [--fail-fast]`.
  While iterating run only the affected ones, then the full suite once at the end.
- After changing `POINTER_SKYRIMSE` IDs: `python3 scripts/re/build_re_data.py`

## State at handoff (branch `clang-cl-port-e2e-tests`, the two commits after 1dd5628c, not pushed)

Done in the session that wrote this page:

- **RE references.** `scripts/re/fetch_references.sh` now also fetches CommonLibSSE-NG and five SKSE
  plugins into `reference/plugins/`; `build_re_data.py` tags their IDs ("Used by plugin"). New
  `scripts/re/relib.py` (meh321's per-version offsets), new pages `docs/re/systems/input.md` and
  `docs/re/systems/actor-values.md`, names in `docs/re/ida_notes.tsv`.
- **Hook fixes** (all three were inherited from upstream Tilted Online):
  - The regen hook was on 37448 (`GetSubmergedLevel`); it is now on 38459
    `Actor::RegenerateActorValue`, and copies don't regenerate.
  - `UnEquipShout` called 38903 (unequip by slot) with no slot; it now calls 38904
    `ActorEquipManager::UnequipShout`.
  - `HookProcessResponse` dropped the engine's 5th argument; it now passes it on.
- **Layouts.** `PlayerControls::Data` is at 0x24 on AE (was 0x20). `MagicCaster::State` holds
  CommonLib's names (the old enum held actor-value names).
- **`ProcessLists`.** Typed `magicEffects`/`magicEffectsLock`, `GetPlayingVisualFormIds`,
  `StopEffectShader`/`StopArtObject` (`Games/TES`), and new `Games/Skyrim/Effects/ReferenceEffect.h`.
- **Owners always send magicka and stamina**, like health (`ActorValueService`): copies no longer
  regenerate, so drift from their replayed casts has to be corrected.
- **Input injector.** `Harness::SendPlayerButton` builds a `ButtonEvent` and sends it through
  `BSTEventSource<InputEvent*>::SendEvent` (68655), the same dispatch real input uses. While a button
  is held, `Runtime::Poll` resends it every frame (needed for melee). The harness helpers in
  `coop_test.py` (`press`/`release`/`tap`, `draw`, `sheathe`, `attack`, `cast_spell`) wait for game
  state, not time. See `docs/re/systems/input.md` for what it took.
- The arrival fix and the scenario rewrite from before this session (see git diff of
  `CharacterService.cpp` and `scripts/coop_test.py`).

Real bugs the suite exposes (keep them failing loudly; not part of this work):
- **Copy stuck on magic equip.** After the owner's spell equip or release, the copy's last animation
  event is `Magic_Equip`/`MagicForceEquip` while the owner's is `MRh_Equipped_Event`/
  `MRh_SpellRelease_Event`. Seen in flames-client-hits-host-npc, client-angers-host-ally and
  burning-shows-on-copy; intermittent.
- **Dead NPC flags.** A dead NPC's `animation.state1` differs on its copy (death-host-npc,
  client-spell-kill-credit).
- **Lock-order inversion** during cell changes (party-changes-cells, weather-follows-host).
- **load-door-not-replayed**: the copy refuses the owner's `Unequip` after a teleport.
- **client-melee-kill-credit**: the 2nd swing starts but doesn't land. Not investigated.
- **door-opens-for-both, late-join-client**: not investigated.

## Rules going forward (put the lasting ones in CLAUDE.md as part of step 8)

- **Test code never widens production interfaces.** No harness methods on the bridges, services or
  engine structs. The harness reads engine state through its own `EngineCalls`, and production
  code never calls the harness except through the gated entry points.
- **Engine access goes through typed structs** in `Code/client/Games/` with `static_assert`ed
  offsets, checked against CommonLibSSE-NG and IDA. No raw byte offsets in services, bridges or
  the harness.
- **Before reversing anything, search GitHub/Nexus** for an SKSE mod that already does it, and add
  good ones to `scripts/re/fetch_references.sh`.
- **When adding or touching a hook,** check our hook's signature against the community name and
  the disassembly. Three upstream hooks were silently wrong.
- **Scenarios drive the game as a player would** (input through the engine's dispatch) and wait
  for game state, never sleeps or retries. A failure names the step that went wrong.
