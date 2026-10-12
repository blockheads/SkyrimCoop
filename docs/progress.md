# Progress

Where SkyrimCoop stands. Update it when a phase, system or known failure changes.
Plans live elsewhere: the migration in `docs/design/host-authority-migration.md`, later ideas in
`docs/ideas.md`, engine findings in `docs/re/`.

**Goal:** two to four friends play Skyrim together, the host's game is the only authority (direct
authority), and the Skyrim Together server is gone. Peers own only their own player; the host owns
every NPC and the world.

Last updated 2026-10-12.

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
| Cell arrivals (party tether: copies dropped and resent on teleport) | done for `coc` (`72b90888`); doors and fast travel unverified |
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
| Cell changes (`coc`) | works | `teleport-keeps-copies`, `party-changes-cells` |
| Equipment and hands | works | `equip-*`, `repair-equipment-drift`, `hands-survive-reload-*` |
| Draw / sheathe | works | `draw-*`, `quick-sheathe-*` |
| Sneak, swimming, diving | works | `sneak-*`, `dive-*` (uncommitted) |
| Casting | works; repeat casts on copies were refused intermittently | `cast-*`, `input-mouse-cast` |
| Health | works | `health-*` |
| NPC death and removal | removal works; `death-host-npc` failing | `remove-host-npc`, `death-host-npc` |
| Melee and spells on host NPCs | hits work; kill credit failing; Lydia's stagger desyncs after a firebolt | `melee-client-hits-host-npc`, `firebolt-*`, `flames-*`, `client-*-kill-credit` |
| Hostility | works | `client-angers-host-ally` |
| Effect visuals (burning, hit shaders) | failing on copies (the magic chain) | `burning-shows-on-copy` |
| Voice lines, subtitles, lip sync | works | `copy-ends-voice-line` (uncommitted subtitle check) |
| Doors, locks | works | `door-opens-for-both`, `lock-reaches-others`, `load-door-not-replayed` |
| Time, weather | works | `host-time-reaches-client`, `weather-follows-host` |
| Party panel (RmlUi) | works alone; fails after other scenarios | `party-panel` |
| Real input (keyboard, mouse) | works | `input-keyboard-draw`, `input-mouse-cast` |
| Mount, AI package, dialogue, XP, factions | relayed, no scenario | |
| Quests, crime, containers, dropped items, followers, physics | not synced | crime: shared party bounty decided (`docs/ideas.md`) |
| Mods | host's mod list is meant to be authoritative; world state of mods not synced | `docs/design/world-state-sync.md` (parked) |

## Copies (how other players' and the host's actors appear)

Copies are puppets: they run no engine AI and replay what the owner did (`docs/design/copy-contract.md`).

- Done: no packages (`8b464c19`), hands owned by the player (`704a82d6`), hit feedback and effect
  visuals (`1dd5628c`).
- In progress (uncommitted, being split into commits 1-6 for review): the AI update skipped for copies
  with the body upkeep kept, the engine's do-nothing package, the owner's animation graph state loaded
  at spawn (fixes the copy pose after `coc`: `party-changes-cells` passes without stepping), sneak/dive/subtitle scenarios, the harness pausing cell
  changes (wolf crash), the lock checker's `coc` pattern.
- Next audits: combat, detection, magic/magicka; then one copy update in place of scattered
  `IsRemote()` checks.

## Test suite

- Last full run (2026-10-12, uncommitted build): 36 of 43 pass.
- Failing: `death-host-npc`, `client-spell-kill-credit`, `client-melee-kill-credit`,
  `burning-shows-on-copy` (the magic chain), `late-join-client` (party panel).
- Order-dependent (pass alone): `party-panel`, `cast-host-player`.
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
- **A copy spends its own magicka** (4963-4998 vs the owner's 5000 after firebolt/flames); it should
  have the owner's.
- **Magic chain:** run back to back, the host's copy of the client ends on `Magic_Equip` and refuses
  later casts; its spell then goes missing from the copy's equipment in later scenarios. Not explained.
  Likely behind `burning-shows-on-copy` and the kill-credit failures.
- **Cast right after a jump landing** (first person, within ~250 ms): the copy's graph is still
  landing and refuses the cast (3 refusals in the last measurement). Not explained.
- **Repeat casts refused intermittently** (seen 2026-10-10): recheck now that copies skip the AI update.
- **`death-host-npc`:** the desync check fails on the chicken's `animation.state1` (owner has the
  Dying life state 0x200041, copy 0x41). Not from the upkeep or the package.
- **Graph state at spawn, not yet covered:** no live sitting/furniture scenario (a copy must reach the
  state by replay while loaded; the spawn load would hide it otherwise); behaviour mods (Nemesis/Pandora,
  Open Animation Replacer) untested; the host's player record (`HostCharacters`) keeps the graph state
  from the player's last arrival, so a copy spawned from the record rather than the host's live copy can
  start in an older state. Check when the record is used.
- **Kill credit** for a client's spell or melee kill: failing (`client-*-kill-credit`).
- **Lydia's stagger** desyncs after a client's firebolt (`firebolt-client-hits-host-npc`).
- **Graph outputs still synced:** Master_Behavior variables that the graph itself writes
  (`IsCastingRight/Left`, `bMRh_Ready`, `IsEquipping`, `IsAttacking`, `bInJumpState`, ...; list with
  `behavior_dump.py writers`), and `currentDefaultState` in the troll, draugr, werewolf, steam and
  falmer descriptors. No harm traced yet.
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

- **Doors and fast travel:** do they send `PositionPlayerEvent`? Only `coc` is checked. If not, hook
  ID 40438, where all three end.
- **Tether:** pulling a player along and travel requests are not built.
- **Teleport command** still reads the server's stale positions.
- **Object ids** still come from the server (`AssignObjectsRequest`).
- **Quests** need their own design before sync goes on.

### Tests and harness

- Order-dependent: `party-panel` and `cast-host-player` pass alone, fail in the full run.
- `late-join-client` fails on the party panel after the rejoin.
- The `duo` harness process once segfaulted on exit after a pass.
- Harness console lines other than cell changes run unpaused, unlike a typed line.

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
