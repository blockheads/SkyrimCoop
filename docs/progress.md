# Progress

Where SkyrimCoop stands. Update it when a phase, system or known failure changes.
Plans live elsewhere: the migration in `docs/design/host-authority-migration.md`, joining a session
(build, game and mod checks, host-stored characters, arrival) in `docs/design/join-flow.md`, how the
party shares one story (the party is the Dragonborn) in `docs/design/party-model.md`, later ideas in
`docs/ideas.md`, engine findings in `docs/re/`, the words (host, guest, owner, copy, player) in
`docs/design/glossary.md`.

**Goal:** friends play Skyrim together (up to a Steam lobby's 250 since 2026-10-29: `PartySize.h`), the host's game is the only authority (direct
authority), and the Skyrim Together server is gone. Peers own only their own player; the host owns
every NPC and the world.

2026-10-29: party dialogue built (uncommitted): every menu shows every player's options tagged with who can say them,
one shared highlight, a player says only its own options ([shared-dialogue.md](design/shared-dialogue.md)).
Fast tiers and the Skyrim dialogue tag pass (the party list, the shared highlight, a question answered by the guest).

2026-10-29: the v1 list ([v1-playtest-todo.md](v1-playtest-todo.md)) gained first-time join (Quick Start) and a
save/load gate; the werewolf crash moved to `docs/bugs.md` (after v1). The party limit is a Steam lobby's 250 (`PartySize.h`,
was 4 on Steam, 8 on the host); only up to 4 players have ever been tested.

Bug reports from play (2026-10-29, uncommitted): `/bug`, `/export`, crash bundles at the next start and `coop_test.py report` ([bug-reports.md](design/bug-reports.md), [playtest.md](playtest.md)); the harness status and log trail now come from the shared `Code/client/Diagnostics/`. Fast tests pass; the plugin builds (test and player builds). `bug-report-writes-every-game` passed in Skyrim before screenshots were dropped and saves kept once per export; that change awaits a rerun on a free duo.

Last updated 2026-10-28: the party-scripts-2 and v1-session-items work is integrated into dev, uncommitted. Crafted potions (v1 checklist) built in `.worktrees/crafted-potions`, uncommitted.
The combined fast tests and Windows plugin build pass. Review findings and verification limits:
[integration review](v1-integration-review-2026-10-28.md). Agreed remaining work:
[v1 checklist](v1-playtest-todo.md). Earlier worktree references below record where each feature was built.

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
| after | Physics sync on the bridge's unreliable channel | objects and ragdolls live from the host, a guest's pushes, casts, grabs and drops to the host, 2026-10-24 (`docs/design/physics-sync.md`, steps 1-6) |

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
| Experience: personal, earned by whoever did it; party rewards to everyone (`docs/design/party-model.md`, "Experience") | rule decided 2026-10-19; wiring below. quest rewards built 2026-10-27 (`design/quest-rewards.md`); measured combat XP sharing removed |
| Objects: activation, locks, script animations | done as bridge relays by form id (`31abfd23`, `550ece84`); the unused object ids deleted (`b7b83aad`) |
| Shared dialogue: one conversation for the party, run by the host's engine; each player sees their own options, anyone picks with their own line | parked 2026-10-19 until quest/world sync and AI state on copies. In: the host's rule and messages (`HostDialogue`, `dialogue_tests.cpp`), copies' own lines and subtitles, the engine structs (`MenuTopicManager`, `UIMessageQueue`). The client (`DialogueService`, `DialogueBridge`, harness `dialogue`, `dialogue_pick`, `skill`, 22 scenarios passing on 2026-10-19) is on tag `archive/shared-dialogue-client` (was a branch): it syncs the host's list to each peer, to be redone as step sync (below; `docs/design/shared-dialogue.md`, "Next"). Engine notes `docs/re/systems/dialogue.md` |
| Quests | done 2026-10-20 as world state: the host runs every quest, guests take its stages (fragments skipped), objectives and alias fills (`docs/design/world-state-sync.md`, `docs/re/systems/quest-apply.md`); upstream's quest relay deleted with phase 5 |
| Waypoints, chat | done as bridge rules, server services deleted; chat is an RmlUi box (Enter), `chat-reaches-everyone`; player list health % and dialogue lines in chat dropped (`b7b83aad`) |
| Shared map markers: a marker placed on the map (P) shows on everyone's map | on again; placing and removing (the map's "Remove It", by mouse), `map-marker-reaches-everyone` (`b7b83aad`) |
| Teleport command, teleport to a player | dropped: everyone is already together under the tether (`b7b83aad`) |
| Tether itself: anyone who travels takes the party along | done (`df9af2ff`): the leader's arrival moves everyone else there, the host too; `party-uses-door`, `party-fast-travels`, `party-quick-travel`, `load-door-not-replayed` |

### Experience: wiring up

The rule and why: `docs/design/party-model.md` ("Experience"); engine path and the mods:
`docs/re/systems/experience.md`. Each step with its scenario. V1.0.0 uses the native game test setup;
installed-mod kill XP and multiplier compatibility are later work (`docs/ideas.md`, "Experience mods").

1. **Drop upstream's shared combat XP**: done 2026-10-27 with quest rewards. No measured gains leave a game;
   the relay and receiver, LastUsedCombatSkill and ScopedExperienceOverride/27244 are removed. Reserved legacy
   wire slots remain. `xp-stays-personal` replaces `shared-xp`.
2. **Fix `HookAddSkillExperience`'s signature**: done 2026-10-27, checked against CommonLib and disassembly:
   advance object and action forwarded. It emits only PlayerProgressEvent for character records.
3. **Speech from a picked line to the picker**: done 2026-10-22 by rule 2 (the line runs for its picker, whose game
   runs its `Game.AdvanceSkill`; `dialogue-picker-gets-speech-experience`), replacing the 2026-10-19 reroute. A
   script's skill gain outside a picked line (a quest's reward) is the party's (step 6).
4. **Barter Speech to the trader**: done 2026-10-27, with barter on the picker's game. Host/guest real purchases
   raise only the trader's Speech; dialogue closes before the barter menu takes input. Fast routing/flow tests and
   native evidence: `design/native-bugs.md`.
5. **Training to whoever pays**: done (2026-10-19; by rule 2 since 2026-10-22, `Game.ShowTrainingMenu` running on
   the picker's game): the cost, the skill and the trainings this level are the picker's (`dialogue-picker-trains`).
6. **Quest rewards to everyone**: built 2026-10-27 (`design/quest-rewards.md`), as the party's calls. Skill input,
   explicit skill levels, perk points, perks and permanent stat grants reach every present player's own engine.
   Real host/guest quest turn-ins check skills, perks, picker-only gold and native guest completion/stage events.
   Experience DLL itself is not installed/tested; its game's native event delivery is measured.

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
   (loose ends, "Created objects in quest alias fills"). Dropped items built on it 2026-10-26 (`dropped-items.md`): a guest's
   drop made by the host's game, its own reference adopted as the twin; pickups through the host.
4. **The rest of `unsynced`:** done 2026-10-21. Animation `0x10000000`: the graph row (`kGraph`, the graphs' save
   data, ~130 bytes each) goes with a cell's state only (live changes are the relayed animation events);
   `late-join-gets-ring-pose` (a claw ring turned while the client was away). Activating children `0x4000000`:
   not sent, the host's transient bookkeeping while a reference's activate-parent children run (19428).
5. **Physics objects' rest positions.** Havok moved `0x4`: when the host's body comes to rest (deactivated), its
   position and angle go out through the move row. Each game simulates the flight its own way, then the host's
   resting spot wins. Built 2026-10-21 (`Bridges/PhysicsRests`); since 2026-10-24 the flight itself is the host's too
   (live physics, `Bridges/PhysicsBodies`, below) and the rest is found from Havok's island mark. A guest's own push
   goes to the host as its impulse (physics sync step 5, 2026-10-24).

World sync is then done: every object ends where the host's is. Live physics (the flight itself, ragdolls, a guest's
push as an impulse) is its own plan after phase 5 (`docs/design/host-authority-migration.md`, "After: physics sync").
Then AI state on copies and dialogue's step sync (next section).

### Shared dialogue: sync the step, not the list

Decided 2026-10-19 (`docs/design/shared-dialogue.md`, "Next"; `docs/design/copy-contract.md`, "AI state"): each
game's engine builds its own list from the host's step, against world and AI state that is the host's. Each step
with its scenarios.

Steps 1-3 done 2026-10-22 (overnight slice 5, below), then replaced the same day by the host's list for every menu
("Dialogue: the host's list" below; `shared-dialogue.md`, "Decided 2026-10-22").

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

### Cutscenes: a player's own moment stays theirs (2026-10-23, uncommitted)

Research `docs/research/party-freezes.md`: 81 vanilla scripts take the controls (8 more only in comments), 25 of them
one player's moment (travel, fishing, the lute, traps, beast forms). Built (`docs/design/cutscenes.md`, "Whose
moment"):

- **The rule:** a controls or screen call whose stack a player's action started (GuestScripts' cause: a guest, or
  this game's player 0x14, now tagged on the host too) stays that player's. It is the party's when no player is
  behind it, or when a stage that player's scripts set runs it (`Cause::Story`).
- **Restrictions:** a controls call is shared only if it asks for movement (a hook on `ToggleControls`, 68545).
- **Tests:** scenarios `player-moment-stays-theirs`, `restriction-stays-with-game`,
  `player-stage-cutscene-reaches-party`, each 3 of 3. Everyone gets a cutscene wherever they are (no distance
  gate, decided 2026-10-23). New harness controls: `papyrus_call` `player_action`, and
  `quest_allow_rerun` for a run-once quest.
- **Re-run:** the scripts, quests, magic and dialogue scenarios pass, except the two crashes below and one
  `dialogue-pick-reaches-host` timeout ("[host] FF000854 says the picked line"), which passed 3 of 3 on a rerun, cause
  unknown.
- **Crashes found:**
  - `HookSetOwnerForm` read through an unresolved owner form id while a reference loaded. Fixed:
    `docs/re/crashes/2026-10-23-owner-form-id-on-load.md`.
  - A guest's beast form: its werewolf quest's stage goes to the host as a world call, the host changes the guest's
    copy, and crashed freeing a behaviour graph (`docs/re/crashes/2026-10-23-guest-beast-form-graph-gc.md`). The cause
    is fixed below; the graph crash itself is not investigated.
- **A guest's own quests** (2026-10-23): a quest the guest's own scripts start (the beast form's
  `PlayerWerewolfQuest`, a mod's quest from its MCM) is the guest's: its stages and events stay on its game
  (`PlayerNatives::IsOwnQuest`, a hook on `EnsureQuestStarted` 25003; `docs/design/world-state-sync.md`, "A guest's
  own quests"). `client-beast-form-stays-client` passes again. The first piece of per-player quests
  (`docs/ideas.md`).
- **Two cutscenes at once** (2026-10-23): each game keeps who took which controls (its own scripts, each other game's
  by `NotifyPartyNative::Source`), and a give-back gives back only what no one else holds. A player leaving gives back
  what its cutscene held on the others (`NotifyPlayerLeft`). Scenarios `overlapping-cutscenes-keep-controls`,
  `leaver-cutscene-gives-back`.
- **Lock order:** `client-beast-form-stays-client` printed an engine-only self-deadlock pattern on the host
  (`SkyrimSE.exe+0xcb553c > +0xcb55b7`, write lock on `0x1431ca5d0` asked while holding its read lock). It has none
  of our frames and is not in `lock_order_known.txt`. A second, also engine-only, came up in the tagged run: a lock
  order inversion (`+0x16abbd > +0xe92c16` vs `+0x1b064a > +0x165105`). Both noted, not looked into.
- **Re-run after both:** the new scenarios and `client-beast-form-stays-client` 3 of 3; the scripts, quests, magic,
  dialogue and session tags pass except one `host-rejoins` (Lydia's copy had no animation state yet after the
  rejoin: action, idle and graph states 0). It passed 3 of 3 on a rerun; cause unknown.
- **One script call type** (2026-10-23, cleanup): the cutscene, world-call and player-native paths each had their own
  `{class, function, self, arguments}` struct, frame reader and conversion loops. Now `ScriptCallOf<TForm>`
  (`ScriptCall` on the wire, `LocalCall` in a game; `Structs/ScriptValue.h`), `GlobalCall::ReadCall`/`QueueCall`, and
  `Utils::ToScriptCall`/`ToLocalCall`. `Payloads::PlayerNative` gains the call's self (always None) on the wire. The
  scripts tag passes, and so do the two dialogue native scenarios. `guest-spawns-no-critters` failed once as the
  first scenario after a reload ("[host] its spawner places the dragonflies"), then passed 3 of 3.

### Cutscenes, step 1 (2026-10-23, uncommitted)

The whole party sees a cutscene (`docs/design/cutscenes.md`; research `docs/research/cutscenes.md`): every
script's call on the controls or the screen (fades: image-space modifiers, `FadeOutGame`; camera, shakes, music,
video) runs on every game, each on its own player (`Papyrus/PartyNatives`, `CutsceneService`, `PartyNativeRequest`/`NotifyPartyNative` relayed by the
host). Which calls those are is read from their code (`NativeReach::CallOf`: the native's own code calling the
engine's controls, fade, image-space, camera, shake, music or rumble functions), no list. Decided against a
cutscene gate (2026-10-23): a spell's fade outside one tints everyone, accepted. A guest's own script setting a quest's stage goes to the host (`Quest.SetCurrentStageID`,
`WorldCallRequest`/`NotifyWorldCall`, `HostPlayerNatives::OnWorldCall`): the Elder Scroll's `MQ206.SetStage(20)`.
Scenarios `cutscene-controls-reach-party`, `cutscene-fade-reaches-party`, `guest-cutscene-reaches-host`,
`screen-reaches-party`, `guest-script-stage-reaches-host`; controls without the sending fail. Harness
`papyrus_call` and `screen`. Found on the way: 1.6's `Disable`/`EnablePlayerControls` take nine arguments, and
`GlobalCall` now refuses a call whose arguments did not fit (`Statement::SetSize` returns false). Step 2: each
player's own character in the scene.

### Overnight 2026-10-22: ghost NPCs, AI state on copies, step sync (uncommitted)

Order: (0) ghost NPCs, (1) AI state on copies (package, sit/sleep, scene, combat), (2) dialogue's step sync.
Decisions made without the user are marked **Decided**.

- **0. Ghost NPCs: done.** `ActorWhereabouts` on `RequestRemoveCharacter`/`NotifyRemoveCharacter` (host rule
  `HostCharacters::OnRemoveCharacter` passes it on; bridge case "the owner's removal says where its actor went";
  TPTests round trip); the guest's `CharacterService::FollowOwner`; harness `actor_place`. Scenario
  `host-npc-leaves-copy-follows` (send home, stage again, disable, enable) fails before (the client's Vilkas left
  on the stage) and passes after; `release_host_npc` lost its client half. Passed with it: `vilkas-counts-client-hits`,
  `remove-host-npc`, `death-host-npc`, `move-host-npc`, the `cells`, `session` and `spawn` tags but the known
  `party-stacks-on-arrival` and `form-state-round-trips` (loose ends) and `dive-host-player` once (below).
  - **Decided:** the host's deleted NPC is disabled and marked deleted on the guest (as the world state's enabled
    row deletes a reference); its entity goes.
  - **Decided:** a guest's game reference stays flagged a copy after its copy entity is dropped (removal, an
    arrival leaving it behind, a respawn request) for the rest of the session (`HeldCopyComponent`); the flag
    is cleared when the session ends. Before, an arrival cleared it and the guest ran that NPC's AI.
  - **Decided:** the guest skips the move when its reference is in the target cell within 64 units (a host that
    travelled away sends removals of actors that did not move).
  - `release_host_npc` sends the host's NPC home while it is still frozen and thaws it there: thawed and
    unloaded, Vilkas's AI walked him on ~300 units a second (`actor_place` on the host), which a copy does not
    follow (it goes where the actor was at removal).
  - `dive-host-player` failed once in the tag run (the host's copy on the client had a package of type 18,
    `kPackage`, in one sample under water), then 3 of 3. Not traced; the copy is a player's (a temporary
    reference), which this slice does not touch.

