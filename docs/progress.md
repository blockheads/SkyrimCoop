# Progress

Where SkyrimCoop stands. Update it when a phase, system or known failure changes.
Plans live elsewhere: the migration in `docs/design/host-authority-migration.md`, joining a session
(build, game and mod checks, host-stored characters, arrival) in `docs/design/join-flow.md`, how the
party shares one story (the party is the Dragonborn) in `docs/design/party-model.md`, later ideas in
`docs/ideas.md`, engine findings in `docs/re/`, the words (host, guest, owner, copy, player) in
`docs/design/glossary.md`.

**Goal:** two to four friends play Skyrim together, the host's game is the only authority (direct
authority), and the Skyrim Together server is gone. Peers own only their own player; the host owns
every NPC and the world.

Last updated 2026-10-21 (phase 5).

## Direct authority migration

Porting Skyrim Together's server to the host's game (`docs/design/host-authority-migration.md`).

| Phase | What | Status |
|---|---|---|
| 0 | Ground truth: message table, backbone scenarios, harness `disconnect`/`game` status | done (`ab302206`) |
| 1 | The host talks to the embedded server in-process, no socket | done (`a76aeddd`) |
| 2 | `HostBridge` (now `HostAuthority`), movement relay, late join from live state | done (`e49e7aea`) |
| 3a | Relay families as bridge rules: inventory, actor values, magic and combat, character extras | done (`84fb7465`, `c5936751`, `cd3dfd0a`) |
| 3b | Character ids, owners, spawns, removal, respawn on the bridge | done (`5e99eae7`) |
| 4 | Host-decided state | partly, see below |
| 5 | Delete `Code/server/`; the enet6 listener moves into the bridge | done 2026-10-21 (uncommitted): `HostNetwork` (below) |
| after | Physics sync on the bridge's unreliable channel | not started (own plan); rest positions come first with world sync (below) |

### Phase 4 parts

| Part | Status |
|---|---|
| Time (`HostClock`; set-time from any player) | done (`4e0efb0a`); set-time works in the game since 2026-10-20 (`/settime` in the chat, `guest-sets-time`); the whole calendar 2026-10-20: a guest takes the host's date and days passed too, written as `Calendar::Update` leaves them (`docs/re/systems/calendar.md`) |
| Weather (`HostWeather`) | done (`50397fa0`) |
| Host/guest code split, the world (2026-10-20) | done: `Services/Guest/` (`GuestClock`, `GuestWeather`, `GuestWorldState`) exist only while joined (`GuestService`); `Bridges/HostEngine` (the host's reads, set hour) and `Bridges/GuestEngine` (applying the host's calendar, weather, globals, quests) replace those `IWorldBridge` calls; `CalendarService` is `TimeScaleService`; `WeatherService`, `WorldStateService` deleted; `HostBridge` renamed `HostAuthority`. Actors next, by owner/copy (`docs/ideas.md`, "Client services by role") |
| Session = party: roster, host kicks, no party messages | done (`50397fa0`) |
| Cell arrivals (party tether: copies dropped and resent on teleport) | done for every travel: `coc`, doors, fast travel, moves all end in ID 40438 (`docs/re/systems/player-travel.md`; door and fast-travel scenarios `87972601`); the arrival refreshes the host's record of the player (inventory, values, hands) |
| Arriving together: the party stacks on the arrival spot, passing through each other until each walks off; followers face the leader's way, so every game shows the same view | done (replaces the step-aside of `83178416`), `party-stacks-on-arrival`, `party-uses-door` |
| Copies converge on their owner (presence): a copy whose owner is around but which is elsewhere, without 3D or deleted is moved or spawned again; a spawn for a known copy refreshes it | done (`87972601`), `party-fast-travels` |
| Players' copies travel with the party: persistent, moved with their owner, never respawned on travel | done (`df9af2ff`), the travel scenarios |
| Experience: personal, earned by whoever did it; party rewards to everyone (`docs/design/party-model.md`, "Experience") | rule decided 2026-10-19; wiring below. Upstream's shared combat XP still on (`shared-xp`) until step 1 |
| Objects: activation, locks, script animations | done as bridge relays by form id (`31abfd23`, `550ece84`); the unused object ids deleted (`b7b83aad`) |
| Shared dialogue: one conversation for the party, run by the host's engine; each player sees their own options, anyone picks with their own line | parked 2026-10-19 until quest/world sync and AI state on copies. In: the host's rule and messages (`HostDialogue`, `dialogue_tests.cpp`), copies' own lines and subtitles, the engine structs (`MenuTopicManager`, `UIMessageQueue`). The client (`DialogueService`, `DialogueBridge`, harness `dialogue`, `dialogue_pick`, `skill`, 22 scenarios passing on 2026-10-19) is on tag `archive/shared-dialogue-client` (was a branch): it syncs the host's list to each peer, to be redone as step sync (below; `docs/design/shared-dialogue.md`, "Next"). Engine notes `docs/re/systems/dialogue.md` |
| Quests | done 2026-10-20 as world state: the host runs every quest, guests take its stages (fragments skipped), objectives and alias fills (`docs/design/world-state-sync.md`, `docs/re/systems/quest-apply.md`); upstream's quest relay deleted with phase 5 |
| Waypoints, chat | done as bridge rules, server services deleted; chat is an RmlUi box (Enter), `chat-reaches-everyone`; player list health % and dialogue lines in chat dropped (`b7b83aad`) |
| Shared map markers: a marker placed on the map (P) shows on everyone's map | on again; placing and removing (the map's "Remove It", by mouse), `map-marker-reaches-everyone` (`b7b83aad`) |
| Teleport command, teleport to a player | dropped: everyone is already together under the tether (`b7b83aad`) |
| Tether itself: anyone who travels takes the party along | done (`df9af2ff`): the leader's arrival moves everyone else there, the host too; `party-uses-door`, `party-fast-travels`, `party-quick-travel`, `load-door-not-replayed` |

### Experience: wiring up

The rule and why: `docs/design/party-model.md` ("Experience"); engine path and the mods:
`docs/re/systems/experience.md`. Each step with its scenario.

1. **Drop upstream's shared combat XP**: the `SyncExperienceRequest` relay, `AddExperienceEvent`,
   `LastUsedCombatSkill`, `RunExperienceUpdates`, and `ScopedExperienceOverride` with its 27244 hook if
   nothing else needs them. `shared-xp` becomes `xp-stays-personal`: the client's hit raises its One-Handed
   experience, the host's stays.
2. **Fix `HookAddSkillExperience`'s signature** (40488 takes the advance object and action too;
   CommonLibSSE-NG, IDA); check it in the disassembly.
