# Progress

Where SkyrimCoop stands. Update it when a phase, system or known failure changes.
Plans live elsewhere: the migration in `docs/design/host-authority-migration.md`, later ideas in
`docs/ideas.md`, engine findings in `docs/re/`.

**Goal:** two to four friends play Skyrim together, the host's game is the only authority (direct
authority), and the Skyrim Together server is gone. Peers own only their own player; the host owns
every NPC and the world.

Last updated 2026-10-13.

## Direct authority migration

Porting Skyrim Together's server to the host's game (`docs/design/host-authority-migration.md`).

| Phase | What | Status |
|---|---|---|
| 0 | Ground truth: message table, backbone scenarios, harness `disconnect`/`game` status | done (`ab302206`) |
| 1 | The host talks to the embedded server in-process, no socket | done (`a76aeddd`) |
| 2 | `HostBridge`, movement relay, late join from live state | done (`e49e7aea`) |
| 3a | Relay families as bridge rules: inventory, actor values, magic and combat, character extras | done (`84fb7465`, `c5936751`, `cd3dfd0a`) |
| 3b | Character ids, owners, spawns, removal, respawn on the bridge | done (`5e99eae7`) |
| 4 | Host-decided state | partly, see below |
| 5 | Delete `Code/server/`; the enet6 listener moves into the bridge | not started |
| after | Physics sync on the bridge's unreliable channel | not started (own plan) |

### Phase 4 parts