- **1. AI state on copies, the package: done.** `AiStateService` (owner sends `AiStateRequest` when it changes, event-driven
  since the cleanup below; `HostAuthority` relays it from the owner only, bridge case "AI state: the owner's reaches everyone else";
  wire bytes pinned); the copy held its owner's form package (`ActorPackage::InitFromPackage` 38959; changed after the
  suite to answered, not run, then back the same day, below). Upstream's package relay is deleted (`NewPackageRequest`/`NotifyNewPackage`
  became `AiStateRequest`/`NotifyAiState`, `AI_SYNC`, `InitPackageEvent`, `HookInitFromPackage`, `SetActorPackage`).
  `AIProcess` now has CommonLib's `ActorPackage currentPackage` (`+0x18`) and `followTarget` (`+0x110`), asserted.
  Harness: status `ai_state` (compared by the desync check), `put_package`, `is_current_package` (the condition
  21529 itself). Scenario `copy-runs-owners-package` (two packages put in place on the host's frozen Vilkas; the
  copy's field and `GetIsCurrentPackage`) fails before (package 0, condition false) and passes after; seen live too:
  the copy followed Vilkas's own evaluation back to his package.
  - **Decided:** the owner sends what conditions read (`Actor::GetCurrentPackage` 37399: for an engine-made package,
    the form package under it); an engine-made package itself (no id shared by the games) leaves the copy on the
    do-nothing one, so `GetAlarmed` (an engine-made alarm package) answers no on a copy (27 menu lines; open).
  - **Decided:** the package target is not written into the copy (no condition in the menus reads it; a copy's
    package aimed at another actor is what the engine's Disable cleanup acts on, the 2026-10-10 deadlock). Since
    the cleanup it is not sent either.
  - `copy-ends-voice-line` asserted the do-nothing package on the copy; it now accepts the owner's package (what
    matters there is that the copy holds one, for `UpdateAI`'s upkeep).
- **2. Sit/sleep, furniture, scene, talking, follow target: partly.** Sit/sleep state (`GetSitting`, `GetSleeping`)
  and talking (`IsTalking`) are the owner's already (replayed actions, the copy's own voice line); `ai_state.talking`
  is compared. Scene: `Actor::GetCurrentScene` (37247) hooked for copies (`Games/Skyrim/CopyAiState.cpp`), compared
  as `ai_state.scene`; **no scenario** (no lever to put a host NPC in a scene without a quest: a stall, left). The
  occupied furniture and follow target travel but are not written (**Decided:** no vanilla dialogue condition reads
  them; writing the furniture handle would hand a copy to the engine's furniture exit code). Greeting
  (`HighProcessData::greetingPlayer`) is not carried (**Decided:** it means "this game's player", which differs per
  game). No host NPC could be seated in QASmoke by the console (`<furniture>.activate <npc>` did nothing), so no
  NPC sit scenario; players' copies are covered by `furniture-*`.
- **3. Combat state on copies: done.** Audit of the 22 `IsInCombat` callers touching `+0x160` written in
  `actor-ai-and-copies.md`: 4 use the controller after a yes unchecked, 3 start or stop combat; the hook answers the
  owner's everywhere else (by return address). `GetCombatState` hooked; `currentCombatTarget` (`+0x104`) written.
  Scenario `copy-in-owners-combat` (Vilkas, AI on, `startcombat player`: the copy's `IsInCombat`, `GetCombatState`
  and combat target = the client's copy of the host's character; then out of combat) fails before and passes after.
  A frozen or restrained actor does not start combat; the scenario thaws Vilkas, who swings at the host's player.
- **4. A guest's player in combat: done.** `HookCountCombatTargetingPlayer` (41261) adds the copies in combat with
  this game's player. **Decided:** counted on the guest from the combatants' AI state, not a count sent by the host
  (the state already says whom each fights). Scenario `guest-player-in-hosts-combat` (Vilkas fights the client's
  character: the client's player is in combat, then out) fails before and passes after. Closes the loose end "A
  guest's player is never in combat"; combat music and wait/save blocking follow from the same flag, not checked.

- **5. Dialogue step sync: done (steps 1 and 3; 2 is slices 1-4), replaced the same day (below).** The archive's client (tag
  `archive/shared-dialogue-client`) redone as step sync on today's tree:
  - Step 1 (spike, IDA): the list build 35287 takes the picked node's option, else `lastSelectedDialogue` (+0x38,
    which it owns); an option whose info has choices (the constructor 35254 sets +0x10 from 25659) makes
    `rootTopicInfo` that info and the list its choices, else the speaker's topics (35276). A game builds a step's list
    by making the said line's option (`BuildOption`) the `lastSelectedDialogue` with no picked node and calling 35287
    (`MenuTopicManager::BuildStep`); the host's step is its `rootTopicInfo` after each list (`DialogueBridge::GetStep`).
    Seen in the games' logs: steps of the speaker's topics and of a line's choices (Andurs, three questions deep).
  - Step 3: `DialogueStepRequest`/`NotifyDialogueStep` (the said line, empty for the speaker's topics) replace
    `DialogueOptionsRequest`/`NotifyDialogueList`; deleted with them: `DialogueOption(s)`, the archive's `OwnLines`,
    `ShowOptions` building options, the added-options record, `SendOptions`, the `List` filter, `GetLine`/`GetChoices`.
    `HostDialogue` sends the step to the games in the conversation and to a joiner and takes a pick only with the
    picker's own line. Ported from the archive: refusals, training, force greets (`DialogueStartFailedRequest`,
    `DialogueTrainingRequest`, `NotifyDialogueRefused`, `NotifyDialogueTraining`, `Everyone`), Speech experience to the
    picker (`PlayerCharacter::GainSkillExperience`, `SetSpeechExperienceFilter`), the copy-speaker guard on
    `TESTopicInfo::Begin` (25616), the dialogue hooks (`Bridges/DialogueBridge.cpp`), the harness (`dialogue`,
    `dialogue_pick`, `skill`, `force_greet`, `info_said`, `trainings`; `item_count` takes `form` alone for the
    player) and the scenarios. `dialogue_tests.cpp` rewritten for steps (27 cases); TPTests round trip.
  - **Decided:** the game side splits by role, per the rules: a shared `DialogueService` (joins, leaves, picks, the
    picker's training and Speech; `Services/Generic`), the host's part `Services/Host/HostConversation` (made by
    `HostService` while hosting) and a guest's `Services/Guest/GuestDialogue` (made by `GuestService`), each plugging
    in as `DialogueService::Role`; not the archive's one service with `IsHost()` branches.
  - **Decided:** a guest with no answer to a line's choices leaves (its menu closes), as the archive did; the host's
    held choices (`HoldChoices`) stay.
  - The 18 two-game dialogue scenarios pass (`--tag dialogue`, first run). The archive's three-player duo did not
    apply on the changed duo code; its half (a `--players` flag `duo` ignored, `client2`, `dialogue-three-players`)
    was removed at review (`docs/ideas.md`, Testing). Steps 4 (quest topics on guests) and 5 (event-driven lists)
    not done; step 5 is moot (step sync has no polled list).
- **Combat target on copies (found while running the suite):** writing `currentCombatTarget` works only while the
  engine's own callers see the copy in combat; with the allowlist below they see it out of combat and something
  clears the field again (not traced). `copy-in-owners-combat` no longer checks it. Open.
- **Combat state allowlist (crash found by the suite, fixed):** the first hook answered the owner's state to every
  caller but the 9 audited sites; the client crashed in `cast-host-player` (`MagicCaster` 34450 through
  `Actor::EngagingInCombat` 38563 reads the controller at `+0x160`; 23 callers of that wrapper, none in the audit of
  direct callers). **Decided:** a copy answers its owner's combat state only to the condition functions (21576,
  21740) and to plugins (callers outside the game's image); the engine's own callers get the copy's own answer. Papyrus
  `IsInCombat`/`GetCombatState` tail-jump into the vfunc from the VM and get the copy's own (open).
- **A copy's package: held in its slot again (2026-10-22, after review).** Overnight it was changed to "answered, not
  run" (do-nothing in the slot, 37399 hooked) after a lock-order report blamed on a copy running `BGSProcedureSleep`.
  Wrong diagnosis: the stack runs through `HookUpdateAI`'s pass-through (`RealUpdateAI`), so the sleeper was not a
  copy but an NPC of the guest's own, 10 ms after the guest left the host for a late join (same report with the
  do-nothing version; `late-join-gets-ring-pose` passed on rerun, open as a vanilla race on load). **Decided:** the
  copy holds its owner's form package in its running slot, without a target (`Actor::PutOwnerPackage`, 38959;
  `HookCheckForNewPackage` keeps it); the 37399 hook is deleted. Inline readers (Open Animation Replacer) now see the
  owner's package. `copy-runs-owners-package` also checks the slot (new readout `ai.running_package`): fails with the
  do-nothing version (slot FF000EAD), passes now; package, combat, furniture, sneak, late-join and all dialogue
  scenarios pass (dialogue-three-players needs a third game, not run). `ai_state.talking` is no longer compared (a copy's own voice state
  trails its owner's; `vilkas-counts-client-hits` failed on it once).
- `guest-player-in-hosts-combat` failed after the full suite on the same duo (Vilkas's `startcombat` on the client's
  character did nothing, target 0), passes on a fresh duo. Not traced.
- **Full suite at the end (fresh duo, final build except the revert below, 935 s): 107 pass, 3 fail.**
  `flames-client-hits-host-npc` (each game's own fire hazard, the `burning-shows-on-copy` loose end);
  `dialogue-pick-reaches-host` (the client's E press in its menu sent no pick: no log line of one; passed 3 of 3
  on rerun and in the dialogue run); `host-kill-move` (the client's blow 176 ms ahead of the host's; 2 of 2 on
  rerun). The run before the package change: 104 pass, 6 fail (the hazard twice, `party-stacks-on-arrival`, the two
  lock reports and `ai_state.talking`, all handled above). Tried for the pick: skipping the guest's redraw of step 1
  (the speaker's topics its engine built at the open) made two other picks fail; reverted. Not explained: the
  guest's step-1 redraw may reset the SWF as a pick lands.
- **AI state at spawn (found by the suite, fixed):** `late-join-client` failed on a copy made after the rejoin
  holding no package for up to 2 s; the spawn now carries the AI state (`AssignCharacterRequest`/`NotifyCharacterSpawn`
  `AiState`, `AiStateService::ReadState`).
- **AI state cleanup (2026-10-22, the user's review: "this clearly isn't clean code"; "events over polling").**
  - Event-driven: `AiStateEvents` (Games/Skyrim) sinks `TESPackageEvent`, `TESCombatEvent`, `TESDeathEvent` and
    `TESSceneEvent` and queues `AiStateChangeEvent`; the owner reads and sends only on those, if changed (target
    switches and the player's flag since, "Events over polling" below). The 250 ms
    check and the 2 s full resend are gone. `put_package` sends the package event as evaluation does.
  - The wire state is what a copy applies: package, scene, combat state and target (package target, furniture and
    follow target dropped; the status still shows them from `EngineCalls::GetAiReadout`). Wire bytes re-pinned.
  - `GetActorAiFields` is one engine read of an owned actor (no copy branches); the status reads what conditions see
    through the harness instead. The fighting-player count goes through `IActorBridge::SetCopiesFightingPlayer`, not
    from the service into the hooks. Target lookups use `Utils::GetFormIdBySessionId`/`GetSessionIdByFormId`.
  - Found by its run: a death clears the package with no package event (`death-stage-runs-once`: the copy kept
    Valdr's); `TESDeathEvent` added. Open: a combat target switch inside one fight has no event.
  - Runs: BridgeTests 110 cases pass. AI scenarios, `late-join-client`, `host-npc-leaves-copy-follows`,
    `furniture-host-player`, `sneak-client-player` pass; `--tag combat --tag spawn` 27 pass, 2 fail:
    `death-stage-runs-once` (above, fixed, then passed) and `party-fast-travels` (the guest crashed in the engine's
    `QueuedPromoteLargeReferencesTask`, 18902+0x80, null `rcx`, ref 190E5, no frame of ours; new, not traced; passed on
    fresh games). After those, `guest-player-in-hosts-combat` failed on the host's own `startcombat` (target never
    set), the known "fails on a used duo" item above; it passed on fresh games earlier.

- **Dialogue: the host's list (2026-10-22, the user's review: "is there a simpler way?").** Step sync replaced:
  every menu shows the host's engine's list (judged for the host's player, with the real quests and aliases), and a
  pick is an option of that list, which the host's game says as a click. Messages back to `DialogueOptionsRequest`/
  `NotifyDialogueList` (HEAD's, with `Structs/DialogueOption`); `HostDialogue` keeps the step's list, sends it to
  the games in the conversation and to joiners, and takes only a pick in it. Deleted: the guest's list build
  (`MenuTopicManager::BuildStep`, `IDialogueBridge::Line`, `GetStep`, `ShowStep`), `HoldChoices` and the guest
  leaving with no answer, `PickTopic`'s say-any-line, `MarkSaid` on guests. Added: `DialogueBridge::ShowList`,
  `PickOption`, `IsChoices`. BridgeTests: the list cases rewritten, new "a pick is one of the host's options" and
  "a pick before the host's list is not said" (111 cases pass); TPTests round trip for the lists.
  Scenarios: `client_shows_hosts_list` (topics, lines, texts equal, in order) in `dialogue-peer-starts` and each
  step of `dialogue-several-questions-deep`; `dialogue-peer-only-option` → `dialogue-options-are-the-hosts`,
  `dialogue-picker-says-its-line` → `dialogue-pick-says-the-hosts-line`, `dialogue-answer-only-a-peer-has` →
  `dialogue-unanswered-question-ends-it`; new `dialogue-quest-option-from-host` (the guest's own beggar quest
  stopped, its menu still lists the coin).
  - Found by the runs and fixed: (1) the guest's engine builds and shows a list of its own after the host's
    arrived (at the menu's open, after its greeting): the host's list now goes in its place at the engine's show
    (`HookShowList`, the `List` filter, `GuestDialogue::ListToShow`); this was what the said-once, reset-hours and
    quest cases caught. (2) The host's list built again within a step (a topic with random lines, Lydia's): a pick
    falls back to the topic's option (`PickOption`). (3) A menu filled again with the list it already shows takes
    no pick for 750 ms (voice ready 52858): a guest's E press in that window was lost (`dialogue-pick-reaches-host`,
    the intermittent failure of the night before); a refill that changes nothing is skipped. Logs added for a pick
    sent and a pick not said, with why.
  - Not dialogue: after a beggar's coin the host's player has Gift of Charity (`EEF58`), whose effect shader stayed
    on the guest's copy forever (copies held every hit shader; the owner only FX Persist ones): found by
    `dialogue-options-are-the-hosts`' desync check; fixed by the EffectVisuals change ("Events over polling",
    below); the coin scenarios dispel the blessing too.
  - Runs: BridgeTests 111 pass, TPTests pass; `--tag dialogue` 22 of 22, twice in a row on one duo.
  The half-ported three-player duo and `dialogue-three-players` are removed (`docs/ideas.md`, Testing).
  - Bridge cleanup (the user's review: "dialoguebridge is large"): `Redraw`, `Pick(index)` and
    `MenuTopicManager::IndexOf` were dead; one "already shown" check (the bridge's `ShowList`, not also
    `GuestDialogue`), cleared when the engine shows its own list (a stale record could skip a needed refill); the
    host sends a step's list when its engine builds one (the `List` filter as the event) instead of counting lists
    every frame (`GetListsShown`, `m_listsAtPick`, `m_stepSent`).
- **A picked line runs for its picker (2026-10-22, after deciding each player keeps their own items and gold,
  `party-model.md`).** Rule 2 for dialogue: on the host's game a guest's picked line's fragments are queued under
  that guest's cause (`TESTopicInfo::Begin` 25616, the `LineFor` filter, `HostConversation`), so `Game.GetPlayer()`
  there is the picker's character. Natives on the player (`Game.AdvanceSkill`, `IncrementSkill(By)`,
  `IncrementStat`, `ShowTrainingMenu`) in any script run for a guest are skipped on the host and run on that guest's
  game (`GuestScripts`, `PlayerNativeEvent` → `PlayerNativeRequest`/`NotifyPlayerNative`, host-only rule in
  `HostAuthority`, `GuestEngine::RunPlayerNative` through `DispatchStaticCall`). Deleted, the two reroutes they
  replace: the training hook (52942) and `OpenTraining`, the Speech filter on `PlayerCharacter` and
  `GainSkillExperience`, `DialogueExperience*`/`DialogueTraining*`, `HostDialogue::Talker`. Also typed:
  `IFunctionArguments` returns bool (CommonLibSSE-NG; nothing implemented it before), `Stack::returnValue`.
  - Scenarios: the coin is the client's (`dialogue-options-are-the-hosts`), the host does not pay the client's room
    (`dialogue-pick-says-the-hosts-line`), a real persuade (Ungrien, `dialogue-picker-gets-speech-experience`; the
    old one faked it with `advskill`), the training menu over the client's dialogue menu as on the host's game
    (`dialogue-picker-trains`; the reroute closed the dialogue first, vanilla does not, read on the host).
    BridgeTests 110 pass (the two reroute cases out, a player-native relay case in), TPTests round trip.
  - Runs, `--tag dialogue,scripts` on a fresh duo: 28 of 32; `dialogue-options-are-the-hosts` missed once
    ("[host] the client's character carries its gold" after 5 s, the copy behind the client's gold once; it passed
    alone and twice after the same predecessors; the wait now names both counts). `statue-fires-at-client` and
    `ring-turns-for-client` fail on the player's `in_combat` (owner vs copy, AI state from commit 2, no native
    forwarded during them; the events agent's combat work passes them), `shrine-blesses-client` on the lock-order
    checker's known engine pair (our hooks only passing through; the checker fix is the late-join report's).
- **Events over polling (2026-10-22, after the AI state cleanup).** What each engine event or hook covers:
  `docs/re/systems/change-events.md`; what is left and why: `docs/ideas.md`, "Events over polling".
  - **Difficulty: done (2026-10-23, uncommitted).** The engine sets it through `PlayerCharacter::SetDifficulty`
    (40563, after writing `iDifficulty`: the settings menu 53310, its defaults 36622, the saved settings applied
    36598, a profile's); hooked in `PlayerCharacter.cpp`, it raises `DifficultyChangeEvent`. The host's game tells
    the others (`HostAuthority::DifficultyChanged`, `HostSession::OnDifficultyChange`), a guest's puts the host's back
    (`PlayerService::OnDifficultySet`). Both per-frame reads are gone. The fake engine's `ChoosesDifficulty` raises
    it (`session_settings_flows.cpp`); `HostAuthorityTests` "Difficulty: the host's reaches the others".
    Discovery, presence and the equipment snapshot were looked at and stay (`docs/ideas.md`).
  - **Combat target switches (AiStateService): done.** `CombatController::SetTarget` (33235, the only writer of an
    NPC's target, `+0x104`) hooked in `AiStateEvents`; it raises the attacker. Also the player's in-combat flag:
    `PlayerCharacter::UpdateInCombat` (41238) hooked, a flip raises the player (a player has no combat event; the
    desync check caught a host's player left in combat on the host and not on its copy). Scenario
    `guest-player-in-switched-combat` (Vilkas fights the host's player, then the client's character once the host's
    player steps 1500 units away; the client's player must be in combat): fails before (its Vilkas stays on the
    host's character), with the hook file left out too, and passes after (3 of 3, and in the tag runs).
    **Decided:** the combat scenarios' cleanups freeze Vilkas before `stopcombat`: thawed, he takes up the fight
    with the host's player again within 2 s (vanilla, seen without the hooks too), and `guest-player-in-hosts-combat`
    then left the host's player in combat.
  - **EffectVisualsService: done.** The owner sends its actors' effects when its engine starts or stops their hit
    visuals (hooks on 34094, ours `ActiveEffect::StartHitEffects`, and `ActiveEffect::StopHitEffects` 34050, in
    `EffectEvents`); `TESActiveEffectApplyRemove` is sent for scripted effects only (Flames' fire sent none). The
    250 ms check, the 2 s full resend and the copy's 5 s "owner went quiet" stop are gone. `AssignCharacterRequest`
    and `NotifyCharacterSpawn` carry `EffectIds` (TPTests round trip, bridge case "a player's own gets an id..."
    checks them; the spawn messages have no pinned bytes). The copy keeps its owner's list by session id (a respawn
    keeps it), replays it when its 3D loads (`TESObjectLoadedEvent`, `ReferenceEvents`) and stops it on the
    character's removal or a disconnect. Found by the main session (Gift of Charity on a copy): copies held every
    effect's visuals until it ended, the owner only those with FX Persist; **decided:** only FX Persist effects are
    shared (`GetHitVisuals`, typed `EffectSetting` fields instead of raw offsets). Scenario
    `copy-made-later-shows-effects` (Vilkas casts Oakflesh while the client is away; the copy made on its return
    shows it, then a dispel ends it on both): passes, fails with the spawn's list left empty.
    `burning-shows-on-copy`'s body passes (its world-differs failure is the hazard loose end).
    Then (main session, merging this): one-shot visuals no longer vanish from copies. `StartHitEffects` raises the
    effect when it does not persist, the owner sends `EffectFlashRequest` (relayed from the owner,
    `NotifyEffectFlash`; bridge case, wire pin) and the copy plays it with a lifetime of 0, which the engine ends
    after the shader's own pass as it does its own (`change-events.md`). Scenario `copy-flashes-one-shot-effects`
    (Gift of Charity on Vilkas: the flash shows and ends by itself on both, the blessing still running), read
    through the harness's `mod.visuals_shown`: a one-shot shader is finished from its start, which `visuals` leaves
    out, so the first version (Arkay's blessing, whose `0x92DEC` is its enchant shader, not a hit shader) never saw
    the host's flash. 3 of 3.
  - **Copies sweeping across a teleport (fixed, the merge's runs).** A copy whose owner went to another interior or
    worldspace was moved there at its old cell's coordinates and interpolated from the old points to the new ones,
    across the new cell: through QASmoke's water on the way home (z 1060 up to the floor at 6976), so the engine
    set the copy swimming (`ActorState1` bit 10, `Character::sub_37946`), and a copy never leaves it on land
    (leaving needs its controller's `kSupport` or the in-air state). A swimming guest's character is never taken as
    Vilkas's target: `guest-player-in-switched-combat` failed after `plate-counts-client` (Kolbjorn and back), not
    alone (the merge was not the cause), and `dive-host-player` failed on `SwimStart` vs `swimStop` after the
    return home (the copy's own swim start; the owner's `swimStop` replay was refused). Now the owner's movement in
    another interior or worldspace drops the copy's earlier points and places it at the new position, as does an
    arrival's spawn (`CharacterService::OnReferencesMoveRequest`, `RefreshCopy`). plate then switched-combat 2 of 2,
    `dive-host-player` 5 of 5, `dive-client-player`, `party-changes-cells` pass. A teleport inside one cell (`setpos`)
    still sweeps.
  - **PlayerService: done.** Beast form on `TESSwitchRaceCompleteEvent` (struct asserted), the level on a hook on
    `TESActorBaseData::SetLevel` (14385), the one write: the level-up sends `LevelIncrease` before writing, the
    console's `setlevel` (the baseline's reset) sends nothing. Both polls gone (`PlayerEvents`). `party-shows-levels`
    (2 of 2) and `client-beast-form-stays-client` pass.
  - **CharacterService factions and experience: done.** Factions on hooks on their five writers (`FactionEvents`:
    `AddToFaction` 37686, `RemoveFromFaction` 37688, the rank setter 37677, `SetCrimeFaction` 37381, Papyrus
    `RemoveFromAllFactions` 54735); the owner sends an actor's factions when they differ from the last sent. Found:
    a copy has no `CacheComponent`, so `OnFactionsChanges` never applied a change to any copy (only spawns carried
    factions); fixed. Experience goes out on each skill use (the hook was already there, the 1 s timer only
    batched). New harness `faction_rank` (the condition GetFactionRank) and scenario `copy-holds-owners-factions`
    (add, rank, remove on the host's Vilkas): passes (failed on the copy-side bug first). `shared-xp`,
    `vilkas-counts-client-hits`, `late-join-client` pass.
  - **InventoryService: not done (stalled).** The 3 s equipment snapshot repairs a copy that drifted in its own game
    (`repair-equipment-drift`), which no owner event sees; the weapon state has no single writer; the naked NPC
    check's cause is unknown. Tried and reverted: snapshots on the owner's `TESEquipEvent`, the naked check on 3D
    loads. Reasons in ideas.md.
  - **ActorValueService: plan only** (ideas.md); **HostWorldState: not done** (alias and location fills and globals
    have no event); **DialogueService:** skipped (the shared dialogue rework in the main checkout).
  - Runs (final build, one duo): BridgeTests 110 cases, TPTests pass. `--tag ai,combat,magic,spawn,session`: 73 of
    76 pass. `join-refused-shows-why` needed `coop_peer` built in the worktree (then passed),
    `flames-client-hits-host-npc` is the hazard loose end, `late-join-gets-ring-pose` hit the known lock-order report
    with only `HookUpdateAI`/`HookMainLoop` of ours on the stacks (a guest NPC's `BGSProcedureSleep` during the late
    join's cell load). Earlier, `sneak-host-player` failed twice on a Fader Menu stuck after a first lever for the new
    combat scenario (lifting the host's player 2000 units); it passed again once the lever changed.

- **Rule 2's gaps (2026-10-22, after the picked line).** Three generic rules, no script named:
  - *A player in the frame:* in a stack run for a guest, a native's `self` or argument holding the player (a
    `PlayerRef` property, a variable stored at load) is that guest's character for the call (`GuestScripts`,
    `PlayerInFrame`). Scenario `relief-takes-client-relics` (Volunruud's relief door, `player` stored at `onLoad`);
    control with the swap off: fails at "[client] the player gave up the axe".
  - *Natives on the player, read from the code:* no list. A global native whose code (three calls deep) addresses
    this game's player, its stats or its screen runs on the guest's game (`Papyrus/NativeReach`, Zydis at run time,
    once per native); IDA's reading of the same rule (`scripts/re/papyrus_natives.py`, `docs/re/papyrus_natives.tsv`:
    65 of 143 global natives) is checked by `natives-reach-matches-ida`. It also moves `FastTravel`, prison, beast
    form, words and the screen natives to the guest's game (`papyrus-guest-scripts.md`, "Natives on the player: read
    from the code"). Any form goes as an argument or result (an actor by session id, the rest by `GameId`,
    `Variable::SetForm` packing it as the engine's per-type packers do), and None: `words-reach-party` teaches the
    QA shout button's twelve words to the client (and, since feature 11, the party).
    A value comes back from the guest's game: the host's stack waits as on a latent native (104851,
    `kInProgress`) until `PlayerNativeResultRequest` → `HostPlayerNatives` → `NotifyPlayerNativeResult` →
    `ReturnFromLatent` (104807); a guest leaving answers with the default. `IFunction::GetReturnType` returns a
    `TypeInfo` through a hidden pointer (it was declared as a reference: the first call crashed the host).
  - *The copy's gold after training:* the Training Menu pays through `InventoryChanges::RemoveItem` (16059), under
    the `TESObjectREFR::RemoveItem` hook, so the host's copy of the guest kept every lesson's gold
    (`dialogue-options-are-the-hosts` failed after `dialogue-picker-trains`). Hooked; skipped inside the reference's
    own removal (`ScopedItemRemovalOverride`), so each removal goes out once.
  - *What the copy stands in for:* a player's copy already carries its owner's actor values, skills included
    (`ActorValueService`); `dialogue-picker-gets-speech-experience` now checks the copy's Speech and the persuade's
    `QueryStat` answering from the client's game. Ownership is a decision for the user: the engine refuses a copy's
    (created) base as an owner (11609), so a guest renting a room pays and owns nothing (`party-model.md`, "Items and
    gold": options A, the party's, recommended, and B, that guest's).
  - Tests: BridgeTests 112 pass (a value answered once and only to the host's game; a guest leaving answers with
    none), TPTests 5 cases pass (round trip of `PlayerNativeResultRequest` / `NotifyPlayerNativeResult`).
    `--tag dialogue,scripts`: 31 of 35; `statue-fires-at-client` and `ring-turns-for-client` on the player's
    `in_combat` (known, fixed elsewhere), `dive-host-player` and `relief-takes-client-relics` on one lock-order pair
    on the client, engine against engine (`GarbageCollector` demoting a reference, 11787, against a sleeping NPC's
    idle conditions, 11778; ours only `HookMainLoop` and `HookUpdateAI` passing through). The relief, the persuade
    and the training passed again on the final build, the relief twice.

### Phase 5: the server is gone (2026-10-21, uncommitted)

Every message a game sends had a host rule; the two without one were dead (upstream's `RequestQuestUpdate`,
never sent, and `AssignObjectsRequest`). What the host still took from `GameServer` moved:

- **`HostNetwork`** (`Services/Host/`): the enet listener (`Net::Server`), each message to the front door
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

Leaving `TiltedPhoques` (2026-10-21, `docs/ideas.md`, "Modern C++ over `TiltedPhoques`"), all four steps:
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
   the SKSE loader went (`Plugin` is a plain class: `InstallHooks`, `Start`, `Update`). Found on the way:
   a bridge test read a message after freeing the outbox that held it (`dialogue_tests.cpp`, `Talking::Open`);
   the custom allocator had hidden it. `scripts/re/build_re_data.py` finds hooks by `HOOK` now (124 hooks).
   Checked on a fresh duo: 24 scenarios across sessions, combat, magic, doors, travel, quests and the world pass
   (12 first failed behind a stuck `LoadWaitSpinner`, loose ends), and a hot reload.

Dead code after `TiltedPhoques` (2026-10-22): four notifications nothing ever sent, not even upstream's
server (`NotifySpawnData`, `NotifyPlayerRespawn`, `NotifyRelinquishControl`, `NotifyWorldState`), and their
handlers; `NotifySetTimeResult` (a guest never read it; the host ignores an hour that does not exist);
unpublished or unheard events (`ActorSpawnedEvent`, `ConnectionErrorEvent`, `PreUpdateEvent`); 20 headers
nothing included (`Differential.h`, engine structs never used); `WindowsHook` (never installed); upstream's
release-branch flag `IS_MASTER` (`BranchInfo.h`); the CRT heap swap and the EngineFixes rehook in
`Games/Memory.cpp`, both only ever called by upstream's launcher. Renamed: `TiltedOnlineApp` is `Plugin`,
`TiltedOnlinePCH.h` is `ClientPch.h`, `TP_ASSERT` is `Utils::Assert` (a function, `std::source_location`),
and the components' include guard is gone. 11 scenarios pass on a fresh duo (sessions, time, combat,
magic, chat).

Includes (2026-10-22): `ClientPch.h`, forced into every client file, held our own often edited headers
(`Utils.h` → `World.h` → every service, `Components.h`), so editing any of them rebuilt all 201 client files.
It now holds third-party, standard and rarely edited headers of ours (`RTTI.h`, `Primitives.h`, `VersionDb.h`,
`Wire`, `Hooks`); files include the rest themselves. `World.h` forward-declares the services it hands out,
`Utils.h` no longer includes `World.h` (its session-id lookups are in `Utils.cpp`), `POINTER_SKYRIMSE` lives in
`VersionDb.h`. Builds (DLL, `-j8`, duo running), before → after: full 33 s → 26 s, one `.cpp` 2.8 s → 2.5 s,
a component header 20.5 s → 7.2 s, a service header 21.8 s → 4.7 s, `Actor.h` 22 s → 6.7 s. Also deleted:
upstream's `SAVE_STUFF` blocks and `Save_Reversed` (never compiled).

`Wire` with spans (2026-10-22): `Buffer` owns a `std::vector`; `Buffer::Reader` reads a `std::span<const uint8_t>`
(received packets are read in place, no `const_cast`), `Buffer::Writer` grows a `Buffer` or writes into a fixed
span and fails past its end (`ViewBuffer`, deleted, grew by freeing memory it did not own). Byte reads and writes
and `vp::ScopedContext::WriteBuffer` take spans; the unused `vp::Put` (no return) and `vp::Nop` are gone. The
wire bytes are unchanged (`wire_tests`); `encoding.cpp` checks growth and both ends.

`std::format` (2026-10-22): `fmt::` is gone from our code and spdlog formats with `std::format` (package config
`std_format`); the Linux build takes libstdc++ 13 (GCC 13 headers from the `ubuntu-toolchain-r/test` PPA), the
first with `<format>`. Format strings are checked at compile time again (`FMT_CONSTEVAL=` had switched fmt's
checks off), which found seven calls fmt formatted wrong or by accident: `MagicService` logged every `GameId`
(spell, effect) as `1` through its `operator bool`, now `LogFormat()`; enums and `volatile` lock words are
converted, `BSFixedString` passes its text. `fmt::join` is `views::join_with | ranges::to`.

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
| A player going down (`docs/design/death.md`) | works: a real death in a session (`OnDeath` and `TESDeathEvent` fire, the reload held), copies die by their owner's death action, spectating once dead, revived beside the party (abilities, hands, graph and camera back; 2026-10-26, uncommitted); after a wipe each guest stands where it stood at the checkpoint; each player's ragdoll streams from its own game to the others (2026-10-26) | `death-spectate-host`, `death-spectate-guest`, `death-wipe-checkpoint`, GameTests `[death]` |
| Melee and spells on host NPCs | works, with kill credit | `melee-client-hits-host-npc`, `firebolt-*`, `flames-*`, `client-*-kill-credit` |
| Friendly fire (`docs/design/friendly-fire.md`) | built 2026-10-26 (uncommitted): a player's sword or spell on another player is applied by the victim's game; a kill is a real death named in every chat; not a crime; kill moves on players work (a copy's ragdoll steered, and kept on it by the engine as its owner; a copy getting up on its own timing: open) | `friendly-fire-*`, `spectator-watches-kill-move`, `kill-move-kills-guest` (fails on the open jitter), GameTests `[friendly-fire]` |
| Kill moves | works, both ways, with kill credit | `host-kill-move`, `client-kill-move` |
| Hostility | works | `client-angers-host-ally` |
| Effect visuals (burning, hit shaders) | works | `burning-shows-on-copy` |
| Cutscenes (step 1: scripts' controls, fades, camera, shakes, music for the party) | works, from any game; a player's own moment stays theirs; two at once keep their controls; each player's own character in the scene not yet (step 2) | `cutscene-controls-reach-party`, `cutscene-fade-reaches-party`, `guest-cutscene-reaches-host`, `screen-reaches-party`, `player-moment-stays-theirs`, `restriction-stays-with-game`, `player-stage-cutscene-reaches-party`, `overlapping-cutscenes-keep-controls`, `leaver-cutscene-gives-back` |
| Beast form (a guest's werewolf change) | works: the guest's game runs it (owner-applied effect), its copies respawn for each race from the owner's state; upstream's host-player bug does not occur (2026-10-20) | `client-beast-form-stays-client` |
| A player's looks changing in a session (race menu, vampirism, any race switch) | works: its copies respawn from its new base data, tints (the shown ones: a vampire's overlay) and weight (2026-10-24) | `guest-changes-looks-in-session`, GameTests "A player's looks change" |
| Furniture (live, while the copy is loaded) | works | `furniture-*` |
| Voice lines, subtitles, lip sync | works | `copy-ends-voice-line` |
| Doors, locks | works | `door-opens-for-both`, `lock-reaches-others`, `load-door-not-replayed` |
| Containers' items (changes, as counts per base item) | works both ways; the host's contents (its leveled roll) when a guest's cell loads (slice 2, 2026-10-21); an item's extra data (enchantment, tempering, poison) does not travel | `chest-take-reaches-others`, `guest-opens-hosts-roll` |
| Time, weather | works | `host-time-reaches-client`, `guest-sets-time`, `host-wait-reaches-guest`, `shared-wait-host`/`-guest`, `shared-sleep-host`/`-guest`, `shared-wait-cancel`, `late-join-gets-calendar`, `party-fast-travels` (days passed), `weather-follows-host` |
| Party panel (RmlUi) | works alone; fails after other scenarios | `party-panel` |
| Player levels (party panel) | works | `party-shows-levels` |
| Real input (keyboard, mouse) | works | `input-keyboard-draw`, `input-mouse-cast` |
| Mounts | covered; ownership returns after dismount/travel/leave | `guest-rides-host-horse`, `host-rides-horse`, `guest-rides-across-border`, `mounted-guest-leaves`, `party-travel-mounted-guest` |
| AI package, dialogue, factions | relayed; individual coverage documented | `docs/design/copy-contract.md`, `docs/design/shared-dialogue.md` |
| Physics: objects and ragdolls | the host simulates, guests show it live (keyframed by its frames, sent when the guess goes wrong) and rest at the host's spots; a dead NPC's copy lies as the host's bone by bone. A guest's push goes to the host as its character's impulse, its casts through its copy's, what it grabs follows its hands on the host and falls from where it let go; the guest shows its own until the host's rest, and gets the host's pose back for what its own physics moved alone. A guest gets frames only of bodies within 4096 units of its copy, rests of all (2026-10-25). A guest's playback is placed at its pose each frame and blends into frames it has early: no flicker, 0.1 units typical off the host's (2026-10-25, `FlightTrace` in the fireball scenarios). Since 2026-10-26 every game streams its own bodies (a guest's player's ragdoll, what it holds), and a living actor's copy is knocked down with it and lies as its ragdoll does. Not yet: a refused second grab, a dead body's pose at a cell load (`docs/design/physics-sync.md`) | `physics-push-reaches-guest`, `ragdoll-lies-as-host`, `knockdown-lies-as-host`, `guest-knockdown-lies-on-host`, `guest-bump-moves-host`, `guest-cast-moves-host`, `guest-throw-lands-on-host` |
| Crime / party bounty | host-authoritative | shared totals, guest alarms and fines from the party's purse; native coverage and jail/trespass-AI limits: `docs/design/shared-party-bounty.md` |
| Dropped items | one in the host's world with their extra data; pickups granted by the host (feature 6, 2026-10-26) | `docs/design/dropped-items.md` |
| Followers | on dev (uncommitted): shared vanilla slots, member anchors, fallback and picker inventory; native coverage/limits in design | `docs/design/followers.md`, `follower-*` |
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
- Built 2026-10-22: copies carry their owner's AI state (package, scene, combat state and target; sitting,
  sleeping and talking by replay), simulated only by the owner, read 1:1 by anything that asks
  (`copy-contract.md`, "AI state"; `actor-ai-and-copies.md`, "Combat state and its readers"). Open: alarmed
  (engine-made packages), the scene's own state (`IsScenePlaying`, quest sync), furniture and follow target
  written. Graph events from outside the actions wait for mods.

## Test suite

- **Join flow, stages 4 to 6 (2026-10-24, uncommitted, worktree `join-flow`).** Every join gets the host's world by
  save transfer: the host's game saves on its main loop (`JoinBridge`, `MainLoopTasks`: a save inside our update,
  which runs in the VM's update job, hung the host), snappy-packed chunks (`HostSaveTransfer`), the guest writes
  `SkyrimCoopJoin` and loads it, puts its character on the save's player, then joins. The character
  (`PlayerRecord`): the player base's save data by change flag but Factions, and the character parts of the player
  (perks with ranks, skills block, perk points, added spells, base values, items, tints), `Extras` for mod adapters;
  research and the engine's split in `docs/design/join-flow.md`, "Who are you". A new friend brings the character of
  its own game; one the host's save knows joins from the main menu as the kept one (`no_character` otherwise). The
  host keeps records from its guests (on joining, changes, leaving) and writes them into its SKSE co-save (`CoSave`,
  `SKCP`/`CHAR` v2). Guests' saves refused, with a chat line (autosaves once a session; `Save_Impl` 35727 hook).
  Scenarios: `guest-cannot-save` 3 of 3, `host-save-keeps-guest-character` 3 of 3, `guest-brings-character-into-
  hosts-world` 1 of 1, `guest-joins-from-main-menu-as-kept-character` 1 run (join and character pass; the after-scenario
  world diff found 7 QASmoke animated activators `ACTI 1BA5A` at another pose after the load: Loose ends). GameTests
  `[join]`, `[records]`, `[save]`. `+connect_lobby` built (GameTests `[steam]`, not run in Skyrim). Not built yet: the RaceMenu adapter
  (build/join-flow-report.md); the join screen came with the UI workbench (below). Live look refresh of copies: built later the same day (below).
  Later the same day: `guest-makes-character-then-joins` (a new game through the main menu, Quick Start - SE, now on
  in every test game's load order, the race menu driven as a player: Khajiit, female, named, then the join; compares
  the base's save sections, the tints, and the host's copy) passes 3 runs. It found: a copy never had its player's
  weight (no base save section has it; `Weight` now on the announcement and spawn, and in `PlayerRecord`, co-save v3;
  GameTests and the baseline's identity check cover it), and a guest joining from another race kept the host's
  player's tint masks (`ApplyPlayer` now rebuilds them for the race and sex, 40696, before the record's colours).
  Harness: keys work at the main menu (W/S/E are the menus' Up/Down/Accept; arrows and Enter only reach Flash raw),
  `char` types into text fields (`CharEvent`), `menu_var` reads strings, `input_state` shows the control map.
- **The UI workbench merged, the join panel wired (2026-10-24).** `UiWorkbench` (native RmlUi on GLFW/GL3,
  fixtures, `scripts/check-ui.sh`, in `scripts/check.sh`), `ui_models` (party activity, player colors, the
  join model), health faces, the AOL-style chat. In the game the join box follows `JoinService` (steps,
  refusals, Copy and Back by mouse or Escape); joins, departures and a player going down ("has perished.",
  red) are chat lines on every game, not a party panel notice. Scenarios: `join-panel-shows-refusal`,
  `party-panel`, `party-shows-levels`, `chat-reaches-everyone`, `join-refused-shows-why`,
  `guest-joins-by-invite`, `loaded-save-hosts-then-joins-friend` PASS. GameTests now run `SessionService`.
  Later: a new game hosts like a loaded save (hook on `NewGame_Enter` 52118; a game hosting alone steps down to
  join), and a guest that loses its host reconnects (3 tries; Steam rejoins the lobby), else its panel says the
  connection was lost; a kick says so without reconnecting (`join-flow.md`, "A guest losing its host"). The guest's
  network now tells the host closing (code 1) from a kick (2). Scenarios `host-rejoins` (reconnects by itself),
  `host-kicks-client`, the Steam ones and `guest-makes-character-then-joins` PASS.
- **Steam: join from an invite (2026-10-23, uncommitted).** The duo's client game runs as OS user `coop2` in its own
  network namespace, its Steam on a second account (every Steam serves games on the fixed 127.0.0.1:57343;
  `docs/design/testing.md`, "Two Steam accounts"; status `steam` shows each game's account). `SteamService` on
  `ISteamBridge` (Skyrim's `steam_api64`): the host makes a friends-only lobby and invites, the friend's accept hands
  its game the lobby, which connects to the host P2P and joins the session through it (enet tunnelled; the host's
  tunnel delivers into its own listener). `guest-joins-by-invite` 3 of 3 plus earlier runs; GameTests `[steam]`
  (FakeSteam): join, the host leaving, the friend leaving, a foreign lobby refused. A loaded save hosts by itself, a
  lone host steps down to join a friend's invite, the chat says who joined or left or why a join failed
  (`loaded-save-hosts-then-joins-friend`, 3 of 3 after its cleanup fix: a friend's tunnel closing kicks its session
  connection at once). Two lock-order families read and added to `lock_order_known.txt`. Next: `+connect_lobby`,
  then joining from the main menu with the host's save (join-flow stages 4 to 6, not built).
- Placed wildlife copies take the host's chosen NPC base (2026-10-23, uncommitted): spawn and refresh keep the
  exact placed reference, but apply the owner's base and reload its model/race. GameTests reproduces the Rabbit/Fox
  swap, including absent 3D and reversed refresh order; removing either the sent base or refresh handler fails it.
  Skyrim found the reload left every swapped actor disabled: `Actor::Disable` (37267) only queues the disable, which
  landed after the immediate Enable. `SetBaseObject` now calls the reference's own Disable (19801, what that task
  runs). New scenario `wildlife-takes-hosts-pick` (Riverwood's Rabbits, Deer, Mudcrab) failed without it and passes
  4 of 4; Riverwood's desync check is clean.
- Voice lines skip a copy with no 3D (2026-10-23): the guest crashed in `Actor::SpeakSound` voicing a bandit's copy
  it was still loading (`docs/re/crashes/2026-10-23-voice-on-unloaded-copy.md`). GameTests flow fails without it.
- **Quest writes instead of polling (2026-10-23).** The host reads quests once, then only quests marked by
  stage/start/stop/objective events, alias-fill/clear and Reset hooks, or the change funnel. Alias references
  moving or becoming shared invalidate their quests too. Fast tests and the plugin gate pass; restoring the
  timer fails three host cases. Skyrim was not run; quest stage, roles/fills, completion and late join remain.
- **Global writes instead of polling (2026-10-23).** The host reads globals once, then only ids marked by
  Papyrus SetValue (Mod uses it), console/compiled-script assignment and beast-form perk spending. The 250 ms
  timer is gone; the calendar remains HostClock's. The gate passes; restoring the timer fails four host cases.
  Skyrim was not run: global-reaches-guest, late-join-gets-globals and beast perk spending/refunds remain.
- **Actor-value writes and death events (2026-10-23).** The writer funnel (38470), TESDeathEvent and Resurrect
  (37321) replace the value/death scans. Only regenerating pools keep their 100 ms comparison; converged copies
  need no resend. Small hits flush each frame; copies wait for the owner's death-action tick, including a
  resurrection before its delayed replay. The gate passes; the old service/serializer fail eight GameTests
  cases and two wire cases. Skyrim was not run: combat, health, death and rapid resurrection remain.
- **Clear, refund and resurrection coverage (2026-10-23).** Six GameTests cover independent reference/location
  alias clears, both beast perk globals decreasing to zero, immediate revival, and owner/copy resurrection
  refusal. Each fails with the relevant quest/global delta or resurrection-event handler omitted; restored
  handlers pass the full gate (66 GameTests). No Skyrim run or new Skyrim-only scenario was needed.

- MMO settings/password/chat cleanup (2026-10-23, uncommitted): only the host's difficulty goes in the join answer
  and its updates; fixed co-op rules stay in code. Removed the unchecked token/password and extra chat channels;
  dialogue picks still show in chat. Packet fixtures and a two-guest join/change/hold/leave/rejoin flow
  (`session_settings_flows.cpp`). Merged with the character assignment: the join answer carries both.
  `ChatMessageTypes.h` is included by the chat broadcast's header, no longer by every message (`Message.h`).
- Four tiers (2026-10-23, `docs/design/testing.md`, Factorio's model): unit, integration against a fake engine,
  the duo, soak. Tier 1 `ClientTests` (new, `Code/tests/client/`): `PartyRules` (the cutscene share rule and the
  control holds, split out of `PartyNatives`), 45 assertions, rule edit to result 0.4 s. Tier 2 `GameTests` (new
  prototype, `Code/tests/games/`): two games in-process, real `TransportService`/`CutsceneService` and the host's
  rules on a fake engine; 4 cutscene flows in 1.0 s. Skyrim showed the fake's first guess wrong (movement takes
  jumping too, `0x401`). Then GuestScripts' tags and cause rules (`ScriptTags`), WorldEvents' own-event rule and
  a guest's own quests (`OwnEvents`) split out the same way, then NativeReach's code reading (`CodeReach`, Zydis on
  Linux): `ClientTests` 173 assertions. `GameTests` 11 flows: world-state sync between games on a fake world
  (locks, containers, cell state, rejoining, two guests), real `GuestWorldState` and `HostWorldState`, checked by
  `Party::Differences()`. `scripts/xmake-linux.sh` passes clang's flags through config files (CMake packages
  could not build on Linux). `guest-spawns-no-critters` fails when the host's weather drifted to rain (0x4D7FB):
  `CritterSpawn` places nothing then; passes with `fw 81a`. The scenario should set the weather as it sets the
  hour (open). The gate: `scripts/check.sh` (tiers 1-2 and the plugin's build, 3-11 s), run by the pre-push hook
  (`core.hooksPath scripts/hooks`; it pointed at a path that no longer exists, so no hook ran). `coop_test.py
  affected` built: changed sources select scenario tags with their file and glob reasons; an uncovered source fails
  loudly, a `core` change says the full suite covers it and never starts it (`--check`: all 1,183 files under
  `Code/` but tests covered). Shared specs (`Code/tests/specs/`): four cutscene scenarios and two world ones (a lock,
  a chest's items) as data, run by
  `GameTests "[spec]"` on the fake and by `scripts/coop_specs.py` in Skyrim (not run there yet); the fake learned
  `Game.ShakeCamera` for markers; all six pass in Skyrim too (21.5 s). The duo's baseline is pinned (the host's
  weather, noon, date, time scale, players' values and placement; the guest follows by sync, checked before
  cleanup; `raw_days` stays cumulative). `coop_test.py soak`: fresh-duo rounds, seeded shuffles, reports of
  crashes, lock patterns, stalls and memory compared with the last soak; two `scripts` rounds 28/28 each, no
  crash, one unreviewed engine-only lock pattern, the guest's memory 22.9 MiB higher across the two (a candidate,
  not a leak yet). The specs take part in `scenario`, `affected` and `soak`. Tier 2 copies: the real `CharacterService`, `TravelService` and
  `ModSystem` in every game (`ModSystem` reads plugins from `IFormBridge::GetLoadedMods`; the session-id lookups and
  script value conversions moved out of `Utils.cpp`), the host hosting through `HostService`; `copy_flows.cpp`.
  Found and fixed: an NPC unloading on the host in the frame between its assignment and the host's game handling
  the answer left the guest's copy for good (`OnAssignCharacter` now sends `RequestRemoveCharacter`). The `spawn`
  scenarios in Skyrim are still to run on it (the duo was busy).
  Travel flows (`travel_flows.cpp`): the tether both ways, players' copies coming along, NPCs staying with their
  cells; `RunLocalUpdates`' snapshot throttle is per service now (was a function static). New rule in CLAUDE.md:
  every bug gets a test in the fastest tier that can show it.
  Tier 2 actors (2026-10-23): an actor checksum (`Party::Agree`: each owned actor's one copy everywhere, its cell,
  position within 64, dead, enabled, copy flag; held copies as the host's references), `ActorValueService` and
  `PlayerService` in every game, the fake's placed NPCs, health, death, bleedout, respawn and created actors deleted
  with their cell; flows for late join, a host NPC going away (moved, disabled, deleted, back), death and a
  guest's respawn (indoors and out: the fake has exteriors now), a placed NPC the party leaves and returns to, an
  NPC loading as a follower arrives; 24 flows, 7.7-10.2 s; each new flow failed with its covered line broken. Found and fixed: a guest joining elsewhere was never
  brought to the party; an NPC announced in the frame a follower's arrival reached the host's rules never reached
  that follower; the host's arrival sent a guest ahead of it the NPCs of the cell it left; `ActorValueService`'s
  send timers and `PlayerService`'s respawn flag were statics two games shared; a respawn outdoors pulled players
  out of the respawned player's grid (its arrival is `kRespawn` now). Four actor shared specs (`Code/tests/specs/`: an NPC's
  death, disable and trip home, joining elsewhere) with actor steps and expectations
  (`actors agree`, `actor`, `together`, `down`; respawn's spec dropped, parked for the spectate design); pass in the fake, the Skyrim side to map them in `coop_specs.py` (`docs/design/testing.md`, tier 2;
  `copy-contract.md`, "Arrival races"). The fake's unmeasured assumptions are listed for the Skyrim side.
  Tier 2 equipment the same day: `InventoryService` in every game, the fake's equipment (hands, armor slots, our
  equip hooks' events and refusals), worn items in the checksum; flows for a player's weapons and armor, a host
  NPC's weapon, an armor swap (measured in Skyrim) and a late joiner's dressed copy (`equipment_flows.cpp`); items (gold, a dropped sword, a later
  joiner's copy of another guest as it is now, `item_flows.cpp`) and drawn weapons, carried items and hands in the
  checksum. Clock, weather, map markers and time scale the same day (`clock_weather_flows.cpp`, `shared_flows.cpp`);
  magic, effect visuals and AI state (`magic_flows.cpp`); combat and objects (`combat_flows.cpp`, `object_flows.cpp`);
  dialogue's games (`dialogue_flows.cpp`, `FakeDialogue.h`); health, doors, globals and kicks (`values_flows.cpp`,
  `world_flows.cpp`, `session_flows.cpp`). The baseline is done: every working feature has a fast test, details
  left for after the MVP. Coverage per scenario: `docs/design/test-coverage.md`.
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
- The gate in 4 s (2026-10-23, uncommitted): it took 21 s, 20 of them `GameTests` asleep (0.3 s of CPU). The host's
  listener slept 2 ms on every `Net::Server::Update`, which `HostService::OnUpdate` calls on the host's game frame
  (upstream's server loop thread left it): the host's Skyrim lost 2 ms a frame, fixed. `Server::Close` and
  `Client::Close` waited up to 100 ms for an answer on the frame; they flush the disconnect instead. Test:
  `BridgeTests` "[Network]" (205/100/100 ms before). `GameTests` then took 6.4 s, waiting on the services' real
  timers; `check.sh` runs it in 8 shards (2 s), each on its own ports (`GAMETESTS_PORT`).
- Relayed messages are a template (2026-10-23, uncommitted, ideas.md "Code cleanup"). The 41 headers that only
  wrapped a payload (`Payloads.h`) are `using` lines of `Relayed<Payload, Opcode>` in `Messages/RelayedMessages.h`
  (declared there, for headers that named them ahead; defined in `Relayed.h`); 41 headers gone, about 950 lines net.
  `Opcodes.h` lists every message once in opcode order (`COOP_REQUESTS`, `COOP_NOTIFICATIONS`), which makes the
  opcodes and both factories' `Visit` (`CreateMessageVisitor` is gone). `kRequestRemoveSpell` is
  `kRemoveSpellRequest`, as its type. All 104 opcode values compared equal before and after; `wire_tests.cpp`'s
  bytes unchanged.
- No form id translation (2026-10-23, uncommitted, ideas.md "Code cleanup"): upstream's `ModSystem` mapped each
  client's load order to the server's. The join refuses any guest whose plugins differ from the host's in set,
  light flag or order (`HostSession`), so a form has the same id in every game: `ModSystem`, its table and the
  join answer's numbered plugin list are gone. `FormIds.h` (no state): `ToSession` (none for a created, 0xFF, form;
  0 is `GameId{}`), `ToGame` (0 for a created id), and for the messages that rebuild created forms
  `ToSessionOrCreated` / `CreatedId` (the old `0xFFFFFFFF` id): world state's references (`HostEngine`,
  `ReadContainerContents`), a spawn's reference (`CharacterService`), enchantments, the save buffer's ids and an
  action's target. The 87 `GetServerModId` calls and every `World::Get().GetModSystem()` chain are plain calls; no
  `ModSystem&` goes through the bridges. Handling that changed: a created poison goes without an id (its receiver
  asserted against one); sends that ignored the result carry no id for a created form; guarded ones drop instead
  of sending an id no game finds (a guest's activation, lock or equip of a created form). `ClientTests`
  "[FormIds]". `Inventory.h` includes `<optional>`.
- One line per hook (2026-10-23, uncommitted, ideas.md "Code cleanup"): `HOOK_ID(ID, &Real, HookFn)` (and
  `HOOK_ID_IMMEDIATE`, `Hooks/FunctionHook.h`) replaces the `POINTER_SKYRIMSE` / `Real = .Get()` / `HOOK` triple at
  122 installs in 38 files (266 lines); signatures, ids, breadcrumb names and install order are unchanged. Left as
  they were (24): setup between resolving and installing (`Memory.cpp`'s two, `Actor.cpp`'s `ApplyMovement`,
  `ResetInventory`, `UpdateAI`, `GuestScripts.cpp`'s nine), relocation names an engine-call wrapper reuses
  (`EquipManager.cpp`'s two, `TESObjectREFR.cpp`'s `SetPositionOnAxis`, `SetAngleOnAxis`) and other target forms
  (`ExitProcess`, DInput's device, four `VersionDbPtr` casts). `build_re_data.py` reads the new form: the same 441
  ids, 486 uses and 152 hook rows.
- Event connections in one vector (2026-10-23, uncommitted): `Listen<Event, &T::Handler>(m_connections,
  m_dispatcher, this)` (`Events/Listen.h`, with a sink overload for the registry's `on_destroy` and the ImGui draw
  signal) replaces 123 `scoped_connection` members in 26 classes, each vector where its members were (97 fields).
- `BridgeTests` is `HostAuthorityTests` (2026-10-23, uncommitted): `Code/tests/host_authority/`, `[HostAuthority]`
  tags, the `HostAuthorityTest` namespace; the gate, `CLAUDE.md` and the design docs follow. Dated entries keep
  the old name.
- No character assignment round trip (2026-10-23, uncommitted, ideas.md "Upstream's MMO leftovers"): the host hands
  out every session id and nothing answers an announcement. A player's character id comes in its join answer
  (`AuthenticationResponse::CharacterId`, reserved by `HostCharacters`); the host's game takes its actors' ids from
  `HostCharacters::NewCharacterId` (in-process, inside `RunActorDiscovery`'s existing host check). A game owns an
  actor from its announcement (`CharacterService::AnnounceCharacter`, `AssignCharacterRequest::SessionId`); the host
  drops one under an id it did not give. Gone: `AssignCharacterResponse`, `CancelAssignmentRequest`, the cookie,
  `WaitingForAssignmentComponent`, `InventoryService`'s queue for actors waiting on an id, and `HostCharacters`'
  hold-back of host actors not yet moved; with them the unload-between-announcement-and-answer race. A player
  announced again (respawn) keeps its id; its copies are made again as before. Tests: `HostAuthorityTests` "only
  under an id the host gave"; two host-rule checks that passed with their announcement refused now announce under
  the right id; the copy and item flows follow the new order.
- Cleanup leftovers (2026-10-23, uncommitted): `AssignCharacterRequest` is `AnnounceCharacterRequest`
  (`CancelServerAssignment` is `ReleaseCharacter`); `AnimationSystem::Serialize` lost a check that could not fail.
  Hooks: 20 more installs are `HOOK_ID`, the four explicit `VersionDbPtr` casts included; `Hooks::Resolve<ID>(&Real)`
  resolves one hooked later or only on a condition (`Memory.cpp`). Left: `ExitProcess` (`GetProcAddress`) and DInput's
  device vtable, which have no Address Library id. `build_re_data.py` reads `Resolve`: 148 hooked ids (six call
  wrappers it had labelled hooks by a shared variable name are calls now; four hooks it could not see are found).
  `PlayerNativeResultRequest`, `NotifyPlayerNativeResult` and `NotifyPartyNative` (payload `PartyNativeFrom`) are
  `Relayed` too; bytes unchanged. `coop_test.py`'s source tags cover the new files.
- Mounts work (2026-10-23, uncommitted). A guest's player getting on a copy asks (`MountRequest`);
  `HostCharacters::OnMount` makes the rider's game the owner and tells every game, the rider's taking it
  (`TakeOwnership`, private). Getting off (`Actor::Dismount` 37906 hooked) gives it back (`DismountRequest`,
  `OnDismount`, `NotifyDismount`: the rider's game makes it a copy, `GiveUpOwnership`; the host's takes it). Fixed with
  it: a rider leaving had its horse removed from every game; now it goes back to the host. Every other game's copy of
  the rider gets on at once (`Actor::PutOnMount`: a `MountInteraction` 40345 attached 41532, as InitiateMountPackage
  links a low-process rider) instead of a mount package a copy never runs (it stuck after the ride); it gets off by
  replaying its owner's `HorseExit`, the engine's interaction update unlinking it. A mount's half of a paired action
  is never forced with its rider's (`AnimationSystem`): ownership changes hands mid-pair, the mount's owner plays it.
  A game taking an actor sends its latest action (through `ReadLiveState`'s `TargetToNetwork`: a paired partner as a
  session id, not a form id, or 0x14 lands on each game's own player) and its AI state; a created package (a ridden
  mount's DoNothing, `PutCreatedPackage`) raises the AI state too. Scenario `guest-rides-host-horse` (Riverwood: the
  copy fades in, the guest gets on, both games agree while riding, gets off; 4 of 4). `HostAuthorityTests`
  "Mounts: ..." (the leaving case fails without the fix). Readouts `riding`, `ridden`, `getting_on_off`,
  `mod.fade_state` (nothing activates an actor fading in, `TESNPC::Activate`), `mod.player_can_ride` (38706).
- A player's active effects go into its record (2026-10-23, uncommitted, `HostCharacters::OnActiveEffects`): spawns
  built later from the record had the effects of its announcement. Test "a player's effects go into its record".
- The actor comparison's "visuals" are what plays on the body (2026-10-23, uncommitted): the owed Skyrim checks
  failed `spec:guest-joins-elsewhere` at "actors agree", the host's copy of the guest's player lacking 0xB94EC.
  That is `FXCameraAttachFluffObject`, which Riverwood's `FXCameraAttachScript` volumes play at the guest's camera
  on its player: the screen's, not the actor's, so no copy shows it (`docs/design/copy-contract.md`). The harness
  now skips visuals whose controller attaches to the camera (`ReferenceEffectController::EffectAttachesToCamera`,
  vtable 0x1A: true for `OwnedCameraEffectController` and `SkyEffectController`, false for active effects' and
  scripts' other effects, IDA 1.6.1170) and shows them as `mod.camera_visuals`. Not a sync bug, so no fast test;
  the fake engine plays no camera effects. To confirm in Skyrim: the rerun was held up by a Steam download
  stalling Riverwood's load (the loading spinner stayed up past the step's 60 s).
- Harness (2026-10-13): a player's button waits for the game to take input (no fade or menu);
  `both_go` waits for the arrival's fade; `attack` waits for the last swing to end; `place` takes a
  bearing; lock patterns match in either order.
- Bridge rules: `BridgeTests` (`Code/tests/bridge`), the host's rules with in-process games on `HostNetwork`, wire
  bytes pinned in `wire_tests.cpp`. Build with `scripts/xmake-linux.sh` (clang 18, lld; since phase 5).
- Encoding: `TPTests` (`Code/tests/encoding.cpp`), same build.

## Loose ends

Native bug pass and audit (2026-10-28): 11 of the 16 reports addressed, including two fixture corrections.
The three remaining ragdoll/get-up reports are accepted for v1.0.0; the AI/quest lock report is deferred.
Plugin quest start/stop compatibility is deferred with mod work. The audit removed unsuccessful pose
reconstruction and the additional plugin event relay. Barter Speech is implemented and tested for both players.
Mod-dependent Experience work is outside v1.0.0. Evidence:
[native-bugs.md](design/native-bugs.md); accepted reports: [bugs.md](bugs.md).

- **A guest crashed once in `follower-trade-to-picker` (2026-10-29, party dialogue).** `Actor::UpdateInDialogue`
  (37541) on a worker reached 39372, which dereferenced a null RCX; Lydia (`FF001E90`, the fixture's follower) was
  already flagged deleted (the client prefix's `SKSE/crash-2026-10-29-09-52-36.log`). Not reproduced: the scenario passed
  3 of 3 since. Lead (v1-engine-fixes' read, unconfirmed): the baseline between scenarios deletes the cell's NPCs
  while queued dialogue work still holds her, so a fixture teardown race rather than play code. If it shows again,
  check whether the baseline deletes an actor still in a conversation (and end the conversation first).
  Since: the baseline ends conversations before deleting NPCs, and the play path (the host removing the NPC
  mid-conversation) passes as `dialogue-npc-removed-mid-conversation`.

- **Any added hook breaks `guest-brings-character-into-hosts-world` (found 2026-10-24, physics-sync).** With one more
  `HOOK_ID` (physics-sync's `PlayerHands` 79133, a `StartGrabObject` 40552 hook, or a test hook on 40557, which the
  join never calls) the scenario's own steps pass, but the baseline finds the host's player ~40 units off its spot
  (`left behind`); with `SKYRIMCOOP_DISABLE_HOOKS=PlayerHands` it passes 3 of 3. So not the hooks' code: something
  in hook installation (order or count) the stacked players' arrival depends on. Not traced yet (no overlapping
  `HOOK_ID` targets). Until then the scenario fails in the physics-sync worktree.
- **Fixed 2026-10-29: a save load left QASmoke's animated cover plates (`ACTI 1BA5A`) at another pose** (found
  2026-10-24). The loaded graph's local bones matched but its world transforms were stale: `ApplyGraph` now updates the
  3D after loading (NiAVObject::Update 70251), equal graph rows included
  ([evidence](re/crashes/2026-10-29-lock-history-and-button-pose.md)).
- **Fixed 2026-10-24: a player record's perks doubled on every join** (`JoinBridge::ApplyPlayer` added each kept perk
  again; `ReadPlayer` now lists a perk once). `guest-joins-from-main-menu-as-kept-character` also waits for the perk it
  adds before restarting (the record goes a few seconds after a change).
- **The lock-order checker counted our main loop and VM update hooks as "ours"** on every main thread stack: they are
  pass-through frames now (`LockOrder::IgnoreFrames`); our other pass-through hooks (AI jobs) still count.


Everything known to be unfinished, unverified or unexplained. Close an entry by fixing it, proving it
harmless, or moving it to `docs/ideas.md`; say where the evidence went. Details are in the doc named.
Bugs we chose to ship with in 1.0 are listed in `docs/bugs.md`.

### Mounts (2026-10-23)

- A copy made while its owner rides (a late join, a rejoin) starts off the horse: the spawn carries no mount. Design:
  the host keeps each mount's rider (from `OnMount`, cleared by `OnDismount`) and sends `NotifyMount` after a
  newcomer's spawns; the newcomer links once both copies have loaded.
- ~~Riverwood, seen in the duo's desync check: two of the host's placed NPCs (Rabbit 00105A25 sid 85, Fox 00105A35
  sid 86) showed swapped on the guest (each copy the other's name and race).~~ Fixed (placed wildlife, "Test suite").
- `host-rejoins` failed once on Lydia landing a jump just as the end-of-scenario comparison ran (the copy not yet
  replaying it); passed twice after.

### Mods

- **EngineFixes and our `FormAllocate` hook** (2026-10-22): upstream rehooked it when EngineFixes replaced
  it, but only from its own launcher, so under SKSE the rehook never ran and is deleted. Whether
  EngineFixes' allocator patch overwrites our hook (the heap history and form allocation) is untested.

### World state

- **Done 2026-10-20: a guest's story manager starts no quest.** `BGSStoryManager::ProcessEvent` (32372) is skipped while a guest, so its story events (arrivals, kills, script events) are handled with none started (`docs/re/systems/story-manager.md`, scenario `guest-story-starts-no-quest`). Still open: quests a guest's own scripts or console start (`EnsureQuestStarted`), with step 5. A guest's story events go to the host since 2026-10-27 (every type since 2026-10-28).
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

- **Fixed 2026-10-27: a guest bringing a character stood on the host's spot.** Before reconnecting, it now waits
  for checked placement beside the save's player position (floor, slope, headroom, room and clear path). Kept
  characters retain their saved place. ClientTests geometry cases, GameTests including a blocked arrival, Skyrim
  `guest-brings-character-into-hosts-world`; [small-bugs.md](design/small-bugs.md).
- **Fixed 2026-10-27: a look change remade the copy.** The same actor and base now take FormState's appearance,
  weight, runtime race/3D and fresh FaceGen tints. Rebuilds run between frames and invalidate the old graph's
  variable descriptor (a humanoid layout on a werewolf graph caused a crash). Deferred/empty-tint and identity
  regressions in GameTests; Skyrim RaceMenu, vampire and Beast Form transitions; `small-bugs.md`.
- **Fixed 2026-10-27: a copy's base level stayed its spawn's.** `NotifyPlayerLevel` now writes the matching copy's
  NPC base level and change flag `0x2`, alongside the roster update. Fresh copies use the latest roster level.
  GameTests checks increases/decreases in both directions without replacing the actor; Skyrim `party-shows-levels`
  reads each copy's actual base level; `small-bugs.md`.
- **Fixed 2026-10-27: a resurrected NPC copy refused subsequent actions.** It resumes graph updates and speech,
  then takes the default graph event once its new 3D accepts it (the first requests were refused). The owner's
  native resurrection reset stays local so its dying-state action cannot become another death on the guest.
  Fast regressions first, then `death-host-npc` drawing and swinging on the same copy, three consecutive passes;
  `death.md` and `small-bugs.md`.
- **Open 2026-10-24: a copy's base has no original race.** `TESNPC::Create` clears it (`TESNPC+0x1E8`, upstream's
  "forces facegen"); the owner's is kept. Only a beast form's return reads it, and a copy follows its owner's race.
  Scenarios compare the race section by its race only (`copy_base_differences`).
- **Fixed 2026-10-27: an instant effect with FX Persist showed nothing on copies.** Zero-duration fire-and-forget
  effects send the existing flash, and the receiver now accepts FX Persist flashes too (the earlier sender-only
  experiment left that filter rejecting them). Constant abilities and concentration retain their normal policy.
  Pure rules and a GameTests effect already gone before the frame; Skyrim `copy-flashes-instant-effects` shows
  the shrine's `CureDiseaseEffect` glow and its fade on the copy. Gift of Charity still passes; `change-events.md`
  and [small-bugs.md](design/small-bugs.md).
- **Fixed 2026-10-23: a player's first items and equipment were lost under latency.** A change made before its
  session id came back was dropped; queued now until the assignment (`InventoryService::RunPendingChanges`). Found
  by tier 2's network conditions, new the same day (`COOP_NET`, `scripts/net_check.sh`: every flow through a relay
  that delays, jitters and drops the guests' datagrams; all pass at 120 ms, 80 ms jitter, 10% loss).
- **Fixed 2026-10-23: a guest brought to the party at its join had no player copies** (`guest-joins-elsewhere`,
  the four-cell join). The copies made at the join were lost on the way, and the arrival sent the host's actors
  only. Now its arrival by the party's travel sends the players' characters both ways
  (`HostCharacters::SendPlayersMet`; tier 2 flow and spec, `BridgeTests`; `docs/design/testing.md`, tier 2 bug 7).
  Skyrim's first run found its join-time arrival counted too, outdoors; only the `kTether` arrival counts now.
- **Fixed 2026-10-23: the host's Take All reopened the guest's container menu** (`chest-take-all`). The host's lid
  opening, arriving after the guest closed the chest, found the guest's player still recorded as the lid's
  activator and opened its menu (`BGSOpenCloseForm`'s lid event, 14285). Applying the host's open state clears that
  record first (`ReferenceStates::ApplyOpen`; `docs/re/systems/guest-world-events.md`). Only Skyrim shows it (no
  lids in the fake): `chest-take-all` to confirm. The replay's `StealAlarm` on an owned chest returns before any
  crime on the guest (measured).
- **Done 2026-10-22: a host NPC leaving no longer leaves a ghost on the guest.** The removal carries where the host's actor went; the guest's reference follows it (moved, disabled or deleted) and stays a copy until the host spawns it again (`docs/design/copy-contract.md`, "When the owner's actor goes away"; `host-npc-leaves-copy-follows`). Still open: a guest's non-persistent references are made anew when it loads their cell, unflagged, so they run their own AI until the host's spawn arrives (not measured).
- **Done 2026-10-22: a guest's player is in combat when the host's NPCs fight it** (`guest-player-in-hosts-combat`;
  above, overnight slice 4). Not checked: combat music and the wait/save block on a guest.
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
- **Done 2026-10-22: a copy jumps with its owner.** A script's `MoveTo`, `SetPosition` or `SetAngle`
  (`TESObjectREFR::MoveTo_Impl`, 56626, from the script functors only) and the console's `setpos`/`setangle`
  (22053, 22054) on an actor raise `ReferenceJumpEvent`; the owner's next snapshot carries `Movement::Teleported`,
  and the copy drops its earlier points and starts at the new place, as for another cell. `TESObjectREFR::SetPosition`
  (19790) is not the signal: root motion, projectiles and flight call it every frame. Scenario
  `copy-jumps-with-owner` (a host NPC moved 900 units with `setpos`: no read of its copy in between).
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
- ~~**Leveled wildlife picks its own base on each game**~~ (host Rabbit is a Fox on the client, Deer an
  Elk). Fixed (2026-10-23, "Test suite" above; `wildlife-takes-hosts-pick`).
- **Console `disable`/`kill` on a wild wolf did nothing** on the host, no error logged. Not investigated.
- **Trespass warning AI:** shared party bounty now judges guest crimes on the host and maintains
  guest-copy trespass state (2026-10-26); native warning-AI coverage remains open (`docs/design/shared-party-bounty.md`).
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

- **Host crash freeing a behaviour graph in a guest's beast form** (2026-10-23): noted, not investigated (`docs/re/crashes/2026-10-23-guest-beast-form-graph-gc.md`).
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
- **Host hang in `dialogue-refused-start` (2026-10-22, the 25th scenario of a `--tag ai,combat,magic,spawn,session,scripts`
  run; 3 of 3 alone).** The main thread waits for jobs in `Main` (36555); one job thread waits in `Mutex::Lock1`
  (13129, the engine's `BSSpinLock`) from `MenuTopicManager` (35289) → `Actor::UpdateInDialogue` (37541) → our
  pass-through `HookSpeakSoundFunction` → `SpeakSound` (37542) → `SetupSpecialIdle` (39256) → `sub_1406F0790` →
  `sub_1406DE5E0`, for the speaker's (Lydia's) `MiddleHighProcessData+0x1E8` spin lock. No thread in the dump is in
  one of the eleven functions that take that lock (`sub_1406DFBC0` ... `sub_1406F1020`, all scoped), the main
  thread is in Main's own wait: the lock looks leaked or its memory reused (Brenuin, her foe, was disabled and
  deleted 0.2 s before the host spoke to her). Not reproduced. The lock-order checker covers only
  `BSReadWriteLock`, and `BSSpinLock`'s unlock is inlined, so it cannot name a holder; instead the HangWatchdog now
  logs, for a thread waiting in `Mutex::Lock1`, the lock (its `rdi` in that frame) with its owner thread id and
  count and whether that thread exists (`CrashHandler::SetSpinLockWait`). Next time: an owner that is a listed
  thread names the holder, one that does not exist means a leaked lock or freed memory (then
  `repro --heap-history` on the process data).
- **Portal graph crash:** our fix works on the repro; SCN-Crash-fix was not run against it
  (`docs/re/crashes/2026-10-11-qasmoke-exit-portal-graph.md`).

### Migration

- **Fixed 2026-10-20: the client's `GameDaysPassed` missed host time jumps** (measured 2026-10-19 after `party-fast-travels`: 0.4376 / 0.3866). `TimeModel` carries the whole days passed; a guest writes the host's calendar as `Calendar::Update` does, `rawDaysPassed` included (`docs/re/systems/calendar.md`). Its date is the host's too: `SyncPlayerCalendar` is no longer read. Scenarios `host-wait-reaches-guest` (the Wait menu across midnight), `late-join-gets-calendar`, `party-fast-travels`. Also fixed: a guest rejoining ran ahead by the time it was away (`CalendarService` counted the gap).
- **Fixed 2026-10-20: set-time set nothing.** `SetGameTimeHours` was a stub, and only the compiled-out CEF overlay sent the command. Now `/settime <hours> <minutes>` in the RmlUi chat sends it; the host runs its clock on to that hour (the next day's if earlier) with `Calendar::Update`, as a wait does (`HostEngine::SetHour`). Scenario `guest-sets-time`.
- **Tether gaps** (2026-10-14, `docs/design/host-authority-migration.md`, "Party tether"):
  - Ready for v1.0.0 testing, accepted by the user 2026-10-27. Remaining observed travel issues are deferred in `docs/bugs.md`; validation details and retained limits are in `docs/design/party-travel.md`.
  - Departure announcements built 2026-10-27 (`party-travel.md`): 40438 observes queuedTarget before loading, including coc's inline queue. Followers no longer wait for arrival. Actual load starts retain network/frame skew (21–40 ms locally); cached 8 ms loads can still finish before a follower starts.
  - Guest fast-travel time fixed 2026-10-27: the native calendar step is sent to HostClock, which advances the host once and shares the result. A three-player midnight flow and the native fast-travel scenario cover it.
  - Every travel of a player's own leads: a scripted move on a client (a quest teleport, jail) takes the
    party along too. Within a cell or the loaded grid nobody moves. Not a respawn (2026-10-23): upstream's
    `RespawnPlayer` moves the player to its cell's COC marker, which outdoors pulled players more than two cells
    away; that arrival is `kRespawn` now and leads nobody (tier 2 "Outdoors, a guest's player goes down").
  - Two players leading at once to different places can end apart (each game follows in the host's
    order; a leader may follow the other one afterwards). Not seen; the fix would be the host keeping the
    party's last destination and sending a follower that arrives elsewhere on to it. One leader travelling
    again before the others got there is handled (`party-quick-travel`).
  - A follower whose tether move never arrives stays "following" for 60 s: a travel of its own in that
    time does not lead.
  - A player joining somewhere else is pulled to the host's player and pulls nobody (2026-10-23,
    `HostCharacters::BringToParty`; tier 2 late-join flow, `BridgeTests` "a player joining away from the host").
    Outdoors also when out of the host's loaded grid in the same worldspace (fixed the same day after Skyrim's
    four-cell join: `InHostGrid`).
  - Players walking apart outdoors work (measured 2026-10-23): the far copy is kept without 3D and loads again on
    return. A far respawn's new copy is lost out of the host's grid and not sent again (respawn parked).
  - Menu/dialogue/dead/mounted follower cases and three-player fast flows added 2026-10-27; evidence and limitations: `docs/design/party-travel.md`.
- **The old CEF overlay and its Angular UI (`Code/skyrim_ui`) are gone** (checked 2026-10-23); they were compiled out of the
  Windows build: the RmlUi panels replace them (party, chat).
- **Quests** need their own design before sync goes on.
- **Build ids of uncommitted trees match each other.** `BuildCommit()` is `git describe --tags
  --always --dirty`, written on every xmake run (`Code/common/xmake.lua`; it was configure-time only
  and lagged commits until 2026-10-13): two different uncommitted trees on the same commit both read
  `...-dirty` and pass the gate's stage 1. Fine for development; release builds come from commits.

### Tests and harness

- **Open 2026-10-27: a duo booted from a late save never connects** (`duo --save LateAfterElderKnowledge`, twice):
  both games load it, the client joins, and neither shows the other in 30 s; the client's log shows it creating a host
  of its own ("port 10578 is in use, trying 10579"). The late-save scenarios (`--tag late-save`:
  `elder-scroll-vision-reaches-party`, `greybeards-scene-casts-party`) wait on it. Logs: `coop-test/runs/20261027-185548-duo`.
- **Open 2026-10-24: the race menu's Weight slider, driven by harness keys, moves but the base's weight does not.**
  `ChangeWeight` (`sub_1409524D0`) writes `TESNPC+0x1FC` only when the value differs from what the menu stored; with
  D presses the slider reached its top in one step and the weight stayed (50, or 75 once). Not understood;
  `guest-makes-character-then-joins` leaves the weight at its default and still compares it in every game.
- `dive-host-player` failed 1 of 2 on a fresh duo (2026-10-21): the copy's animation event `SwimStart` vs the
  owner's `swimStop`; once left the copy showing visual `77785`. Cause found 2026-10-22: the copy swept across the
  teleport home through QASmoke's water (above, "Copies sweeping across a teleport"); fixed.
- **Checked 2026-10-22: a copy that swam stops swimming with its owner.** `copy-leaves-water-with-owner`: the host
  swims in the river and is put on its bank; its copy on the client swims, then stops (2 of 2). Not covered: an
  owner walking out up a shore (the copy follows at its owner's pace there).
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
  Seen again 2026-10-23 with its stacks (an outfit reset on the remove side): added.
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
- **Decided for review 2026-10-22: a cast's hazards are each game's own**, as its projectiles are: sync never sent them
  (`HostEngine`'s `ReadCreate` takes plain references, a placed hazard is `PHZD`), each game's replayed cast makes
  its own, and the world check now skips them (`world_differences`). Undo the check's line to make the host's the only
  one instead. The history:
- `burning-shows-on-copy` fails its world check about half the time (2026-10-21, 3 of 6 after the `TiltedPhoques` steps;
  it passed the run before them): each game's own fire hazard from the spell's impact (`HAZD` 3E075, a created
  reference) is still there when the check looks, at a different spot in each game. Either hazards are each game's
  own transient effect and the check should skip them like projectiles, or the host's should be the only one;
  not decided. Seen again 2026-10-22, in `flames-client-hits-host-npc` too, as a hazard alive on one game only (the
  host's or the client's, with and without the event changes): the two games' hazards end at different times.
- **Done 2026-10-22: `party-stacks-on-arrival` failed every run** ("[client] came within 9 units of the other player: it
  walked through it"). The game was right: the client walked off the spot and back into the host within half a
  second, and a player leaves the stack only after half a second off it (the grace for a one-frame jump), so it was
  stacked again. The scenario's walk off now waits for its game to take it off the stack (the `Stack: … walked off`
  log line); 2 of 2.

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
- `party-uses-door` once saw the host's activation of the inn door (13419) do nothing for 60 s, with no log line
  on either game during the step (2026-10-22, after a hot reload); it passed twice right after. Unexplained.
- The stuck `LoadWaitSpinner` happened again (2026-10-13, host, the first `both_go` to Riverwood on a fresh
  duo); a restart cleared it. Third time 2026-10-21 on the client, from `host-kicks-client`'s rejoin onward
  (every later step waiting for input timed out); a fresh duo ran kick, late join and the failed scenarios
  clean. A harness readout of what opened the spinner would catch it next time.
- The map's fast travel UI (open the map, pick a marker, confirm) is not driven; `party-fast-travels` uses
  the console's `FastTravel`, which queues the same move (`docs/re/systems/player-travel.md`).
- The `duo` harness process once segfaulted on exit after a pass.
- Harness console lines other than cell changes run unpaused, unlike a typed line.
- `scripts/re/ida_export.py` exported 58,317 rows (every named address) instead of the ~100 hand-set ones
  (2026-10-12); the six new rows were added to `docs/re/ida_notes.tsv` by hand.

## v1.0.0 features (decided 2026-10-24)

What the first playtest build has besides what works today (the table above):

1. **Death: spectate, then respawn beside the party** (`docs/design/death.md`, `.worktrees/death-spectate`):
   built 2026-10-24; host authorizes recovery after 10 continuous seconds clear, spectator camera/UI and
   party-wipe checkpoint reload replace the old 5 s/COC respawn. GameTests, UI fixtures, plugin build and the
   host-death, guest-death and wipe Skyrim scenarios pass. Fixed stale copy magicka and recovery graph state.
   Ragdoll physics/settling is feature 5's integration; report: `build/death-spectate-report.md`.
   Real death since 2026-10-26 (uncommitted): the player really dies in a session (mods' `OnDeath` fires) and is
   revived. The guest's place after a wipe and `death-spectate-guest`'s staging fixed the same day (`death.md`).
2. **Shared party bounty** (`docs/design/shared-party-bounty.md`, `.worktrees/shared-party-bounty`):
   merged into dev, 2026-10-26. Host witnesses judge guest actions; violent/nonviolent bounty and
   infamy and native player-enemy flags mirror per faction. Guest bodies expose shared bounty and
   known crimes to native queries; host NPCs apply the party's crime reactions to guests too.
   Guest fines debit the chooser once, confiscate on its own game and end guest-body hostility/combat.
   Review adds guard arrest/jail: a guest's real theft opens vanilla arrest choices, its fine charges the
   party's purse (since 2026-10-27), and resisting makes the guard fight that guest. A jail choice runs the native sentence
   on the host; the existing tether brings every player into the same locked cell (no new jail travel).
   Fixed guest arrest greetings, stale option lists and locally rendered fine text; engine layouts are typed.
   Authority/encoding tests, seven crime/dialogue flows under both lossy profiles, UI and plugin build pass.
   Native guest arrest/fine/resist/jail checks PASS. The host jail control reaches the sentence and brings
   the guest, then reports an existing physics lock inversion during release. Six dialogue and nine older
   crime regressions PASS; pickpocket's final world comparison fails on a QA ladle pose. The final merged
   gate passes all 115 GameTests, authority/encoding checks, UI fixtures and the plugin build. Prior native
   physics failures and review evidence remain recorded in the design doc.
   No existing Games/Skyrim hooks or death/body files changed.
3. **Quest rewards to everyone** ([quest-rewards.md](design/quest-rewards.md); built 2026-10-27 with feature 11,
   merged into dev uncommitted). Skill XP inputs, skill levels, perk points, perks/reversals and
   permanent stat changes reach every player as the party's calls (the reward natives relayed, `dragonborn.md`).
   Body/effect/equipment gains and a picked line's Speech remain personal; items and gold stay with the picker.
   A host pick is tagged before its begin fragment runs. Native quest stage/completion events reach guests without
   their reward fragments. No catch-up ledger; earned permanent stat totals are kept in character records.
   Calibration exposed the old combat XP relay double-applying rewards; removed, and 40488's ABI fixed.
   Validation: full gate PASS (142 GameTests, 57 ClientTests, 164 authority cases, encoding/UI/DLL);
   both lossy reward/record profiles PASS; six real turn-ins plus two native checks 3/3 shuffled (24/24), full
   character rejoin PASS; Speech/training/shrine/beast/dragon/personal-XP regressions 6/6 PASS. Duo stopped/freed.
   The known plate-carry flake failed earlier and passed the final gate. Owners receive shared perks, but a
   copy's HasPerk still reads its own missing list; guest start/stop events are not guaranteed completion events.
   Both limitations recorded in `bugs.md`; Experience DLL not installed/tested. No commits.
4. **Shared wait and sleep** (`docs/design/shared-wait-sleep.md`): built 2026-10-26. Any player's wait
   or sleep opens every game's native menu; the host's countdown drives them all. The party-event
   follow-up makes a guest's sleep run the matching native rest on the host's player too, so host scripts
   receive sleep start/stop; engine sleep callbacks use that host-player context. Vanilla listener/player
   effects are recorded in the design doc. Fast tests, native/UI/DLL checks and both lossy profiles pass;
   follow-up rest scenarios pass 3/3 shuffled (18/18), including the host's vanilla sleep-script bonus.
   Calendar/world/script-context regressions pass; physics regression limitations are recorded in the
   design doc. Duo stopped and freed; changes remain uncommitted in `.worktrees/party-rest-events`.
5. **Physics:** objects and ragdolls from the host, live (`docs/design/physics-sync.md`): steps 1-6 built 2026-10-24.
   Owners built 2026-10-26 (uncommitted, `.worktrees/physics-owners`): every game streams the bodies it simulates
   (one `IBodyBridge`, `BodyService` in every game), a guest's player's ragdoll reaches the others, and living
   knockdowns knock the copies down with their owners. A dead body's pose at a cell load is parked in
   `docs/ideas.md` (it drops in place on the guest, lootable).
6. **Dropped items** (created references), with their extra data; items in chests keep theirs too. This is how
   players give each other items in 1.0 (a trade menu comes later). Built 2026-10-26, merged into dev after a
   review (uncommitted; [dropped-items.md](design/dropped-items.md)): a guest's drop is made by the host's game
   and the dropper's own reference becomes its twin (its fall its own until the host's rest); a guest's pickup asks
   the host, the first request wins and the item is gone for everyone; the host's own drops and pickups are its
   world's. The item's enchantment (player-made too), charge, tempering, poison, soul and custom name travel
   (`Inventory::Entry`, name added), and containers' changes carry their stacks with extra data. A guest's drop where
   the host's game has no cell loaded waits for it. Tiers 1-2 pass (`[Items]`, `[loose]`, both lossy profiles);
   scenarios `host-drop-reaches-guest`, `guest-drop-reaches-host`, `guest-pickup-race`, `chest-keeps-tempered-item`
   3/3 shuffled. Skyrim found two engine gaps, fixed: a deleted reference was still picked up (both players got the
   item), and a stack with extra data added with a count needs its `ExtraCount` (else one plain). The review fixed a
   player-made enchantment comparing unequal across games (chests rebuilt such stacks on every sync), a drop the
   host's game cannot make (now told, stays the dropper's), and a pickup in the frame its drop was made.
7. **Followers:** on dev, uncommitted ([followers.md](design/followers.md), 2026-10-27): shared vanilla slots; recruiter/latest Follow picker controls native pathing; death/disconnect fallback and return; host save bindings and picker inventory. V1 keeps vanilla Wait → Follow. Native acceptance and remaining travel/favor/combat checks are recorded in the design.
8. **Vampirism:** each player's own, the infection and the hunger (the user, 2026-10-28). Built 2026-10-28
   (`.worktrees/party-scripts-2`, uncommitted) on per-player system quests, [system-quests.md](design/system-quests.md):
   a call from a player's own script into a quest's function makes the quest one per player on every game (told
   through the host, kept in the save's co-save); a call into one runs on the instance of the player behind it, the
   story's on every game (the cure cures every vampire); its stage and its scripts' globals stay out of the world sync;
   a guest's instance is kept in its record. Hooked at the call itself (`ScriptFunction::Call`, 105466). It replaced
   the shared vampirism built earlier the same day ([vampirism.md](design/vampirism.md)). Tiers 1-2 pass. Skyrim
   `vampirism-is-each-players`: see the run in the session's report.
9. **Cutscenes, step 2:** each player's own character in a scene. Built 2026-10-27 (`.worktrees/party-scripts-2`,
   uncommitted): a Body row in the router's engine table (moves, idles, AI drive, read from code, a queued functor's
   vtable too), so a story's call on the player's body runs on each game's own player and a guest's moment on its
   own game; a mirrored move leads no party travel (one check in party travel's `HookProcessQueuedTravel`); each
   guest's player plays the host's player's part in an engine scene (`GuestScene`). Tiers 1-2 pass; Skyrim scenarios
   from late saves: [cutscenes.md](design/cutscenes.md), "Step 2". `Debug.SendAnimationEvent` not read yet
   (`docs/bugs.md`).
10. **Mounts:** ready for v1.0.0 testing (user accepted 2026-10-27); available for review on dev (uncommitted): host/guest riding, exterior border crossing, dismount and disconnect; a following rider's horse accompanies it and native travel's implicit dismount returns ownership at the horse's latest position. [Party travel](design/party-travel.md).
11. **The party is the Dragonborn** ([dragonborn.md](design/dragonborn.md); built 2026-10-27 with feature 3,
    merged into dev uncommitted): a word of power a script teaches or unlocks every player has; every soul
    gained reaches each player's own count (souls are each player's to spend: a Magic menu unlock is the spender's);
    the story's factions and spells on a player (titles, the Companions' blood, marriage's factions) reach every
    player, a player's own moment (its beast form's quest) stays theirs; an NPC's relationship with a guest's
    character is the player's (0x7), as ownership. Rebuilt the same day as the party's calls (dragonborn.md, "The
    party's calls"): the story's call of a native changing a player's progression (a row of `PartyRules::ProgressOf`)
    is relayed as the call and runs on every game's own player (`PartyNatives::Scope`, `RouteOf`), replacing the
    per-kind hooks, `PartyChange` and its apply; feature 3's rewards are more rows. A rejoining character keeps its own souls and soul-bought words
    (`PlayerRecord::Words`, value 133), and the save's shouts come along. A dragon's death is the world's event:
    its dissolve and the absorb's streams show on every game, the streams on each game's own player (`Call::kShow`,
    `LocalForm::EachPlayer`). Relayed calls now pass objects as their parameters' types: the VM had dropped every
    mirrored effect on an actor (`GlobalCall`). Tiers 1-2 and both lossy profiles pass; Skyrim: the dragonborn and
    reward scenarios and their regressions, 21/21 after the rebuild.
12. **Script rules 3 to 5** (`party-model.md`): the host's messages (notifications, message boxes) on every game;
    position questions without a cause ask about the party; a guest's story events (locations, crafts, kills) raised
    on the host for its copy. Rules 5 and 3 built 2026-10-27, merged into dev uncommitted. First the party's
    calls became one router ([party-calls.md](design/party-calls.md)): every native's call is routed by one decision
    (`PartyRules::RouteOf`) from what its code reaches, read once with Zydis, its virtual calls resolved against the
    player's vtables; the progression name list became engine functions with the party's policy flag, and three relay
    messages one (`RoutedCall`). Behaviour unchanged: 24 of 24 regression scenarios. Rule 5 built: the story's messages
    on every game, a player's own on its game, a story's box answered by the host with the others' copies closing
    (`story-message-reaches-party`, `story-box-host-answers`; a guest's lever's box is the guest's now:
    `relief-takes-client-relics`). Rule 3 built: a quest's question about the player asks the party (yes/no: any
    player; else the player nearest what it names; `timer-asks-party-position`). Rule 4 built 2026-10-27
    (party-calls.md, "A player's story events"): every story event a guest's game raises (any type: arrivals, crafts,
    skills, levels, script events) is sent to the host and raised there for its character, its members read from the
    engine's type table (generic since 2026-10-28, `.worktrees/story-events`; one naming another game's actor, a kill
    or an assault, is the host's own), and the story manager's visitors judge the guest's character while one of its
    events runs (a copy's kill too); a party arriving together is one arrival. HostAuthorityTests, ClientTests, GameTests; Skyrim
    `guest-story-starts-no-quest`, `guest-location-change-starts-story`, `guest-craft-raises-story` pass. A copy can
    stay getting up after its guest leaves the crafting menu (`docs/bugs.md`).
13. **Party travel gaps:** ready for v1.0.0 testing (user accepted 2026-10-27); available for review on dev (uncommitted): guest fast-travel time advances HostClock; native guest MoveTo leads the party; queued destinations are announced before loading. Menu, dialogue, dead/mounted follower and three-player coverage added. Full gate and both scoped non-death travel/mount network profiles pass; remaining jail cleanup, intermittent lock reports and death/physics failures under packet loss are deferred known issues in `docs/bugs.md`. Native measurements, validation and remaining limits: [party-travel.md](design/party-travel.md).
14. **Small visible bugs** (built 2026-10-27, merged into dev uncommitted): the same resurrected NPC
    copy draws and attacks again; a brought character gets checked placement beside the host; a copy's base level
    follows its owner; instant FX Persist effects flash; looks update the existing actor and base. Fast tests were
    made to fail first. All nine affected Skyrim scenarios pass shuffled, NPC resurrection also passes three
    consecutive runs, and player recovery passes alone after one wolf-target staging failure. Both lossy network
    profiles pass 15 affected flows. Validation: [small-bugs.md](design/small-bugs.md). The final gate's only failure
    is the plate-carry physics case, also reproduced on unchanged dev `241bb0f2` under the same shard order (75 units
    behind, about 20 allowed); source and logs are retained in the worktree's `build/baseline-dev/`.
15. **Menus that pause the game** (merged into dev 2026-10-27; [pausing-menus.md](design/pausing-menus.md)):
    decided with the user that personal menus keep the world running.
    The host's map, journal, crafting, lockpicking, barter, books and training leave its world and guests' NPCs live;
    a guest's menus leave its own simulated player live too. Map/local-view rendering and first-person return are
    handled, books render unpaused, and journal saves run between frames on the main thread. Shared rest/loads keep
    their pauses; offline menus stay native. Three unit cases, all 18 native menu scenarios shuffled, three marker
    regressions, shared rest, host loads, guest load refusals and dialogue training pass; plugin/UI checks pass.
    All 132 other GameTests pass; the gate's existing plate-carry physics failure also reproduces on unchanged dev.
16. **The host's loads take the party along** (built 2026-10-26; [host-loads.md](design/host-loads.md)): host
    quickload, menu and console loads send the chosen save to every guest through the wipe's reload and rebuild
    the host's rules on the same listener. Each guest rejoins as its saved character at its saved place. A guest's
    own loads are refused in chat, and a refused menu load releases its journal and loading fade. Quickload
    bypasses `Load_Impl` (35728): it queues `0xD0000400` through 35769; its exact quicksave comes from the engine's
    per-character `SaveEntry` (35789). GameTests cover three guests, an older chosen save and repeated loads;
    Skyrim scenarios cover quickload, the Load menu, console loading, guest refusals and the shared wipe.
17. **The party's purse** (decided and built 2026-10-27, merged into dev uncommitted;
    [party-purse.md](design/party-purse.md)): every player carries one purse, the host's player's gold. Any
    player's gold change (loot, trade, training, a fine, a reward on a guest's copy) reaches the host as before
    (`RequestInventoryChanges`), which keeps it in its player's gold (`HostPurse`) and sends the purse to every game
    (`NotifyPartyGold`); each game sets its player and its copies of players silently, a copy whose spawn items land
    later too. A joining guest carries the purse, not what it brought. Items and quest item rewards stay each
    player's. Tiers 1-2 (`purse_tests.cpp`, `purse_flows.cpp`, join and records flows) and both lossy profiles for
    `[purse],[items],[records],[crime]` pass; the host-load flows fail under the lossy profiles on dev as well.
    Skyrim: `party-gold-is-one` and the gold scenarios that assumed each player's own (five dialogue,
    `chest-take-all`, `host-save-keeps-guest-character`, `late-join-client`, the crime fines) pass; the crime
    scenarios' witness and arrest-travel steps fail now and then, before any gold, on a build without the purse too.
17. **Friendly fire, always on** (`docs/design/friendly-fire.md`; built 2026-10-26, uncommitted, `.worktrees/friendly-fire`):
    players hurt, knock down and kill each other as NPCs would. A player's weapon hit on another player's copy is a
    request to that player's game (`HitRules`, the damage hook's rule), and hostile spells and shouts reach other
    players (`IsBlockedPvp` deleted). The victim's game applies a spell's damage to its own player, and a shout's
    push knocks it down (its ragdoll streams). A teammate's kill is a real death (spectate, respawn), and every chat
    names the killer ("was killed by"). Decided with the user: no XP or kill counts, not a crime, and nobody turns
    on the attacker (the victim's game runs no NPC reaction for its player). Kill moves on players work. Found on the
    way and fixed: a player revived after a kill-move death floated (its ragdoll left in the physics world;
    `death.md`, "The ragdoll"), and spectating could drop into first person (third person only now). Harness:
    camera, body pose and ragdoll motion readouts, `coop_test.py look` (`testing.md`). Open: melee stagger is not sent
    (as for host NPCs); a copy's ragdoll (any actor's) no longer fights its owner's frames (steered), and the engine keeps a lying
    copy on it as its owner (its spins and snaps fixed; `physics-sync.md`, "A copy's ragdoll, steered"); knock-start
    pops fixed (never placed where its owner lies, eased off its ragdoll). Left for after 1.0 (`docs/bugs.md`): a
    copy getting up on its own timing, a little shaking while it lies, and a revived player knocked down stays getting up.

18. **Dialogue judged for each player** (built 2026-10-28, `.worktrees/party-scripts-2`, uncommitted;
    [shared-dialogue.md](design/shared-dialogue.md), "The rule"): the host's engine judges every topic of a step for
    each player's character too (`JudgedFor`: conditions on PlayerRef, a PlayerRef parameter, `GetPC*`, intimidate and
    bribe rewritten for the character), and each game's menu shows its own list; the host's game says the picker's
    line. Copies now carry their player's skills from the spawn, and its added spells with its perks (GameTests
    `perk_flows.cpp`; `HasSpell` reads them through `Actor::VisitSpells` 38781). Tiers 1-2 pass; Skyrim `dialogue-line-judged-for-guest`
    (fails as expected with the hook off), `dialogue-options-are-each-players` (Lucan's claw), and the dialogue tag.
    Known gap: a guest with an empty own list leaves. Harness fixes found on the way: the after-scenario body check
    read a first-person player's first-person model (`dialogue-refused-start`, `-picker-gets-speech-experience`
    failed on it, dev too), and a spawned NPC recruited by a scenario's "Follow me" pick stayed in the follower slot
    once deleted (every `follower-*` fixture after it failed): `remove_host_npc` dismisses it first.

19. **Crafted potions and poisons** (built 2026-10-28, `.worktrees/crafted-potions`, uncommitted;
    [crafted-potions.md](design/crafted-potions.md)): a player-made potion or poison is named in the session by its
    recipe (one id for the same effects on every game), each game making its own form from the recipe
    (`AddPotion`/`AddPoison`); the recipes reach each game before the first message naming one (`TransportService` and
    `HostNetwork` send the book's new entries first) and are kept in the character record. Brewed potions reach the
    brewer's copies, chests, drops and the host's pickup; a coated weapon keeps its poison. Found on the way: the alchemy
    menu adds its potion with `TESObjectREFR::sub_19688`, which no hook saw (no copy got a brewed potion); hooked for
    actors. Tiers 1-2 (ClientTests, HostAuthorityTests, TPTests, GameTests `crafted_potion_flows.cpp`); Skyrim
    `guest-brewed-potion-reaches-host`, `guest-poison-coating-reaches-host` and ten item regressions pass;
    `crafted-potion-kept-across-rejoin` passes its potion steps and fails the known cover-plate difference after a rejoin.
20. **Adaptive copy playback** (built 2026-10-28 on dev, uncommitted; [copy-clock.md](design/copy-clock.md)): copies
    trail their owner by what the line needs instead of a fixed 300 ms (one game-wide clock steered toward the worst
    lateness of the last 3 s, plus each copy's own send interval), movement, actions and ragdoll on that one time;
    late snapshots carry a copy on for up to one interval. Players, mounts and fights are sent 30/s, other actors 10/s.
    Tiers 1-2 (ClientTests `copy_clock_tests.cpp`, GameTests `copy_playback_flows.cpp`); Skyrim: 16 of 19
    movement/combat/knockdown/death/travel scenarios pass, the other three known and in `docs/bugs.md`; the guest's copy
    of the host's player shows about 140 ms behind (was 300).
21. **v1 checks: permanent abilities and live menus** (2026-10-28, dev uncommitted). An ability's values reach a rejoined
    player's copy (the spawn carries values held off their base; non-pool values are set as the base, not capped by
    damage). Live chest and barter menus close when their target goes, dies or leaves (`MenuAutoClose`, Skyrim Souls'
    rule), and an open chest's list follows another game's change (`SendInventoryUpdateMessage`). ClientTests
    `menu_auto_close_tests.cpp`, GameTests `perk_flows.cpp` (rejoin); Skyrim `ability-stays-on-copy` and the
    `coop_menus.py` live-menu checks pass; a chest lid's pose after an interrupted open is in `docs/bugs.md`.

Not in 1.0: a player list with kick and a settings screen (players join by Steam invite), the trade menu, other
players on the world map and compass (the tether keeps the party close), mod support (RaceMenu adapter, animation
mods, mod sync).

## Product status and remaining delivery work

The earlier "Product pieces not started" list was obsolete. Steam lobbies/invites and the P2P transport,
game/build/plugin-order checks, main-menu returning-character joins, host save transfer and character co-save
records are implemented. The join UI shows progress/refusals/end reasons; ordinary leave, kick, host exit and
exhausted reconnects now clean up and return to the main menu. Difficulty follows the host's actual setting.
See `docs/design/join-flow.md`, `Code/client/Ui/README.md` and the integration review above for evidence and limits.

Animation action snapshots now copy and replace their owned strings/vectors under one mutex; refused death
actions preserve newer actions and body resets. Fast concurrency/restoration tests and native checks pass.
The original reload corruption remains open (v1); the historical werewolf crash is out of v1 scope (2026-10-29,
`docs/bugs.md`). See the [stability report](v1-session-items-2026-10-28.md#concurrent-animation-snapshots).

- **V1 delivery:** a reproducible `test_harness=n` player package with UI/assets/dependencies, Quick Start - SE,
  clean-install validation and current setup instructions (with `docs/playtest.md`: `/bug`, `/export`). The README
  still needs its deleted-server/launcher description cleaned up.
- **V1 first-time join (planned 2026-10-29):** a friend the host's save does not know accepts the invite at the main
  menu, a new game starts by itself, Quick Start skips the intro, and naming the character joins the host with it
  ([join-flow.md](design/join-flow.md#first-time-join-planned-2026-10-29-v1)). Today such a friend is refused with
  "start or load a game of your own". The vanilla intro is out of v1 (`docs/ideas.md`).
- **V1 acceptance:** the agreed upstream fixes/features and library checks in [v1-playtest-todo.md](v1-playtest-todo.md),
  the unresolved host-reload freeze with its save/load gate (save three ways, load three ways, then keep playing
  minutes while checking frames, crashes, copies, follower and items; the current load scenarios stop at positions
  matching), and ordinary packaged-build Steam, quest/save/rejoin and 3–4-player runs.
- **Later scope:** automatic mod/file syncing or self-updating, a launcher if needed for those features,
  player-list/kick/settings screens and the other items explicitly outside 1.0 above. V1 entry uses Steam invites
  and Skyrim's own save/character flow; no additional Host Game or character-library screen is required.

## Infrastructure (done)

- Linux cross-build with clang-cl (`4571bcbe`), one-file rebuilds in ~3 s (`71858be5`).
- Two-game scenarios off-screen, through real input (`088d78da`); harness out of player builds (`ea087fb2`).
- Hot reload into running games (`d09fdd1f`).
- Crash and hang tooling: CrashLogger reports, breadcrumbs, heap history, probes, repro, vanilla
  harness (`f4ff0467`); lock-order checker (`261be264`); desync checker (`8e14e17d`).
- RE references: `docs/re/ENGINE_REFERENCE.md`, CommonLibSSE-NG and SKSE plugin sources, IDA database.
