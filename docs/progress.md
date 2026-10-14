# Progress

Where SkyrimCoop stands. Update it when a phase, system or known failure changes.
Plans live elsewhere: the migration in `docs/design/host-authority-migration.md`, joining a session
(build, game and mod checks, host-stored characters, arrival) in `docs/design/join-flow.md`, later
ideas in `docs/ideas.md`, engine findings in `docs/re/`.

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
| Cell arrivals (party tether: copies dropped and resent on teleport) | done for every travel: `coc`, doors, fast travel, moves all end in ID 40438 (`docs/re/systems/player-travel.md`; door and fast-travel scenarios `87972601`); the arrival refreshes the host's record of the player (inventory, values, hands) |
| Arriving together: players step aside onto navmesh floor by session slot | done (`83178416`), `party-spreads-on-arrival` |
| Copies converge on their owner (presence): a copy whose owner is around but which is elsewhere, without 3D or deleted is moved or spawned again; a spawn for a known copy refreshes it | done (`87972601`), `party-fast-travels` |
| Shared XP (upstream's party XP, to everyone) | works, `shared-xp` |
| Objects: activation, locks, script animations | done as bridge relays by form id (`31abfd23`, `550ece84`); the unused object ids deleted (uncommitted) |
| Quests | not started; quest sync off (`cSyncQuests`) until it has a design |
| Waypoints, chat | done as bridge rules, server services deleted; chat is an RmlUi box (Enter), `chat-reaches-everyone`; player list health % and dialogue lines in chat dropped (uncommitted) |
| Shared map markers: a marker placed on the map (P) shows on everyone's map | on again; placing and removing (the map's "Remove It", by mouse), `map-marker-reaches-everyone` (uncommitted) |
| Teleport command, teleport to a player | dropped: everyone is already together under the tether (uncommitted) |
| Tether itself (pulling a player along, travel requests) | not built: a player who leaves on their own is only logged |

### Still on the embedded server (what phase 4-5 removes)

Removed one section at a time, each with its bridge replacement and tests first
(`docs/design/host-authority-migration.md`, "Server sections"):

1. Player levels: done.
2. String cache: dropped, not moved (uncommitted).
3. Session, in steps: 3a auth gate (3a.1 build and cap: done, uncommitted; 3a.1b refusals
   delivered as data (join status, host chat line): done; 3a.2 game and SKSE versions, 3a.3 plugin
   list and order, 3a.4 the host's settings: done, uncommitted; the gate is complete), 3b player list, 3c player records (a shadow record stays on
   the server for its last services until section 4): done; 3d mod ids (a plugin's place in
   the load order): done, uncommitted. Section 3 is complete.
4. The listener (phase 5), and with it what nothing replaces: `ScriptService` (Lua), `AdminService`,
   server console commands, recording.

`QuestService` stays until quests have a design.

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
| Player levels (party panel) | works | `party-shows-levels` |
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