| Part | Status |
|---|---|
| Time (`HostClock`; set-time from any player) | done (`4e0efb0a`) |
| Weather (`HostWeather`) | done (`50397fa0`) |
| Session = party: roster, host kicks, no party messages | done (`50397fa0`) |
| Cell arrivals (party tether: copies dropped and resent on teleport) | done for every travel: `coc`, doors, fast travel, moves all end in ID 40438 (`docs/re/systems/player-travel.md`; door and fast-travel scenarios uncommitted); the arrival refreshes the host's record of the player (inventory, values, hands) |
| Arriving together: players step aside onto navmesh floor by session slot | done (`83178416`), `party-spreads-on-arrival` |
| Copies converge on their owner (presence): a copy whose owner is around but which is elsewhere, without 3D or deleted is moved or spawned again; a spawn for a known copy refreshes it | done (uncommitted), `party-fast-travels` |
| Shared XP (upstream's party XP, to everyone) | works, `shared-xp` |
| Objects: activation, locks, script animations | done as bridge relays (`31abfd23`, `550ece84`); object ids still from the server (`AssignObjectsRequest`) |
| Quests | not started; quest sync off (`cSyncQuests`) until it has a design |
| Waypoints, chat, player dialogue, overlay health | still handled by the server's `MapService`/`OverlayService` |
| Teleport command | still on the server, reading stale positions; host only |
| Tether itself (pulling a player along, travel requests) | not built: a player who leaves on their own is only logged |

### Still on the embedded server (what phase 4-5 removes)

- Session: `AuthenticationRequest`, connections, version and mod-list check, player list
  (`NotifyPlayerJoined`/`Left`), `StringCacheService`, `PlayerLevelRequest`.
- `ObjectService::OnAssignObjectsRequest` (object ids).
- `QuestService`, `MapService` (waypoints), `OverlayService` (chat, player dialogue, health,
  teleport), `CommandService` (teleport command).
- To delete outright: `ScriptService` (Lua), `AdminService`, server console commands, recording.

## Sync by system

Each verified system has a two-game scenario (`scripts/coop_test.py scenario <name>`).

| System | Status | Scenarios |
|---|---|---|
| Session, join, leave, kick, host rejoin | works | `session-roster`, `late-join-client`, `host-rejoins`, `host-kicks-client` |
| Movement (players, host NPCs) | works | `steady-state`, `move-host-npc` |
| Cell changes (`coc`, doors, fast travel) | works | `teleport-keeps-copies`, `party-changes-cells`, `party-uses-door`, `party-fast-travels` |
| Equipment and hands | works | `equip-*`, `repair-equipment-drift`, `hands-survive-reload-*` |
| Draw / sheathe | works | `draw-*`, `quick-sheathe-*` |
| Sneak, swimming, diving | works | `sneak-*`, `dive-*` |
| Casting | works: copies replay in step with their graph and cast for free | `cast-*`, `input-mouse-cast`, `firebolt-*`, `flames-*` |
| Health | works | `health-*` |
| NPC death and removal | works: a copy dies with its owner's death action | `remove-host-npc`, `death-host-npc` |
| Melee and spells on host NPCs | works, with kill credit | `melee-client-hits-host-npc`, `firebolt-*`, `flames-*`, `client-*-kill-credit` |
| Hostility | works | `client-angers-host-ally` |
| Effect visuals (burning, hit shaders) | works | `burning-shows-on-copy` |
| Furniture (live, while the copy is loaded) | works | `furniture-*` |
| Voice lines, subtitles, lip sync | works | `copy-ends-voice-line` |
| Doors, locks | works | `door-opens-for-both`, `lock-reaches-others`, `load-door-not-replayed` |
| Time, weather | works | `host-time-reaches-client`, `weather-follows-host` |
| Party panel (RmlUi) | works alone; fails after other scenarios | `party-panel` |
| Real input (keyboard, mouse) | works | `input-keyboard-draw`, `input-mouse-cast` |
| Mount, AI package, dialogue, factions | relayed, no scenario | |
| Quests, crime, containers, dropped items, followers, physics | not synced | crime: shared party bounty decided (`docs/ideas.md`) |
| Mods | host's mod list is meant to be authoritative; world state of mods not synced | `docs/design/world-state-sync.md` (parked) |

## Copies (how other players' and the host's actors appear)

Copies are puppets: they run no engine AI and replay what the owner did (`docs/design/copy-contract.md`).

- Done: no packages (`8b464c19`), hands owned by the player (`704a82d6`), hit feedback and effect
  visuals (`1dd5628c`).
- Done 2026-10-12 (`719c2409`..`46bbad21`): the AI update skipped for copies with the body upkeep kept,
  the do-nothing package, the owner's graph state loaded at spawn, sneak/dive/subtitle scenarios.
- Done 2026-10-13 (`f3716725`..`e145b097`, `docs/re/systems/actor-ai-and-copies.md`):
  - Replay in step with the copy's graph update (`HookUpdateAnimationGraphs`, 20123): fixed the magic
    chain (refused `MRh_SpellAimedStart`, the copy left on `Magic_Equip`).
  - Copies cast for free (`kSkipCheckCast` in `CheckCast` and the caster's `Update`): no own magicka spent.
  - Health, magicka and stamina from the owner within 100 ms.
  - A copy dies with its owner's death action (`KillWithDeathAction`), never from health alone.
  - An owner's actions before its id arrives are kept (NPCs falling after `placeatme`).
- Next audits: combat, detection, magic/magicka; then one copy update in place of scattered
  `IsRemote()` checks.

## Test suite

- Travel and presence (2026-10-13, uncommitted): `party-uses-door` and `party-fast-travels` added; those two,
  the session scenarios, `teleport-keeps-copies` and `party-changes-cells` pass (8 of 8). A full run was
  stopped after 26 (25 pass; the failure, `late-join-client`, is fixed: see Tests and harness).
- Last full run (2026-10-13, `5cdf49d2` build): **47 of 47**. Earlier the same day: 42 of 43, then
  45 of 47 with the new scenarios (the two dives, fixed: they had passed only because the arrival fade
  swallowed the swim input). The morning's baseline was 37 of 43.
- Harness (2026-10-13): a player's button waits for the game to take input (no fade or menu);
  `both_go` waits for the arrival's fade; `attack` waits for the last swing to end; `place` takes a
  bearing; lock patterns match in either order.
- Bridge rules: headless `BridgeTests` (`Code/tests/bridge`), wire bytes pinned in `wire_tests.cpp`.

## Loose ends

Everything known to be unfinished, unverified or unexplained. Close an entry by fixing it, proving it
harmless, or moving it to `docs/ideas.md`; say where the evidence went. Details are in the doc named.

### Copies and actor sync

Notes: `docs/re/systems/actor-ai-and-copies.md` ("Still open"), `docs/design/copy-contract.md`.

- **Audits not done:** combat, detection, magic/magicka. Then one copy update in place of the
  scattered `IsRemote()` checks.
- **Upstream AI patches to re-check** (from before copies ran no AI; delete each the cut covers):
  `HookIsFleeing`, `HookProcessResponse`, `HookUpdateDetectionState`, `HookIsHostileToActor`.
- **Cast right after a jump landing** (first person, within ~250 ms): the copy's graph is still
  landing and refuses the cast (3 refusals in the last measurement). Not explained.
- **Interpolation delay 300 ms → 200 ms** (2× the 100 ms snapshot interval, the usual rule; Source
  uses 100 ms at 20/s). Tried 2026-10-13 and reverted: `health-host-npc` failed 2 of 4 runs (the
  copy missed a fresh NPC's `JumpLand`, or had no action at all) and `death-host-npc` 1 of 4, against
  0 of 4 each at 300 ms. Find why a freshly spawned NPC copy loses its first actions at 200 ms, then
  retry. The delay is `RunRemoteUpdates` in `CharacterService.cpp`.
- **Replay retries:** a copy a graph update behind gets 3 more updates to take an action (as the
  owner's input retries). Logged ("graph updates late"); 1 in 8 casts needed it. Watch the count.
- **Graph state at spawn, not yet covered:** behaviour mods (Nemesis/Pandora, Open Animation Replacer)
  untested. The host's record of a player is refreshed on each arrival (graph state, action, inventory,
  values); between arrivals it is the arrival's.
- **Copies refuse `turnStop`** now and then (NPC copies). Not traced.
- **Shared XP** goes to the receiver's last used combat skill only; a player who has not used one since
  loading gets nothing (upstream's rule). Decide whether to keep it.
- **Arrival spacing** needs navmesh under the marker: QASmoke's `coc` marker has none (players stay
  stacked there). Doors and fast travel arrive through the same `RunArrivals`; no scenario asserts the
  spacing there yet.
- **`TESObjectREFR::GetParentCell` (our virtual)** returned null in an exterior after `coc` while the
  field was set: the vtable slot may be wrong. Nothing of ours calls it any more (the harness's object
  list reads the field, `GetParentCellEx`, since 2026-10-13).
- **Copies converge on their owner for presence only.** State that still arrives only as events (dead or
  alive, inventory beyond equipment, the host's roster of characters, object state) stays wrong if one is
  lost. Add each to the same check when a scenario shows it drift
  (`docs/design/host-authority-migration.md`, "Copies converge on their owner").
- **Outdoors the host counts a player "with" it by worldspace:** a player still far off (same worldspace)
  gets the others' spawns there, which the engine deletes; presence recovers it once they meet (an extra
  spawn round trip). Tighten to the loaded area if it costs anything.
- **Graph outputs still synced in creature descriptors:** `NotCasting` in Vampire Lord (the same output
  that ended the player's casts), `currentDefaultState` in steam, falmer, troll, draugr and werewolf, and
  per creature the `IsAttacking`/`bAttached` kind. No harm traced, no scenario covers them. Fix as data
  when each gets a scenario: harness `graph_writers` lists a graph's outputs. Why not a generic runtime
  skip: `docs/design/graph-replication.md` ("Decision").
- **Name-keyed graph variable sync (animation mods): parked.** Inputs and outputs are told apart (harness
  `graph_writers`, `scripts/re/graph_compare.py`); the sync is not built. `docs/ideas.md`,
  `docs/design/graph-replication.md`.
- **`flames-client-hits-host-npc` failed once** (2026-10-12, with the since-reverted copy-side skip): Lydia's
  copy without any animation state after her spawn drop, "desync afterwards"; passed 4 of 4 alone and in
  the other full runs.
- **Leveled wildlife picks its own base on each game** (host Rabbit is a Fox on the client, Deer an
  Elk). The copy should take its owner's base.
- **Console `disable`/`kill` on a wild wolf did nothing** on the host, no error logged. Not investigated.
- **Trespass and crime:** a remote player is never caught (only the real player trespasses). Decided
  answer: shared party bounty (`docs/ideas.md`).
- **Voice lines, not traced:** where a copy's resting `soundCallBackSet` 3 came from in
  `move-host-npc`; why no line is queued on a copy past fade distance. A copy that surfaces before its
  owner's gasp arrives could gasp on its own (not seen).
- **Crash in the harness's `strip`** after 376 hot reloads (`UnequipAll` on a copy, garbage list
  node). Not reproduced on fresh games (5 of 5).

### Engine threading and crashes

- **Our client update runs on a job thread** inside `HookVMUpdate`. Which AI stage jobs run beside it,
  and are our engine changes (spawn, delete, `MoveTo`, packages) safe there?
  (`docs/re/crashes/2026-10-12-wolf-combat-coc.md`, open questions.) Not verified in IDA either: which
  `WaitForJobTask` joins `Job_Combat_and_magic`.
- **Wolf crash fix rests on the code path only:** the one-game repro never crashed (0 of 10 with and
  without the fix); the two-game load is needed to hit it.
- **Menu mode and `equipitem`:** an NPC's `equipitem` run while paused never shows
  (`AIProcess::Update3DModel_Impl` reads menu mode; not traced). Why the harness pauses only cell changes.
- **Lock order, vanilla:** `coc` while the exterior world steps inverts the extra-data and `bhkWorld`
  locks. Listed in `scripts/lock_order_known.txt`, not fixed.
- **Portal graph crash:** our fix works on the repro; SCN-Crash-fix was not run against it
  (`docs/re/crashes/2026-10-11-qasmoke-exit-portal-graph.md`).

### Migration

- **Tether:** pulling a player along and travel requests are not built.
- **Teleport command** still reads the server's stale positions.
- **Object ids** still come from the server (`AssignObjectsRequest`).
- **Quests** need their own design before sync goes on.

### Tests and harness

- `late-join-client`'s party panel after a rejoin (health -1 for the host): a disconnect deleted the other
  players' copies but left their entities with a `PlayerComponent`, and player ids repeat across sessions,
  so the panel could pick the dead one (order-dependent, seen before). Fixed 2026-10-13: a disconnect drops
  every copy (`DropCopies`); the panel check passed in all 4 runs since. Its other failure, Lydia's `JumpLand` missed after the rejoin,
  is the interpolation delay entry above.
- A stuck `LoadWaitSpinner` once kept the host from taking input (2026-10-13, a fresh game): the first
  `cow` to the river took over `both_go`'s 10 s fade wait on the client, and the cleanup `coc` ran while
  the host's spinner was up; it stayed through later cell changes until a restart. Not reproduced.
- The map's fast travel UI (open the map, pick a marker, confirm) is not driven; `party-fast-travels` uses
  the console's `FastTravel`, which queues the same move (`docs/re/systems/player-travel.md`).
- The `duo` harness process once segfaulted on exit after a pass.
- A harness command with a wrongly typed field (`place` with a hex string target) throws in `nlohmann::json`
  and takes the game down (`CommandsStaging.cpp` `Place`, 2026-10-13). Commands should read fields without
  throwing.
- Harness console lines other than cell changes run unpaused, unlike a typed line.
- `scripts/re/ida_export.py` exported 58,317 rows (every named address) instead of the ~100 hand-set ones
  (2026-10-12); the six new rows were added to `docs/re/ida_notes.tsv` by hand.

## Product pieces not started

- Joining through a Steam invite (the core value: host clicks host, a friend joins). Started work is
  shelved in two git stashes: `Code/client/Steam/` lobby and session (2026-10-11), and a Steam
  probe and launcher with tests (2026-10-10). Not built or tested in the current tree.
- In-game screens besides the party panel: chat, player list, connect, settings, notifications.
- Mod list check on by default in host mode.

## Infrastructure (done)

- Linux cross-build with clang-cl (`4571bcbe`), one-file rebuilds in ~3 s (`71858be5`).
- Two-game scenarios off-screen, through real input (`088d78da`); harness out of player builds (`ea087fb2`).
- Hot reload into running games (`d09fdd1f`).
- Crash and hang tooling: CrashLogger reports, breadcrumbs, heap history, probes, repro, vanilla
  harness (`f4ff0467`); lock-order checker (`261be264`); desync checker (`8e14e17d`).
- RE references: `docs/re/ENGINE_REFERENCE.md`, CommonLibSSE-NG and SKSE plugin sources, IDA database.