3. **Speech from a picked line to the picker**: built 2026-10-19 with shared dialogue's client, parked on
   tag `archive/shared-dialogue-client` (the host's game holds back its player's Speech gain after another player's
   pick and sends its input amount; the picker's game adds it through 40488, `GainSkillExperience`). The host's
   side (`DialogueExperienceRequest` → `NotifyDialogueExperience`, `dialogue_tests.cpp`) is in. A script's
   skill gain outside a picked line (a quest's reward) is the party's (step 6).
4. **Barter Speech to the trader**, with shared barter.
5. **Training to whoever pays**: done 2026-10-19 with shared dialogue (Flow, 8): the training menu opens on the
   picker's game with its copy of the trainer, so the cost, the skill and the trainings this level are the
   picker's (`dialogue-picker-trains`).
6. **Quest rewards to everyone**, with quest sync: a script's `Game.AdvanceSkill` / `IncrementSkill` on the
   host's player is replayed on each game as its own player's (input, not the measured gain). Mods' quest
   experience follows from the quest completing on every game.
7. **Kills**: the killer's game records its own player's kill. Scenario with Experience (17751) installed:
   a peer's kill of a host NPC, who gets its kill experience.
8. **Mods once**: Skill Uncapper AE and Experience installed, a routed gain (step 3) gets the receiver's
   multiplier once.

### World sync: finishing it (plan 2026-10-21)

Before AI state and dialogue (below), world sync is finished while its pieces are fresh (`docs/design/world-state-sync.md`,
"As built" and step 7). Each step ends with its scenarios and the oracle (`world-diff`, `world-changes-reach-guest`) passing.

1. **The world diff after every scenario.** `scenario` compares the two games' loaded worlds after each scenario
   (`world_differences`, idle animation skipped) and fails one that leaves them different; `coop_test.py unsynced`
   collects the flags with no row from the whole suite. Expect failures at first (the travelling scenarios);
   each is a sync gap, a row or a scenario that leaves the world changed.
   Built 2026-10-21 (`check_world`): after each scenario, the cells both games have loaded, a difference counted
   when it lasts 3s; again at home after the reset (charged to the scenario that left it, not the next). `objects`
   and `cells` pass but `create` (step 3); `party-stacks-on-arrival` fails on its own check (the client walks
   through the host's player), unrelated. Found on the way: a model's own loops (QASmoke's spinning arms) frozen
   off-camera at different phases (pose not compared on references neither game changed and with no graph); the
   lock kind's put-back could not reach level 0 with the console (harness `lock` now). `unsynced` adds base
   object `0x80` (E95D1, arriving in SoljundsSinkhole01) and encounter zone `0x20000000` (664DE, through door
   13419): both on a cell's first load, likely leveled rolls, step 2's.
2. **State at cell load.** A guest's cell loading gets the host's current state of every changed reference there,
   with the same rows (`ReferenceStates`): the changes a guest missed while away (dropped today, "its object is
   not in this game") and leveled containers (containers slice 2: the host's roll, not the guest's). A scoreboard
   kind: the guest leaves, the host changes something, the guest comes back.
   2a, the references, built 2026-10-21: each cell a guest loads (`TESCellFullyLoadedEvent`, and every loaded
   cell on joining) sends `CellStateRequest` with the references its own game holds row changes for; the host
   answers with every row of the references either game changed there (`NotifyObjectChanges` with `Cell`), or
   once its game has that cell loaded. A cell's state skips what already matches (no events); live changes
   still apply as the host's game did. Scenario `late-join-gets-cell-state` (fails on the old build, passes
   now); bridge cases in `world_state_tests.cpp`. Found: a cell's references stay in memory after it unloads, so
   live changes already reached cells a guest had loaded once; the gap was cells it never loaded and changes made
   while it was away. 2b, leveled containers, built 2026-10-21: the host rolls each container of a guest's
   loaded cell (16038) and sends all it holds (`kContents`); a guest's game rolls no world container's leveled
   entries (16129 skipped). Scenario `guest-opens-hosts-roll` (fails with the two lines off: the client's own
   roll). 2c, placed leveled items, built 2026-10-21: a reference placed with a leveled base (`XLIB`, e.g. Soljund's
   Sinkhole's clutter books: `DummyBook` `D7866` with `LItemBookClutter` `D9F89`, 124 books) rolls the list once at
   its cell's first load in `TESObjectREFR::PickLeveledBase` (20232, our name; called from the cell's reference
   loads), takes the pick as its base (19785) and records `0x80`. A guest's game skips the pick (hooked, with the
   story manager's switch); the host's pick goes as a `kBase` row (live, and in the cell's state), applied with
   `SetObjectReference` and the 3D dropped and loaded again (Disable/Enable, the form-flags record left as it
   was). Scenario `placed-leveled-items-are-hosts` (failed on the 2b build: 13 books each game's own). Encounter
   zone `0x20000000` (`664DE`) not identified: not in memory since.
3. **Created objects** (`0xFF` ids, each game's own): dropped items, placed objects, loot. A mapping between the
   host's and each guest's ids, made when the host creates one; also what quest fills of created objects need
   (loose ends, "Created objects in quest alias fills").
4. **The rest of `unsynced`:** done 2026-10-21. Animation `0x10000000`: the graph row (`kGraph`, the graphs' save
   data, ~130 bytes each) goes with a cell's state only (live changes are the relayed animation events);
   `late-join-gets-ring-pose` (a claw ring turned while the client was away). Activating children `0x4000000`:
   not sent, the host's transient bookkeeping while a reference's activate-parent children run (19428).
5. **Physics objects' rest positions.** Havok moved `0x4`: when the host's body comes to rest (deactivated), its
   position and angle go out through the move row. Each game simulates the flight its own way, then the host's
   resting spot wins. Built 2026-10-21 (`Bridges/PhysicsRests`); a guest's own push is not sent (its cell's next
   load puts the object back at the host's): guest proposals were built and removed the same day, live physics
   sends a push as an impulse instead (`docs/design/world-state-sync.md`, "Physics objects' rest").

World sync is then done: every object ends where the host's is. Live physics (the flight itself, ragdolls, a guest's
push as an impulse) is its own plan after phase 5 (`docs/design/host-authority-migration.md`, "After: physics sync").
Then AI state on copies and dialogue's step sync (next section).

### Shared dialogue: sync the step, not the list

Decided 2026-10-19 (`docs/design/shared-dialogue.md`, "Next"; `docs/design/copy-contract.md`, "AI state"): each
game's engine builds its own list from the host's step, against world and AI state that is the host's. Each step
with its scenarios.

1. **Spike:** on the client, set the conversation's line to the host's step and call the engine's build and show
   (35287, 52857); compare with the host's list, list what else 35287 reads.
2. **AI state on copies** (revised 2026-10-20, `copy-contract.md`, "AI state"): the owner sends current package,
   sitting, sleeping, scene, combat state and targets when they change; the copy writes the fields readers read
   and answers `IsInCombat`/`GetCombatState` from it, never with a combat controller of its own. The package
   first (`GetIsCurrentPackage`, 87 menu lines).
3. **Step sync:** the host sends its step at each list it shows (the greeting first, the current one to a
   joiner); peers mark said lines and build their own list. Deletes `OwnLines`, `ShowOptions`, the record of
   options put in the list, `SendOptions`, the host-options message and the `List` filter. The dialogue scenarios
   keep passing; `dialogue-peer-only-option` and `dialogue-picker-says-its-line` check each player's own lines.
4. **Quest topics on peers:** follow from quest sync (missing on a peer's menu until then).
5. **If step sync waits:** list updates event-driven, not checked every update (`DialogueService::ShowOwnLines`
   on the host's list arriving, the peer's engine building its list, a new step).

### Phase 5: the server is gone (2026-10-21, uncommitted)

Every message a game sends had a host rule; the two without one were dead (upstream's `RequestQuestUpdate`,
never sent, and `AssignObjectsRequest`). What the host still took from `GameServer` moved:

- **`HostNetwork`** (`Services/Host/`): the enet listener (`TiltedPhoques::Server`), each message to the front door
  (`HostAuthority::Handle`), and in-process routes: the host's own game (the transport's local connection) and, in
  `BridgeTests`, every test game. A route carries plain messages (no packet type byte). Ports from 10578 upward.
- **Character ids**: `HostCharacters` counts them from 1, never reused in a session; 0 is no character (the server
  handed out entity ids from 0, and `AnimationSystem` already took 0 as none).
- **Player records**: only `HostPlayers` (the server's shadow records, quest logs and `DiscordId` went).
- `HostService` starts and stops hosting at once (the server stopped on its next update).

Deleted with it: `Code/server` (Lua, admin, console commands, recording), `admin`, `admin_protocol`, the server-only
components (console, es_loader, crash_handler, resources), `native_client`, the replay and `fake_player` tests (and
`coop_test.py net` and `game`, which ran them), upstream's quest relay (`QuestService`, `QuestLog`, `CharacterData`),
the never-implemented `IEventBridge`, upstream's CEF UI and launcher (`skyrim_ui`, `tp_process`, `libraries/ui*`,
`one_ui`, `immersive_launcher`, `immersive_elf`, the overlay and Discord stubs), the MinGW build (its branches,
`devbuild` mode, `SKYRIM_STRUCT_ASSERT`, `MinGWCompat.h`, `link_stubs.cpp`), the Docker files and the old relay-era
scripts (`tests/`, `deploy_and_test.sh`, `fix_ptrace.sh`, `scripts/smoke_test.sh`). The Linux build is clang 18 now
(sol2 had kept it on GCC), and `scripts/compile_commands.sh` gives clangd both builds.

Then (2026-10-21): the upstream root files went too (CI workflows, release config, msvc-wine and Docker scripts,
Windows batch files, the old planning docs, `Tools/`), and the glossary's renames were made: `ServerId` →
`SessionId` (status key `session_id`), `ClientMessage`/`ServerMessage` → `RequestMessage`/`NotifyMessage` (with
their opcodes and factories), and the host → game messages named as requests became notifies
(`NotifyCharacterSpawn`, `NotifyReferencesMove`, `NotifyTimeSettings`, `NotifyWorldState`; the guest's move is
`ReferencesMoveRequest`). Merging the actors' session ids with created objects' ids: `docs/ideas.md`.

Dead code after that (2026-10-21): `Code/base`'s unused parts (SimpleIni, `fast_queue`, `Check.h`, `Testing.h`, the
task dialog: the Address Library error is a message box now), `SkyrimBridge`'s never-implemented stubs (14, none
called), upstream's F4 "reveal other players" (needed `SkyrimTogether.esp`), `BGSSaveLoadManager`, `MenuControls`,
`Overrides.cpp` (the counters are inline). The DLL builds with no unused-code warnings; `offsetof` on engine structs
and clang-cl's PCH include rule are silenced as expected. The one stub something called, `SetActorRotation`, is
implemented: a copy now spawns facing its owner's way (it kept its created heading until the owner's next move).

Leaving `TiltedPhoques` (2026-10-21, `docs/ideas.md`, "Modern C++ over `TiltedPhoques`"), steps 1-3 of 4:
1. No scoped allocators (the transport's send, the enet clock sync, a debug view): every allocation is rpmalloc
   through global `new`.
2. Containers are `std`: `std::vector`, `std::string`, `std::unordered_map`/`set`, `std::unique_ptr`; the hopscotch
   package is gone. Maps that go on the wire are ordered `std::map` (bytes in key order, as `wire_tests` pins them).
   A hot reload refuses this change (actor extension layout): restart the games.
3. `TP_THIS_FUNCTION`/`TP_MAKE_THISCALL`/`ThisCall` are plain C++: `using T = R(C*, A...)`, `R Hook(C* apThis, ...)`,
   `f(this, ...)` (an engine address wrapper converts to its function pointer). One call bound an rvalue to the
   engine's `uint32_t*&` through `ThisCall`'s copy; it names the copy now (`TESObjectREFR::MoveTo`).
4. The rest moved into our tree and `Code/TiltedCore` and `Code/libraries` are gone: the wire buffer is
   `Code/encoding/Wire` (`Wire::Buffer`, `Wire::Serialization`), the enet transport `Code/network` (`Net::Server`,
   `Net::Client`), the MinHook wrapper, patch helpers and D3D11/input hooks `Code/client/Hooks` (`Hooks::`, `HOOK`,
   `Hooks::Initializer`), `TaskQueue` the client's own, the CRC64 of animation descriptors `Games/Crc64`, and global
   `new`/`delete` call rpmalloc directly (`Code/base/allocator`). The `App`/`Entry` framework, its WinMain hooks and
   the SKSE loader went (`TiltedOnlineApp` is a plain class: `InstallHooks`, `Start`, `Update`). Found on the way:
   a bridge test read a message after freeing the outbox that held it (`dialogue_tests.cpp`, `Talking::Open`);
   the custom allocator had hidden it.

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
| Kill moves | works, both ways, with kill credit | `host-kill-move`, `client-kill-move` |
| Hostility | works | `client-angers-host-ally` |
| Effect visuals (burning, hit shaders) | works | `burning-shows-on-copy` |
| Beast form (a guest's werewolf change) | works: the guest's game runs it (owner-applied effect), its copies respawn for each race from the owner's state; upstream's host-player bug does not occur (2026-10-20) | `client-beast-form-stays-client` |
| Furniture (live, while the copy is loaded) | works | `furniture-*` |
| Voice lines, subtitles, lip sync | works | `copy-ends-voice-line` |
| Doors, locks | works | `door-opens-for-both`, `lock-reaches-others`, `load-door-not-replayed` |
| Containers' items (changes, as counts per base item) | works both ways; contents at a cell's load not yet (slice 2) | `chest-take-reaches-others` |
| Time, weather | works | `host-time-reaches-client`, `guest-sets-time`, `host-wait-reaches-guest`, `late-join-gets-calendar`, `party-fast-travels` (days passed), `weather-follows-host` |
| Party panel (RmlUi) | works alone; fails after other scenarios | `party-panel` |
| Player levels (party panel) | works | `party-shows-levels` |
| Real input (keyboard, mouse) | works | `input-keyboard-draw`, `input-mouse-cast` |
| Mount, AI package, dialogue, factions | relayed, no scenario | |
| Crime, dropped items, followers, physics | not synced | crime: shared party bounty decided (`docs/ideas.md`) |
| World state (quests, globals, containers, mods' state) | not synced; what drifts and how often is measured (world-state sync steps 1-2: `save-diff`, `change-rate`, `docs/research/world-state-drift.md`); a form's save data round-trips in a running game (step 3, 2026-10-20: `Games/FormState.cpp`, `form-state-round-trips`; the copies' appearance uses it); quest walks 2026-10-20: `coop_test.py quest-walk` drives real quests (main, side, radiant) from a late save (`scripts/fetch_late_saves.py`) and shows the client never follows (`docs/research/world-state-drift.md`, "Quest walks"); step 4 built: globals and quests from the host (`HostWorldState`, `NotifyGlobals`, `NotifyQuests`; a guest applies with the engine's own calls, stage fragments skipped; scenarios `global-reaches-guest`, `late-join-gets-globals`, `quest-stage-reaches-guest`, `quest-completes-for-guest`, `quest-roles-match-host`, `late-join-gets-quests`), the main-quest walk matches 14 of 14; loose ends below. Step 5 slice 1 built 2026-10-21: a guest's world scripts get no activation or trigger events, the host's run them for its copy, and their doors, animations and spells reach the guest from the host (`Papyrus/WorldEvents.cpp`, `HostWorldState`'s `NotifyObjectChanges`, `MagicTarget.cpp`; `docs/re/systems/guest-world-events.md`; scenarios `sword-box-asks-client`, `statue-fires-at-client`, `ring-turns-for-client`, `shrine-blesses-client`). Step 6's enabled state built 2026-10-21: the host's references enabled or disabled reach the guests (`NotifyObjectChanges`; `sword-box-asks-client`, harness `ref_enabled`). Locks the same day: the host's lock changes go out as object changes, a guest's are proposals the host's game takes (`HostAuthority::OnLockChange`; `lock-reaches-others`); the old lock relay is deleted. Both read on the host from the engine's change funnel (`Forms/FormChanges`, `HostEngine::TakeObjectChanges`; `docs/design/world-state-sync.md`, "As built"), built 2026-10-21 (scenarios `lock-reaches-others`, `guest-loads-propose-no-locks`, `sword-box-asks-client` pass); the `Enable`/`Disable` hooks are deleted. Containers slice 1 the same day: a container's item changes travel as its count now (`kItems` object changes from the host; a guest's own a `ContainerChangeRequest` the host's game takes), seen through the engine's `TESContainerChangedEvent` (`ContainerEvents`); `chest-take-reaches-others`; the unused `RequestObjectInventoryChanges`/`NotifyObjectInventoryChanges` deleted. World desync check for references (step 7) 2026-10-21: harness `world_state` (with each reference's change record, pose, physics, graph, effects), `coop_test.py world-diff`, scenario `world-changes-reach-guest` (also each change's object events). Reference sync reworked the same day from what it showed: rows for enabled, lock, open state, move, scale and owner (the engine's calls: swing, events, 3D, collision), the funnel also watching `RemoveChange` and repeat moves; every kind but created references matches in every side effect, items' container event included. The save-data fallback is built but off until mods (with it on, it hid the missing container event); flags with no row are logged once, `coop_test.py unsynced` (now: havok moved, activating children, animation). Open: created references, those rows, whether the guest's scripts should get object events at all, references the guest has not loaded (changes dropped). Rewritten 2026-10-20: save transfer at join, plain messages while playing (step 4: globals and quests), save data per form only for mods later; AI state travels on its own | `docs/design/world-state-sync.md`, `docs/re/systems/form-state.md` |
| A client's actions counting on the host ("who is the player") | spikes 1-5 and a script scan done; decided (`docs/design/party-model.md`): every party member is the player in the host's scripts, "the player" is the member whose action started the script, position questions without a cause ask about the party, story events judged for their member, messages to everyone. Rules 1-2 built (`Games/Skyrim/Papyrus/GuestScripts.cpp`; rule 2 never ran until 2026-10-20: `GetPlayer`'s name reads back from the engine's string pool as `getplayer`; engine names are now compared case-insensitively everywhere, `Games/EngineNames.h`, `docs/re/systems/engine-names.md`; scenarios `plate-counts-client`, `sword-box-asks-client`, `statue-fires-at-client`, `ring-turns-for-client`, `keyhole-asks-client`, `shrine-blesses-client`, `vilkas-counts-client-hits`); upstream's patched vanilla scripts and `SkyrimTogetherUtils` natives removed; rules 3-5 not built. The client's world scripts stop reacting to activations and triggers (world-state sync step 5, slice 1, 2026-10-21); other events still run on both games | `docs/research/player-events.md`, `scripts/spikes/` |

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
  `IsCopy()` checks.
- Planned (2026-10-19, revised 2026-10-20): copies carry their owner's AI state (current package, sitting,
  sleeping, scene, combat state and targets), simulated only by the owner, read 1:1 by anything that asks:
  written into the fields readers read, `IsInCombat`/`GetCombatState` hooked (`copy-contract.md`, "AI state";
  IDA notes in `actor-ai-and-copies.md`, "Combat state and its readers"). First for shared dialogue's step
  sync (above). Built for vanilla; graph events from outside the actions wait for mods.

## Test suite

- Beast form (2026-10-20, `docs/re/systems/papyrus-guest-scripts.md`, "Beast form"): `client-beast-form-stays-client`
  (3 of 3) shows upstream's beast form bug gone: the change runs on the guest's game only. Fixed on the way:
  a copy respawned for a race change started from the host's record of its owner's last cell entry (stale
  values, graph state and action); the owner's respawn now carries its state (`RequestRespawn::HasState`,
  bridge and encoding tests). Harness: `script_var`, `script_stacks`; `stop` finds games launched from
  another worktree (resolved prefix paths). Passed with it: `death-host-npc`, `late-join-client`,
  `hands-survive-reload-client-player`, `steady-state`, `form-state-round-trips`, the `scripts` tag but the
  two loose ends.
- Players' copies travel with the party (2026-10-14, `df9af2ff`; `docs/design/copy-contract.md`). A copy
  of another player is persistent and moved with the party, never dropped and spawned again on travel;
  the host sends only its actors on an arrival. Found by a host crash in `party-fast-travels`: a deleted
  copy's form id was reused for a Cow, which the copy code then moved, voiced and would have deleted
  (`docs/re/crashes/2026-10-14-reused-copy-form-id.md`); a copy is now the actor at its form id only while
  flagged a copy. Each game kept one actor for the other player through about 30 arrivals (spawned again
  only by the scenarios that disconnect). A copy's inventory is never reset to its base's template
  (`HookResetInventory`, Actor.cpp): enabling a copy's 3D removed and re-added its default outfit, which
  stripped the owner's clothes for up to 3 s (`load-door-not-replayed` failed once on its desync check)
  and sent the removals to the owner. Checked by disabling and enabling a copy: its worn set stays its
  owner's, before the hook it lost all but one item. Passed: the travel scenarios (`party-fast-travels` 3 of 3), the
  equipment scenarios (`equip-*`, `repair-equipment-drift`, `draw-*`, `hands-survive-reload-*`),
  `steady-state`, `late-join-client`, `host-rejoins`, `session-roster`, `host-kicks-client`,
  `move-host-npc`, `death-host-npc`, `party-shows-levels`, `copy-ends-voice-line`, `dive-*`,
  `map-marker-reaches-everyone`, `weather-follows-host`; `BridgeTests` 670 assertions (arrival cases
  rewritten: no player spawns, the host's actors to a player ahead once the host arrives).
- Party tether (2026-10-14, `df9af2ff`): anyone who travels takes the party along. `party-uses-door`
  (the host leads out of the inn, the client back in), `party-fast-travels` (each leads once from the
  river to Riverwood, in one worldspace), `load-door-not-replayed` (the host follows the client out by the
  tether, never by the door), `party-changes-cells`, `party-spreads-on-arrival` (now `party-stacks-on-arrival`), `teleport-keeps-copies`,
  twice each; `dive-*`, `map-marker-reaches-everyone`, `weather-follows-host` (they use `both_go`, which now
  moves the host only). `dive-host-player` 6 of 7: once a lock-order report, a new site of the known
  vanilla `coc` detach, read and added to `scripts/lock_order_known.txt`; once the copy's own gasp (below).
  `BridgeTests` 664 assertions (two tether cases), `TPTests`, `ClientReplayTests`.
  `party-quick-travel` (new): the client goes out of the inn and straight back in while the host's game is
  stalled (SIGSTOP, `game_stalled`). It failed first (the host saw the second travel as "already there"
  while its move out was pending, and went out alone); 3 of 3 since a follower keeps following until it
  reaches the latest place. The travel scenarios again after the fix: `party-fast-travels` once a new
  vanilla lock-order report (an NPC's movement against `Job_Combat_manager`, our pass-through
  `HookApplyMovement` on the stack; read and added to `scripts/lock_order_known.txt`), then 2 of 2; the
  others pass.
- A harness request with a wrongly typed field (`place` with a hex string target) took the game down: the
  command's `nlohmann::json` read threw (2026-10-13). Fixed 2026-10-14 (`3a9c8930`): `HandleCommand`
  refuses it (`bad request: ...`, the field's expected and actual type) and the game carries on.
- Kill moves by the client (2026-10-13, `437a6bb6`): `client-kill-move` 5 of 5 (new). The client's player
  kills the host's wolf with a kill move: on the host the client's character and the real wolf play it as one
  pair ~340 ms later, at the same pace (blows within 50 ms of each other, counted from each start), laid out
  as on the client, and the host records the client's character as the killer. Three fixes
  (`docs/re/systems/actor-ai-and-copies.md`, "Kill moves by the client"):
  - The victim's half on the client: `HookPerformAction` takes a copy's action aimed at the local player
    during the player's own action (never sent). On the host the paired replay carries `TargetEventName`
    when the partner is the host's own actor and starts on the game thread.
  - The host's pair waited ~3.1 s for the copy to reach its mark, which a copy cannot walk to: a copy's
    half now counts as at its mark (`HookUpdateSynchronizedClip`, `BSSynchronizedClipGenerator::Update`
    63050). Without it the scenario fails (the host's blow 4958 ms in against 1999 ms).
  - Kill credit: the owner no longer applies a forwarded hit to an actor held by a kill move (the engine
    gave the pending kill to its local player); the kill move credits its partner. Without it the scenario
    fails (killer 14). `client-melee-kill-credit` now also accepts a blow that becomes a kill move: 4 of 4,
    each ending in one.
  `combat` tag (11), `death-host-npc`, `health-host-npc`, `host-kill-move` 3 of 3 more pass; `BridgeTests`
  639 of 639, `TPTests`. Tools: harness `idle_conditions` (each condition of an idle and its parents,
  `TESConditionItem::IsTrue` 29924) and `kill_move_check`'s `attack_target` (the engine's melee pick 47272).
  Since (`437a6bb6`): `idle_conditions` also gives each idle's event and whether its whole list holds
  (`TESCondition::IsTrue` 29888); conditions and idles in `docs/re/systems/conditions.md`.
- Kill moves (2026-10-13, `5a58a51c`): `host-kill-move` 10 of 10. The host's player kills a wolf with
  a kill move (`KillMoveRandom` 100); on the client the host's character and the wolf's copy play it as
  one pair (the engine's kill-move flag on both in one frame) about 330 ms after the host, laid out as on
  the host (distance, bearing and heading within 15 units/degrees, both kill-move variants), at the same
  pace (the blow within 350 ms of the host's, counted from each start), the wolf's copy dying within
  350 ms of its blow, and the client's own player untouched. Three fixes:
  - Action targets go as session ids (`ActionEvent::TargetActor`): the target was the owner's form id, so
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
- Animation trace (2026-10-13, `5a58a51c`): `AnimTrace` in `coop_test.py` and the harness's
  `anim_trace` command (`Code/client/TestHarness/AnimationTrace.cpp`). For chosen actors in both games
  it records every behaviour graph event (a sink on each graph's `BSAnimationGraphEvent` source), the
  actions taken, life state, kill move, health, graph booleans and position each frame, with the wall
  clock and frame; both screens; and the plugin logs' lines naming them. It writes one timeline (host
  left, client right), `trace.json`, the videos and side-by-side frames at marked moments to
  `build/coop-test/traces/`. Scenarios can wait on its entries (`trace.wait`, `trace.wait_state`). Each
  game's kill cam and clock speed are entries too.
- Kills (2026-10-13, `5a58a51c`): `client-melee-kill-credit` and `client-spell-kill-credit` 10 of 10
  each, and now wait for the body to come to rest, the copy's within 50 units of its owner's. The
  host takes a forwarded hit through the engine's damage path with the attacker
  (`SetActorValueModifiers` 38468): its death rules, a death animation, the killer recorded at once.
  It used to force the damage without an attacker (the engine queued a killer-less kill) and then kill
  with an instant ragdoll that threw the body and dragged the copy through the air. Copies are in
  deferred kill (as skymp's remote actors): they show their owner's health as is and die only with
  their owner. The client stands in front of the wolf's head.
  `combat`, `magic`, `values` tags 18 of 18; `BridgeTests` 589 of 589. Notes:
  `docs/re/systems/actor-ai-and-copies.md` ("A hit forwarded to the owner").
- Kill cams (2026-10-13, `e4088aca`): `client-spell-kill-credit` passes 10 of 10 (a kill cam in 8):
  the client's killing bolt sometimes plays a kill cam (game clock at 0.25x, player controls off,
  `VATS::mode` 4, 400883), and the reset's one Ready Weapon press fell in it. `takes_input` now also
  waits for the kill cam (status `kill_cam`), and `place_before` for the landing animation
  (`bInJumpState`; a draw pressed in it stalled ~3 s).
- Mod ids (3d, 2026-10-13, `da15a6be`): `BridgeTests` 589 of 589 (ids are places in the load order,
  the same for every player, under each player's own names; written against the old table first,
  which failed on a name differing in case). On the real games 43 scenarios across `session`,
  `restart`, `spawn`, `world`, `objects`, `cells`, `inventory`, `magic` and `combat` pass, all but
  `client-spell-kill-credit` (Loose ends; it fails the same without 3d).
- Player records (3c, 2026-10-13, `f438ba48`): `BridgeTests` 582 of 582 (every test's players now
  join through the real join; the list split out of `HostSession` as `HostPlayers`, and the 28
  passed again on freshly started games); on the real games `session`, `restart`, `spawn`, `world`, `objects`
  and `cells` (28) pass after 3c.1, and `session` and `restart` (11) after the disconnection handler.
  Kicking is covered by `host-kicks-client` only: the headless server has no network peer to drop.
- Player list (3b, 2026-10-13, `75cc39f5`): `BridgeTests` 415 of 415 (who a newcomer learns, with
  the host marked; who learns the newcomer; leaving; a refused game never listed); the `session` and
  `restart` scenarios (11) pass; without the leave notice the bridge case fails, and on the real games
  `host-kicks-client` fails at "[host] no longer lists the client".
- The host's settings (3a.4, 2026-10-13, `755891ca`): new `difficulty-follows-host` (the client
  takes the host's difficulty, follows its change, is held there, gets its own back on leaving; the
  host's is never changed) passes, and fails at the step concerned without the change notice and
  without keeping the client's own; the `session` scenarios (10) pass; `BridgeTests` 389 of 389.
- Plugins and load order with real games (3a.3, 2026-10-13, `5bd486ca`): new
  `join-refused-plugins-differ` (tag `restart`, ~30 s: the client game restarts with an extra empty
  plugin, the real host refuses it, the client's join status lists `extra SkyrimCoopTest.esp`, the
  host's chat says why; the host must drop the killed client within enet's timeout) passes, and
  fails at that step without the timeout handling. The `session` and `restart` scenarios (10) pass;
  `BridgeTests` 378 of 378; `TPTests` pass. Seen in a screenshot: the host's chat line.
- Game versions (3a.2, 2026-10-13, `5bd486ca`): `BridgeTests` 365 of 365 (Skyrim and SKSE
  mismatches, the build checked first, the wording), `TPTests` pass (the new fields round-trip);
  `join-refused-shows-why` adds a stand-in host on another Skyrim and SKSE and a game on another Skyrim
  asking the real host, and fails at that step with the host's check skipped; the `session`
  scenarios (9) pass. The real games send Skyrim 1.6.1170 with SKSE 2.2.6.
- Refusals as data (3a.1b, 2026-10-13, `cf0ad03b`): new `join-refused-shows-why` (the client refused
  by stand-in hosts on another build and full, the real host refusing a game on another build)
  passes; it fails at the step concerned with the old kick, with no refusal status, and with no host
  chat line. The `session` scenarios (9) pass; `BridgeTests` 349 of 349, `TPTests` pass. The scenario
  needs `scripts/xmake-linux.sh build -j16 coop_peer`.
- Join gate 3a.1 (2026-10-13, `cf0ad03b`): `BridgeTests` 339 of 339 (new `session_tests.cpp`), `TPTests`
  pass; the `session` scenarios (8) and `move-host-npc`, `remove-host-npc`, `party-changes-cells`,
  `door-opens-for-both` pass after a hot reload (which joins both games through the new gate).
- Map markers (2026-10-13, `b7b83aad`): `map-marker-reaches-everyone` (place both ways, remove) 3 of 3 on
  a warm duo and 2 of 2 on a fresh one, `chat-reaches-everyone` passes with `key` through the control map, `BridgeTests` 284 of 284.
  Test games had tutorials on all along: `bShowTutorials` went to SkyrimPrefs.ini, which ignores it; it
  is in Skyrim.ini now (the map's first-open tutorial took the next key).
- Relays, object ids, chat (2026-10-13, `b7b83aad`): `BridgeTests` 284 of 284; the `objects` and `session`
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
- Bridge rules: `BridgeTests` (`Code/tests/bridge`), the host's rules with in-process games on `HostNetwork`, wire
  bytes pinned in `wire_tests.cpp`. Build with `scripts/xmake-linux.sh` (clang 18, lld; since phase 5).
- Encoding: `TPTests` (`Code/tests/encoding.cpp`), same build.

## Loose ends

Everything known to be unfinished, unverified or unexplained. Close an entry by fixing it, proving it
harmless, or moving it to `docs/ideas.md`; say where the evidence went. Details are in the doc named.

### World state

- **Done 2026-10-20: a guest's story manager starts no quest.** `BGSStoryManager::ProcessEvent` (32372) is skipped while a guest, so its story events (arrivals, kills, script events) are handled with none started (`docs/re/systems/story-manager.md`, scenario `guest-story-starts-no-quest`). Still open: quests a guest's own scripts or console start (`EnsureQuestStarted`), with step 5; forwarding a guest's story events to the host (`docs/research/player-events.md`, spike 4).
- **A guest's quest fills are not in its own save** (2026-10-20): `ForceRefIntoAlias` records no change; harmless while a guest never saves in a session (`join-flow.md`).
- **The save's running bounty was not running on one boot** (2026-10-20, `LateAfterDragonslayerBackinSkyrim`): `quest-walk BQ01` found it stopped where earlier boots had it at stage 0; not looked into.
- **Step 5 after slice 1** (2026-10-21; superseded the same day by option 1, under "Tests and harness": a guest runs only its own player's and UI's scripts, so the world-script cases below are closed except what bypasses the gate). Of delayed natives only `Spell.Cast` carries the guest whose script called it (`GuestScripts`, `CastFunctor`); the others do not. Not measured: an engine-only hazard (no script) hitting a guest on both games now hurts it twice (the host's goes to its owner), and a host NPC's hostile spell on a guest now reaches it (the PvP rule covers players' casts only; an NPC's arrow hit the guest in `statue-fires-at-client`), no scenario of its own. The host's messages for a guest's action (`BlessingMessage.Show()` for the guest's prayer) show on the host's game: rule 5, not built. Enabled state (2026-10-21): a guest's own timers and loads can still enable or disable references on its game until step 5 covers them; a guest joining later gets no enabled states (save transfer at join will carry them); actors are left to their owner. Locks (2026-10-21): a guest's own world scripts (timers, loads) locking something are proposals too until step 5 covers them; a guest's lockpicking is not checked against the host's lock (it is taken as is).
- **Done 2026-10-21: a guest's loads no longer set the host's locks.** `AddLockChange` runs on cell loads and from scripts, not only for a player's lockpick or key; a guest now proposes only its player's own (`ScopedPlayerLockOverride`: the lockpick menu's unlock 51968, the player's activations), `guest-loads-propose-no-locks`. The host still sends all of its own on each load (dozens, mostly the same state): the change funnel's per-form "last sent" cache would drop those.
- **Done 2026-10-21: console lines on a reference run.** Found when the harness began failing on the console's errors (`docs/re/systems/console.md`): `ID.command` never ran for an id starting with a digit (`Script command "1A695.getav" not found.`), so `vilkas-counts-client-hits`' clean-up (`1A695.removefromfaction`, not a console command either: `setfactionrank … -1`) had never run. The harness now runs a `<hex id>.<command>` line with the reference as its target. The full suite was not run with the stricter console: other scenarios' silent no-ops may show.
- **Done 2026-10-21: a copy no longer takes the engine's weapon pick when it loses an item.** An item leaving an NPC makes the engine equip the best weapon it carries; our removal hook let that through on copies (`ScopedEquipOverride`), so the guest's copy held a staff its owner only carried (`sword-box-asks-client`, 2 of 7). Now `ScopedItemRemovalOverride` allows the copy's unequip only (`docs/re/systems/actor-ai-and-copies.md`, "A copy's equipment"). The scenario carries the staff and fails at once without the fix.
- **`form-state-round-trips` fails as the first scenario on a fresh duo** (2026-10-21, also with the main tree's DLL): the client's character base saves 59 bytes, the host's copy of it 54; after other scenarios it passes. Not looked into.
- **Containers after slice 1** (2026-10-21, `docs/re/systems/guest-world-events.md`, "Containers"):
  - Counts per base item: an enchanted, tempered or poisoned item taken from a container comes back plain in the other games' containers (the player's own inventory keeps it; actor inventory sync carries extra data).
  - A container's leveled contents roll per game (`InitLeveledItems` at first open, which sends no event): a rolled item the other games lack is taken there as a count of 0, and items only the guest rolled are not in the host's container until the guest takes them. Slice 2: the host's contents when a guest's cell loads.
  - A guest's quick second change to the same item can be undone for a moment: its first proposal's echo (the host's count) comes back after the second, then the second's. Converges; not seen in play.
  - A guest's own world scripts (timers, loads) changing containers are proposals too until step 5 covers them. Merchants' chests go the same way (shared stock), not tested.
- **Quest walks skip what leads up to a stage** (2026-10-20): travel, fights and dialogue between stages are not played; a guest's actions moving a quest come with step 5, inside the same walks. Without a later save the walk takes stages in record order and stops at the first branch that ends the quest (`DA10` at 65).
- **`coop_test.py stop` misses games from a deleted worktree** (2026-10-20): their prefix is under `.claude/worktrees/<name>/build/coop-test`, which `uses_prefix` does not match; they hold the shared game install and the next `duo` waits behind them. Killed by hand this time. Matching any checkout's test prefix would also stop another live session's games.

- **A guest's wait or sleep passes no time** (2026-10-20): its `Calendar::Update` is off, so the menu counts down and nothing changes. Meanwhile the host's wait jumps everyone's clock while the others play on. A shared wait is in `docs/ideas.md`.
- **A guest's Days Passed stat stays its own** (2026-10-20): the DaysPassed event fires only from `Calendar::Update` (`docs/re/systems/calendar.md`).

### Copies and actor sync

Notes: `docs/re/systems/actor-ai-and-copies.md` ("Still open"), `docs/design/copy-contract.md`.

- **A host NPC leaving the cell leaves the client's copy behind as the client's own actor** (2026-10-20): a copy of a placed NPC is the client's own reference; when the host's NPC goes, the client keeps it where it stood and treats it as its own (a ghost, and a client running a host NPC). Cause, cases, repro and options: `docs/design/copy-contract.md`, "When the owner's actor goes away". Only the test works around it (`release_host_npc`).
- **A guest's player is never in combat** (2026-10-20, read in IDA, not seen in a game yet): the engine counts it
  in combat from the combat groups targeting it (41238, 41261), and the host's NPCs on a guest are copies with
  none. Expected in play: no combat music on a guest and nothing stopping a wait or save mid-fight (the
  checks reading the flag not traced). Fix planned with AI state on copies (`copy-contract.md`, "AI state").
- **A copy's spawn writes its NPC base's factions** (2026-10-19): `SetActorFactions` (`RemoveFromAllFactions`, then `SetFactionRank`) changes the shared `TESNPC`, so the client records `NPC_ Factions` change forms the host never did (168 in one fast travel). Harmless while the values match the host's; it is client-made world state in its save (`docs/research/world-state-drift.md`).
- **The wolf's copy holds its last pose ~0.6 s after a client's kill move** (2026-10-13): it dies with its
  owner's death action, which comes the host's pair lag (~0.35 s) plus the interpolation delay after the
  client's own `KillActor`. `client-kill-move` allows up to 0.95 s. Watch whether it reads as a stall.
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
  scattered `IsCopy()` checks.
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
- **Arrival spacing** needs navmesh under the marker: QASmoke's `coc` marker has none (players stay
  stacked there). Doors and fast travel arrive through the same `TravelService` arrival; no scenario asserts the
  spacing there yet.
- **`TESObjectREFR::GetParentCell` (our virtual)** returned null in an exterior after `coc` while the
  field was set: the vtable slot may be wrong. Nothing of ours calls it any more (the harness's object
  list reads the field, `GetParentCellEx`, since 2026-10-13).
- **Copies converge on their owner for presence only.** State that still arrives only as events (dead or
  alive, inventory beyond equipment, the host's roster of characters, object state) stays wrong if one is
  lost. Add each to the same check when a scenario shows it drift
  (`docs/design/host-authority-migration.md`, "Copies converge on their owner").
- **Outdoors the host counts a player "with" it by worldspace:** a player still far off (same worldspace)
  gets the host's actors there, which the engine deletes; presence recovers them once they meet (an extra
  spawn round trip). Players' copies are no longer affected (they travel with the party). Tighten to the
  loaded area if it costs anything.
- **Copies of the host's placed NPCs come back unflagged after travel** (2026-10-14): the client's copy of a
  river wolf (`DC8DF`, a placed reference) was kept on a fast travel to Riverwood (the arrival keeps copies
  in the same worldspace), and on the arrival frame it was no longer flagged a copy; the identity check
  respawned it as one (correct, one request per copy). Whether the engine made a new actor or something
  cleared the flag is not traced. Before the identity check such a copy came back as a plain actor running
  its own AI.
- **A copy's engine-made inventory changes reach its owner** (2026-10-14): the add and remove item hooks
  send every change not made by us (`ScopedInventoryOverride`) as an inventory change, and the host relays
  those from anyone (looting a host NPC's body changes a host actor). An inventory reset on a copy removed
  the host player's own clothes that way (seen in an A/B with the reset allowed). The reset is closed
  (`HookResetInventory`); any other change the engine makes to a copy's inventory would still go out. The
  remove reason (`ITEM_REMOVE_REASON`) may tell engine housekeeping from a player's action.
- **Which in-game flag keeps a copy out of the save** is not traced: `Actor::Create` sets all of
  `TESForm::inGameFormFlags` (`0xFFFF`, upstream). It works for persistent copies too (checked by booting a
  save made with one, `docs/design/copy-contract.md`).
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
  owner's gasp arrives could gasp on its own: seen once (2026-10-14, `dive-host-player`, 1 of 7 runs: the
  host's copy had a line of its own with no length before the owner's gasp).
- **Crash in the harness's `strip`** after 376 hot reloads (`UnequipAll` on a copy, garbage list
  node). Not reproduced on fresh games (5 of 5).

- **An NPC on the guest only** (2026-10-20): after play in Whiterun the guest showed an NPC at a door that the host's game did not; `desync` listed `Ulfberth War-Bear [client 000D15B0, sid -1]: copy in client with no owner in the other game`. Not looked into; the guest's own actors are the copy contract's.

### Engine threading and crashes

- **Done 2026-10-21: the guest crash and deadlock blamed on hot reload and log rotation** (a crash in
  `GuestWeather::OnUpdate` → `GuestEngine::HoldWeather` → `ModSystem::GetGameId` with a bad map,
  `crash-2026-10-21-15-50-41.log`, and a deadlock as `host-rejoins` ended the session) were the guest services'
  `UpdateEvent` handlers connected and dropped during the dispatcher's own dispatch (entt 3.10 iterates its
  handlers in place): `GuestService` now forwards the update to them. Rotation works (`tp_client.1.log`,
  `.2.log` in the game's `Data/SKSE/Plugins/logs`); a run folder's copy of the logs is taken at the duo's start,
  so a live report is read from the game's own log.

- **Host crash in `TaskQueue::Drain`, 2 s after a save** (2026-10-19): the Runner queue looked overwritten; not reproduced in six more saves (`docs/re/crashes/2026-10-19-runner-queue-after-save.md`).
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

- **Fixed 2026-10-20: the client's `GameDaysPassed` missed host time jumps** (measured 2026-10-19 after `party-fast-travels`: 0.4376 / 0.3866). `TimeModel` carries the whole days passed; a guest writes the host's calendar as `Calendar::Update` does, `rawDaysPassed` included (`docs/re/systems/calendar.md`). Its date is the host's too: `SyncPlayerCalendar` is no longer read. Scenarios `host-wait-reaches-guest` (the Wait menu across midnight), `late-join-gets-calendar`, `party-fast-travels`. Also fixed: a guest rejoining ran ahead by the time it was away (`CalendarService` counted the gap).
- **Fixed 2026-10-20: set-time set nothing.** `SetGameTimeHours` was a stub, and only the compiled-out CEF overlay sent the command. Now `/settime <hours> <minutes>` in the RmlUi chat sends it; the host runs its clock on to that hour (the next day's if earlier) with `Calendar::Update`, as a wait does (`HostEngine::SetHour`). Scenario `guest-sets-time`.
- **Tether gaps** (2026-10-14, `docs/design/host-authority-migration.md`, "Party tether"):
  - Followers start once the leader has arrived: the trip takes the leader's load and then their own.
    Moving them as the leader leaves (40442, the queued move) would overlap the two.
  - A client's fast travel passes time only on its own game; the host follows by `MoveTo`, so the
    party's clock (the host's) skips the trip and the client is set back to it.
  - Every travel of a player's own leads: a scripted move on a client (a quest teleport, jail) takes the
    party along too. Within a cell or the loaded grid nobody moves.
  - Two players leading at once to different places can end apart (each game follows in the host's
    order; a leader may follow the other one afterwards). Not seen; the fix would be the host keeping the
    party's last destination and sending a follower that arrives elsewhere on to it. One leader travelling
    again before the others got there is handled (`party-quick-travel`).
  - A follower whose tether move never arrives stays "following" for 60 s: a travel of its own in that
    time does not lead.
  - A player joining somewhere else is neither pulled nor pulls (joining goes through the join flow).
  - Not tried: a follower in a menu, in dialogue, dead or mounted; three players.
- **The old CEF overlay and its Angular UI (`Code/skyrim_ui`) are still in the tree**, compiled out of the
  Windows build: the RmlUi panels replace them (party, chat).
- **Quests** need their own design before sync goes on.
- **Build ids of uncommitted trees match each other.** `BuildCommit()` is `git describe --tags
  --always --dirty`, written on every xmake run (`Code/common/xmake.lua`; it was configure-time only
  and lagged commits until 2026-10-13): two different uncommitted trees on the same commit both read
  `...-dirty` and pass the gate's stage 1. Fine for development; release builds come from commits.

### Tests and harness

- `dive-host-player` failed 1 of 2 on a fresh duo (2026-10-21): the copy's animation event `SwimStart` vs the
  owner's `swimStop`; once left the copy showing visual `77785`. Actor sync, not followed up.
- `late-join-client` failed once on a fresh duo (2026-10-21): the client's copy of the host's player kept the
  baseline heading (4.71 vs the host's 2.55); passed 3 of 3 after.
- `late-join-gets-cell-state` failed once on a fresh duo (2026-10-21): the same copy heading (the host's player
  at 1.66 vs 4.73), then 4 of 4.
- A placed iron ingot (`created_reaches_guest` with `5ACE4`, by hand) came to rest with changes `0x3` on the
  host and `0x2` on the guest (2026-10-21): a form-flags record (`0x1`) the guest's twin lacks. Not followed
  up; the scenario places the cell's ladle, which never showed it.
- Scenario time (2026-10-21): the world check after each scenario and after its reset looks once before
  sampling the idle animations (about 0.1 s instead of 1.3 s or more), so a trivial scenario takes 1 s, not
  3.5. The long ones are game time: `late-join-gets-calendar`'s waits (the Wait menu passes about an hour a
  second, 36 h in all, ~40 s; its 24 h wait found the demotion record gap) and `world-changes-reach-guest`'s
  placed ladle rocking ~20 s before it rests.
- Done 2026-10-21: `vilkas-counts-client-hits` failed on a lock-order report marked ours (twice, then passed:
  a report fails only the first scenario to hit it in a game's life). Both stacks read (the live log keeps
  them; the run folder's copy is taken at the duo's start): an engine inversion between a quest alias fill
  adding a spell (the VM relays the magic event under the alias array's lock) and the Wait menu's autosave,
  with only our pass-through hooks on the stack. In `scripts/lock_order_known.txt`.
- **Guest scripts, option 1, built 2026-10-21** (`docs/research/guest-scripts-and-mods.md`): a guest runs only the
  scripts its own player or UI caused (and what they start). Open: the UI lane is untested (no SKSE script mod in
  the test install); fragments and stacks resumed from a save skip the gate (no `SendEvent`); a guest's player-alias
  events (location changes, item pickups) are dropped and not yet raised on the host (party model rule 4).
- **Quest fills, 2026-10-21:** a fill of a reference the guest never loaded is read from its plugin as the host's
  quest did (15328), `quest-fill-loads-on-guest`; a created reference in a fill maps to the guest's twin
  (`quest-roles-match-host` now compares them by base); location aliases are synced (`QuestState::Locations`,
  25054/25067), no scenario (no console-started quest fills one in the test setup). A join loads every running
  quest's unloaded references at once: not measured on a late save.
- **Created objects, 2026-10-21** (critters stressed them): a deleted one is found by sweeping the ones sent each
  update (a script's `Delete()` often records nothing), an id reused in one update sends the old one's deletion
  first; dynamic persistence (`0x400`) and a look-at form-flags record (`0x40000`) are each game's own (oracle).
- Not done from the 2026-10-21 list: enchanted, tempered or poisoned items taken from containers still arrive plain
  (counts per base item); the client sometimes misses one door `open` event in `world-changes-reach-guest` (2 of
  ~10 runs, passes on rerun), not looked into.
- Regression 2026-10-21 evening (55 world, session, script and quest scenarios, fresh duo, 434 s): 53 pass.
  `form-state-round-trips` (the fresh-duo loose end above) and `late-join-gets-ring-pose` on a guest lock report,
  `+0x15584d > +0x155859` against `+0x154dce > +0x154de0`: the engine's inventory functions 11594/11591 again
  (see `lock_order_known.txt`), at other sites; its stacks were not in the log, so not added to the known list.
- **A guest whose game restarts keeps some of its own state at join** (2026-10-21, found in the phase 5 runs):
  `join-refused-plugins-differ` restarts the client's game, and after it rejoins (a) put-back states the host's
  cell answer leaves out (containers changed and refilled, `changes 0x20`; a door unlocked at level 50) and (b) a
  quest its own game started before joining that the host has stopped (`CR03`: the host sends no stopped quest) stay
  as the client's. Later scenarios then fail on the leftovers (`world-changes-reach-guest`, `quest-roles-match-host`).
  Save transfer at join would cover both.
- `difficulty-follows-host` failed 2 of 8 on the phase 5 build (2026-10-21), always after other scenarios: the
  client's copy of the host's player kept the baseline heading after the two quick rejoins (the late-join facing
  entries above). `HEAD` before phase 5 passed 6 of 6, the same orders included; not settled whether phase 5 makes
  it likelier. Likely the spawn heading: `SetActorRotation` was a stub, so a copy kept its created heading until its
  owner moved; implemented the same day, then 3 of 3 passed right after the scenarios that had tripped it.
- `burning-shows-on-copy` fails its world check about half the time (2026-10-21, 3 of 6 after the `TiltedPhoques` steps;
  it passed the run before them): each game's own fire hazard from the spell's impact (`HAZD` 3E075, a created
  reference) is still there when the check looks, at a different spot in each game. Either hazards are each game's
  own transient effect and the check should skip them like projectiles, or the host's should be the only one;
  not decided.
- `party-stacks-on-arrival` fails every run (2026-10-21; with copies spawning at their owner's heading, 2 of 4 passed): "[client] came within 9 units of the other player: it
  walked through it". Not followed up.

- Rule 2 through `SetStage` has no scenario (2026-10-20): `vilkas-counts-client-hits` now claims rule 1 only (the trial's sword is given at stage 200, from dialogue). Candidate: Northwatch Keep's `MS09NonCombatTrigger1Script` (`OnTriggerEnter`: `Game.GetPlayer().SetFactionRank`, then MS09 stage 55, whose fragment ranks `Game.GetPlayer()` too); needs a faction readout and MS09 staged at 40. `statue-fires-at-client` checks the host's own hits now and fails with rule 2 off (`docs/re/systems/papyrus-guest-scripts.md`, "Rule 2 never ran").
- The client's fire statue in Soljund's Sinkhole fired though `6AE8C.disable` ran on the client (2026-10-20): its script checks `FPStatueA.isEnabled()`. Not followed up; the scenario no longer disables it (step 5 stops the client's trigger).
- The guest's view of a script's object animation (a claw door's ring turning, from the host's `NotifyObjectChanges`) has no harness readout: `ring-turns-for-client` checks that the guest's own ring script stays, not that its ring turns (2026-10-21).
- `load-door-not-replayed` reported a lock-order inversion once (2026-10-20 night, host: `+0xe85437 > +0x17928d` against `+0x16abbd > +0xe92c16`, ours on the stack), next to known patterns; passed on rerun. Stacks not read.
- `Projectile::LaunchData` (`Games/Skyrim/Projectiles/Projectile.h`) names `+0x50` `fYAngle`; CommonLibSSE-NG has a pointer there (`unk50`) and `desiredTarget` at `+0x58`. Projectile sync sends it as `YAngle`. Not checked in IDA.
- A failed `sword-box-asks-client` run leaves the secret door open on the client, so the next run fails at "not closed to begin with" until a fresh duo.
- The lock-order checker reported two inversions on the guest during a beast form change (2026-10-20, `tp_client-client.log`: the guest's character's reference lock, `SkyrimSE.exe+0x16d4cb > +0x182241` against `+0x182315 > +0x17928d`, one marked ours). Not followed up.
- Passes again (5 of 5 on 2026-10-20 evening, client-led trip included; cause not identified). Was: `party-fast-travels` failed at the client-led trip (2026-10-20): the client's console `fasttravel` runs and nothing happens, and the step "[client] arrived somewhere new" times out. The same on HEAD `30cd815f` without the step-3 changes; not followed up.
- A release-mode client build (`-m release --test_harness=n`) fails to link: `/opt:ref: no such file or directory` (2026-10-19, seen while checking a player build; `-m dev --test_harness=n` links).
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