- Kill moves (2026-10-13, uncommitted): `host-kill-move` 10 of 10. The host's player kills a wolf with
  a kill move (`KillMoveRandom` 100); on the client the host's character and the wolf's copy play it as
  one pair (the engine's kill-move flag on both in one frame) about 330 ms after the host, laid out as on
  the host (distance, bearing and heading within 15 units/degrees, both kill-move variants), at the same
  pace (the blow within 350 ms of the host's, counted from each start), the wolf's copy dying within
  350 ms of its blow, and the client's own player untouched. Three fixes:
  - Action targets go as server ids (`ActionEvent::TargetActor`): the target was the owner's form id, so
    the wolf's copy paired with the client's own player (form id 14) and the player's copy with nothing.
  - One clock for everyone: a game slowing its clock slows every game's (`HookSetGlobalTimeMultiplier` in
    `BSTimer.cpp`, the host's `HostTimeScale`; each game runs at the slower of its own and the others'
    slowest). Every kill move plays a kill cam, and some of its camera shots slow the clock (`BGSCameraShot`
    `globalTimeMult`, to 0.2 and ramping back): the host's kill move then took 4.1 s against the copy's
    2.9 s, and the wolf's copy stood 1.4 s after its blow until its owner died (3 of 8 runs). Now both
    clocks slow to 0.2 together (2 of 10 runs) and the blows still line up. Slow Time, a bow zoom and
    `sgtm` go the same way. `clock-slows-for-everyone` (each game's `sgtm`, both at once) passes;
    `BridgeTests` (5 `[TimeScale]` cases).
  - An owner does not send its hands while dying (`RunWeaponStateUpdates`): its hands-down arrived before
    its death action, and the copy's weapon convergence sheathed it mid-death (its death 0.5 s late).
  `client-*-kill-credit`, `death-host-npc`, `health-host-npc`, furniture, draw, cast, sneak, quick
  sheathe, `equip-host-npc`, `steady-state` pass; `late-join-client` failed once (Loose ends), then 3 of
  3; `TPTests`. Notes: `docs/re/systems/actor-ai-and-copies.md` ("An action's
  target", "Kill cams slow the clock").
- Review of the session's changes (2026-10-13): every new engine call checked against CommonLibSSE-NG and
  IDA (`SetActorValueModifiers` 38468 as `DoDamage` calls it, the deferred-kill pair through the papyrus
  natives, `HandleHealthDamage` vtable 0x104, `BSTimer` 68245/410196/388442, `CrosshairPickData`, the
  default objects at 0x20 + 8 × index). Fixed: a player joining during a slowdown stayed slowed after it
  (`HostTimeScale` did not record what it told it; a bridge test now fails without the fix), the harness's
  session snapshot sent its action target as a form id, a duplicated `IsInKillMove`, an animation trace
  that failed to start left its screen captures running. `BridgeTests` 639 of 639; on the games
  `host-kill-move` 3 of 3, `clock-slows-for-everyone`, kill credit, `death-host-npc`, `late-join-client`,
  `steady-state` pass, `host-rejoins` 3 of 4 (Lydia's `JumpLand`, Loose ends).
- Animation trace (2026-10-13, uncommitted): `AnimTrace` in `coop_test.py` and the harness's
  `anim_trace` command (`Code/client/TestHarness/AnimationTrace.cpp`). For chosen actors in both games
  it records every behaviour graph event (a sink on each graph's `BSAnimationGraphEvent` source), the
  actions taken, life state, kill move, health, graph booleans and position each frame, with the wall
  clock and frame; both screens; and the plugin logs' lines naming them. It writes one timeline (host
  left, client right), `trace.json`, the videos and side-by-side frames at marked moments to
  `build/coop-test/traces/`. Scenarios can wait on its entries (`trace.wait`, `trace.wait_state`). Each
  game's kill cam and clock speed are entries too.
- Kills (2026-10-13, uncommitted): `client-melee-kill-credit` and `client-spell-kill-credit` 10 of 10
  each, and now wait for the body to come to rest, the copy's within 50 units of its owner's. The
  host takes a forwarded hit through the engine's damage path with the attacker
  (`SetActorValueModifiers` 38468): its death rules, a death animation, the killer recorded at once.
  It used to force the damage without an attacker (the engine queued a killer-less kill) and then kill
  with an instant ragdoll that threw the body and dragged the copy through the air. Copies are in
  deferred kill (as skymp's remote actors): they show their owner's health as is and die only with
  their owner. The client stands in front of the wolf's head.
  `combat`, `magic`, `values` tags 18 of 18; `BridgeTests` 589 of 589. Notes:
  `docs/re/systems/actor-ai-and-copies.md` ("A hit forwarded to the owner").
- Kill cams (2026-10-13, uncommitted): `client-spell-kill-credit` passes 10 of 10 (a kill cam in 8):
  the client's killing bolt sometimes plays a kill cam (game clock at 0.25x, player controls off,
  `VATS::mode` 4, 400883), and the reset's one Ready Weapon press fell in it. `takes_input` now also
  waits for the kill cam (status `kill_cam`), and `place_before` for the landing animation
  (`bInJumpState`; a draw pressed in it stalled ~3 s).
- Mod ids (3d, 2026-10-13, uncommitted): `BridgeTests` 589 of 589 (ids are places in the load order,
  the same for every player, under each player's own names; written against the old table first,
  which failed on a name differing in case). On the real games 43 scenarios across `session`,
  `restart`, `spawn`, `world`, `objects`, `cells`, `inventory`, `magic` and `combat` pass, all but
  `client-spell-kill-credit` (Loose ends; it fails the same without 3d).
- Player records (3c, 2026-10-13, uncommitted): `BridgeTests` 582 of 582 (every test's players now
  join through the real join; the list split out of `HostSession` as `HostPlayers`, and the 28
  passed again on freshly started games); on the real games `session`, `restart`, `spawn`, `world`, `objects`
  and `cells` (28) pass after 3c.1, and `session` and `restart` (11) after the disconnection handler.
  Kicking is covered by `host-kicks-client` only: the headless server has no network peer to drop.
- Player list (3b, 2026-10-13, uncommitted): `BridgeTests` 415 of 415 (who a newcomer learns, with
  the host marked; who learns the newcomer; leaving; a refused game never listed); the `session` and
  `restart` scenarios (11) pass; without the leave notice the bridge case fails, and on the real games
  `host-kicks-client` fails at "[host] no longer lists the client".
- The host's settings (3a.4, 2026-10-13, uncommitted): new `difficulty-follows-host` (the client
  takes the host's difficulty, follows its change, is held there, gets its own back on leaving; the
  host's is never changed) passes, and fails at the step concerned without the change notice and
  without keeping the client's own; the `session` scenarios (10) pass; `BridgeTests` 389 of 389.
- Plugins and load order with real games (3a.3, 2026-10-13, uncommitted): new
  `join-refused-plugins-differ` (tag `restart`, ~30 s: the client game restarts with an extra empty
  plugin, the real host refuses it, the client's join status lists `extra SkyrimCoopTest.esp`, the
  host's chat says why; the host must drop the killed client within enet's timeout) passes, and
  fails at that step without the timeout handling. The `session` and `restart` scenarios (10) pass;
  `BridgeTests` 378 of 378; `TPTests` pass. Seen in a screenshot: the host's chat line.
- Game versions (3a.2, 2026-10-13, uncommitted): `BridgeTests` 365 of 365 (Skyrim and SKSE
  mismatches, the build checked first, the wording), `TPTests` pass (the new fields round-trip);
  `join-refused-shows-why` adds a stand-in host on another Skyrim and SKSE and a game on another Skyrim
  asking the real host, and fails at that step with the host's check skipped; the `session`
  scenarios (9) pass. The real games send Skyrim 1.6.1170 with SKSE 2.2.6.
- Refusals as data (3a.1b, 2026-10-13, uncommitted): new `join-refused-shows-why` (the client refused
  by stand-in hosts on another build and full, the real host refusing a game on another build)
  passes; it fails at the step concerned with the old kick, with no refusal status, and with no host
  chat line. The `session` scenarios (9) pass; `BridgeTests` 349 of 349, `TPTests` pass. The scenario
  needs `scripts/xmake-linux.sh build -j16 coop_peer`.
- Join gate 3a.1 (2026-10-13, uncommitted): `BridgeTests` 339 of 339 (new `session_tests.cpp`), `TPTests`
  pass; the `session` scenarios (8) and `move-host-npc`, `remove-host-npc`, `party-changes-cells`,
  `door-opens-for-both` pass after a hot reload (which joins both games through the new gate).
- Map markers (2026-10-13, uncommitted): `map-marker-reaches-everyone` (place both ways, remove) 3 of 3 on
  a warm duo and 2 of 2 on a fresh one, `chat-reaches-everyone` passes with `key` through the control map, `BridgeTests` 284 of 284.
  Test games had tutorials on all along: `bShowTutorials` went to SkyrimPrefs.ini, which ignores it; it
  is in Skyrim.ini now (the map's first-open tutorial took the next key).
- Relays, object ids, chat (2026-10-13, uncommitted): `BridgeTests` 284 of 284; the `objects` and `session`
  scenarios, `chat-reaches-everyone` and the input scenarios pass.
- Travel and presence (2026-10-13, `87972601`): `party-uses-door` and `party-fast-travels` added; those two,
  the session scenarios, `teleport-keeps-copies` and `party-changes-cells` pass (8 of 8). A full run was
  stopped after 26 (25 pass; the failure, `late-join-client`, is fixed: see Tests and harness).
- Last full run (2026-10-13, `5cdf49d2` build): **47 of 47**. Earlier the same day: 42 of 43, then
  45 of 47 with the new scenarios (the two dives, fixed: they had passed only because the arrival fade
  swallowed the swim input). The morning's baseline was 37 of 43.
- Harness (2026-10-13): a player's button waits for the game to take input (no fade or menu);
  `both_go` waits for the arrival's fade; `attack` waits for the last swing to end; `place` takes a
  bearing; lock patterns match in either order.
- Bridge rules: headless `BridgeTests` (`Code/tests/bridge`), wire bytes pinned in `wire_tests.cpp`. Build with
  `scripts/xmake-linux.sh` (GCC, lld; a one-file change links in ~6 s). Not clang yet: sol2 3.3.0 (the
  server's Lua) does not build with clang 18; the server goes in phase 5.
- Encoding: `TPTests` (`Code/tests/encoding.cpp`), same build. Encoding only since 2026-10-13: it had not
  built since `a94b0002` deleted `line_buffer.h`, which the `native_client` files it compiled include.
  The server and `native_client` test files beside it stay unbuilt until their server section deletes them.

## Loose ends

Everything known to be unfinished, unverified or unexplained. Close an entry by fixing it, proving it
harmless, or moving it to `docs/ideas.md`; say where the evidence went. Details are in the doc named.

### Copies and actor sync

Notes: `docs/re/systems/actor-ai-and-copies.md` ("Still open"), `docs/design/copy-contract.md`.

- **Kill moves by the client** (2026-10-13): a host kill move syncs (Test suite), but a client cannot
  start one on a host NPC: `ShouldAttackKill` (21744, `Actor::sub_38559`) needs the attacker engaging
  in combat, and no copy is ever in combat with the client's player. The kill move should then go to the
  host, which plays it with the real NPC.
- **A copy's kill move skips the owner's approach** (2026-10-13): on the host `KillMoveStart` comes
  ~180 ms after the paired idle starts (the engine bringing the two together); a copy's comes at once,
  so the client's blow lands ~120 ms after the host's although the idle starts ~330 ms after. Seen in
  the trace; looks fine. The copies spend the first ~250 ms sliding into place by movement sync.
- **Far copies are not in deferred kill** (2026-10-13, review): the engine keeps it only on middle/high
  process actors (`docs/re/systems/actor-ai-and-copies.md`, "A copy is in deferred kill"), so a copy
  dropped to low process can die from its owner's 0 health before its owner's death action. Unseen at
  that distance; the death action is skipped for an already dead copy.
- **A copy slides after its owner is teleported** (2026-10-13): the scenarios' `placeatme` then move
  of a host NPC (~900 units) shows on the client as a 0.5 s slide across the room, not a jump.
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
- **The old CEF overlay and its Angular UI (`Code/skyrim_ui`) are still in the tree**, compiled out of the
  Windows build: the RmlUi panels replace them (party, chat).
- **Quests** need their own design before sync goes on.
- **Build ids of uncommitted trees match each other.** `BuildCommit()` is `git describe --tags
  --always --dirty`, written on every xmake run (`Code/common/xmake.lua`; it was configure-time only
  and lagged commits until 2026-10-13): two different uncommitted trees on the same commit both read
  `...-dirty` and pass the gate's stage 1. Fine for development; release builds come from commits.

### Tests and harness

- `late-join-client`'s party panel after a rejoin (health -1 for the host): a disconnect deleted the other
  players' copies but left their entities with a `PlayerComponent`, and player ids repeat across sessions,
  so the panel could pick the dead one (order-dependent, seen before). Fixed 2026-10-13: a disconnect drops
  every copy (`DropCopies`); the panel check passed in all 4 runs since. Its other failure, Lydia's `JumpLand` missed after the rejoin,
  is the interpolation delay entry above.
- `late-join-client` failed once with the host's player's and Lydia's copies both facing 270° (the
  client's home heading) against 147° (2026-10-13, after the kill-move work; then 3 of 3): the desync
  check likely ran before the rejoined client's first rotation update.
- A stuck `LoadWaitSpinner` once kept the host from taking input (2026-10-13, a fresh game): the first
  `cow` to the river took over `both_go`'s 10 s fade wait on the client, and the cleanup `coc` ran while
  the host's spinner was up; it stayed through later cell changes until a restart. Not reproduced.
- The stuck `LoadWaitSpinner` happened again (2026-10-13, host, the first `both_go` to Riverwood on a fresh
  duo); a restart cleared it.
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

The join flow these build toward is in `docs/design/join-flow.md` (agreed 2026-10-13); its "Build
order" says what comes during the migration and what after.

- Joining through a Steam invite (the core value: host clicks host, a friend joins). Started work is
  shelved in two git stashes: `Code/client/Steam/` lobby and session (2026-10-11), and a Steam
  probe and launcher with tests (2026-10-10). Not built or tested in the current tree.
- Identical installs: the plugin list and load order checked against the host's game (section 3a),
  then a manifest of every file (`join-flow.md`, stage 3).
- Mod sync: making a friend's install match the host's, and with it whether a launcher is needed at
  all. Open: research spike `docs/research/mod-sync-spike.md` (for an agent working alone), findings
  to `docs/research/mod-sync.md`. 1.0 at least lists the differences with links.
- Updating our mod to the host's build (download, restart, a resident loader picks the new build).
- Joining from the main menu: connect and gate with no save loaded; the host sends its save (save
  transfer) and the friend's game loads it, as their character (`join-flow.md`, stages 4 to 6).
- Host-stored characters: every player's character in the host's save (its SKSE co-save); new players
  make one. Only the host saves; a friend's game cannot save while connected.
- The plugin's file interface (`request`, `status`, `events`, `manifest`) and the error model (state
  with stable reason codes, events as chat system lines); the harness moves onto the files.
- The join screen: one component, a dark full screen over the main menu with our panel, showing
  each step and a refusal in the same place; a Host button in game; needs mouse and keyboard input
  for RmlUi. Built once, with joining from the main menu and save transfer (`join-flow.md`).
- Session settings from the host: its real difficulty (forced to Master today), a settings screen.
- In-game screens besides the party panel: player list with kick, settings.
- A launcher: not decided; the mod sync spike answers whether one is needed. Steam research and a
  launcher prototype are shelved in `git stash` ("steam/launcher work"); next Steam step: the
  two-account test (`join-flow.md`, "Steam"). Side note: AOL-style buddy list, global chat and
  per-session chat rooms.

## Infrastructure (done)

- Linux cross-build with clang-cl (`4571bcbe`), one-file rebuilds in ~3 s (`71858be5`).
- Two-game scenarios off-screen, through real input (`088d78da`); harness out of player builds (`ea087fb2`).
- Hot reload into running games (`d09fdd1f`).
- Crash and hang tooling: CrashLogger reports, breadcrumbs, heap history, probes, repro, vanilla
  harness (`f4ff0467`); lock-order checker (`261be264`); desync checker (`8e14e17d`).
- RE references: `docs/re/ENGINE_REFERENCE.md`, CommonLibSSE-NG and SKSE plugin sources, IDA database.
