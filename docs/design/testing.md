# Testing: four tiers

Decided 2026-10-23 with the user. The model is Factorio's (Friday Facts #60 and #62,
factorio.com/blog/post/fff-60, fff-62): unit tests of isolated logic, integration tests of the real game on small
maps checked against saved checksums, and black-box tests of the binary with several instances for multiplayer.
They test the trickiest code first, not everything, and credit the tests with the desyncs that would otherwise
have held up their multiplayer release.

We can't boot Skyrim headless as Factorio boots its engine, so our "small map" is a fake engine behind the bridge
interfaces. Skyrim itself is the black box.

```
                         speed       runs                  catches
 1 unit         Catch2   < 1 s run   every change          logic: rules, holds, tags, encodings
 2 integration  Catch2   seconds     every change          flows between games: messages, order, leaving
 3 black box    duo      minutes     while building;       how Skyrim behaves; glue; the fake's honesty
                                     affected before push
 4 soak         duo      hours       before a release      crashes, deadlocks, order bugs
```

## The gate

```bash
scripts/check.sh            # tiers 1-2 and the plugin's build; before every push (the pre-push hook)
scripts/check.sh --no-dll   # without the Windows toolchain
```

It builds and runs `ClientTests`, `BridgeTests`, `TPTests` and `GameTests`, then builds the plugin, one target per
build at `-j8`. Each step's output is in `build/check/`, and a failure shows its failed checks or compiler errors.
`GameTests` runs in 8 shards at once (`build/check/GameTests/`), each with its own ports (`GAMETESTS_PORT`): its
games wait on real timers, not the CPU. The step fails if any case did not run (Catch2 reads a comma in a name as
"or"; `check.sh` escapes them). Incremental it takes 4-12 s. The hook is `scripts/hooks/pre-push`, installed with
`git config core.hooksPath scripts/hooks` (relative, so worktrees share it); `git push --no-verify` skips it.
Skyrim's scenarios are not in it.

## Today's numbers (measured 2026-10-23)

- DLL, one `.cpp` touched (`PartyNatives.cpp`, `CutsceneService.cpp`, `World.cpp`): 1.8-2.4 s
- DLL, `World.h` touched (most of the plugin): 12.9 s
- `ClientTests`: rule edit, build and run 0.4 s; test edit, build and run 1.1 s
- `BridgeTests`: build 2.4-3.8 s (one rule or test file); run 0.04 s (118 cases)
- `TPTests`: build 1.9 s; run under 0.01 s
- `GameTests` (tier 2): build 2.5-4.5 s (one service), 14.5 s from clean; run 14 s (28 flows and spec runs, 2-3 games each;
  8 s with the duo running), 0.9 s of it each death flow (the services' real-time timers); 10.2 s for 24 flows
  with the outdoor respawn, on a loaded machine (load 9)
- Duo boot (`stop`, then `duo --keep-running`): 22.6 s, both games connected and the baseline recorded
- `reload` (swap a rebuild into the running duo): 2.2 s
- One scenario (`cutscene-controls-reach-party`): 1.6 s, 2.6 s with the command's start
- The `scripts` tag (24 scenarios): 176 s
- The full suite: 132 scenarios, over 10 minutes (not run for this)

So one Skyrim loop (build, reload, the affected tag) costs minutes; tiers 1-2 cost a few seconds.

A catch: xmake compares timestamps by the second, so an edit within a second of the last build is not rebuilt.
Scripts that edit and build in a loop wait a second first.

## Tier 1: unit

- **For:** pure client and host logic, no engine: whose a call is, who holds what, the host's rules, message
  encoding.
- **Where:**
  - `Code/tests/client/` (`ClientTests`): every game's logic. The pure files are listed one by one in its
    `xmake.lua` (`PartyRules`, `ScriptTags`, `OwnEvents`, and `Games/EngineNames.h`); a file that needs the engine
    doesn't build there, so the Linux build is the purity check.
  - `Code/tests/bridge/` (`BridgeTests`): the host's rules (`HostAuthority`, `Services/Host/`) against in-process
    games (`session.h`).
  - `Code/tests/encoding.cpp` (`TPTests`): messages on the wire.
- **Run:**
  ```bash
  scripts/xmake-linux.sh build -j8 ClientTests && build/linux/x86_64/debug/ClientTests
  scripts/xmake-linux.sh build -j8 BridgeTests && build/linux/x86_64/debug/BridgeTests
  scripts/xmake-linux.sh build -j8 TPTests && build/linux/x86_64/debug/TPTests
  ```
- **When:** every change. **Budget:** about 10 s build and run, each target.
- **Shape:** a hook's file splits in two:
  ```
  Papyrus/PartyRules.{h,cpp}    pure: inputs -> decision; no engine headers
  Papyrus/PartyNatives.cpp      glue: the hook gathers inputs, calls the rule, acts
  ```
  Engine constants the rule needs are mirrored in the pure header and `static_assert`ed against the engine struct
  in the glue (`PartyRules::kMovement == BSInputEnableManager::kMovement`).

## Tier 2: integration

- **For:** whole games in-process (the real client services and the host's rules) against a fake engine, 2-4
  games, given/when/then flows. Two checksums: `Party::Differences()` (every reference in a cell two games have
  loaded is the same) and `Party::Agree()` (the games agree on every actor, below).
- **Where:** `Code/tests/games/` (`GameTests`).
- **Run:** `scripts/xmake-linux.sh build -j8 GameTests && build/linux/x86_64/debug/GameTests`
- **When:** every change. **Budget:** seconds. Today: build 2.5-4.5 s (one service), run 7.7-10.2 s for 23-24 flows.
  Most of the run is enet's loopback handshakes, `Net::Client::Close`'s 100 ms wait per leave, and the death
  flows' waits on `ActorValueService`'s timers (250 ms death state, 500 ms before a copy dies).

```
one process
  Party (Party.h)
    HostNetwork + HostAuthority (every rule in Services/Host/), HostOf: the host's reads from its fake world
    World "Dovahkiin" (the host's game)    TransportService::ConnectLocal (in-process, as in Skyrim)
    World "Lydia" (a guest)                TransportService::Connect 127.0.0.1 (enet, as from another machine)
  each World (Seams.cpp): the services a flow needs, real
    TransportService, CutsceneService, RunnerService, CharacterService, TravelService, ActorValueService,
    PlayerService, ModSystem, HostService
    (HostSeams.cpp: hosting as in Skyrim, its reads from the fake world and CharacterService);
    on a guest GuestWorldState (GuestSeams.cpp, as it joins)
    SkyrimBridge = FakeEngine: StillBridges (every bridge call answers nothing) + ControlMap's mask + a script VM
                   + the world's references (locks, containers' items, change records, loaded cells)
                   + the actors (the player 0x14, placed and placeatme NPCs, copies; temporary ids reused;
                     where, health, death, bleedout, enabled; respawn at a cell's COC marker)
  World::Get() is the game being stepped (Party::As)
```

Real code in it: `TransportService`, `CutsceneService`, `RunnerService`, `CharacterService`, `TravelService`,
`ActorValueService`, `PlayerService`, `InterpolationSystem`, `ModSystem`, `GuestWorldState`, the script value conversions (`ScriptValues.cpp`) and
session ids (`SessionIds.cpp`), `HostNetwork`, `HostAuthority` and its rules (`HostWorldState` included),
`PartyRules`, the encoding and the network. Fake (link seams, no production interface widened): `World`'s
constructor (the services above, in the real order, destroyed first), `SkyrimBridge`, `RunningGame`,
`PartyNatives`' entry points (the glue's job, mirrored in about 40 lines over `PartyRules`), `GuestEngine`'s calls
`GuestWorldState` makes, `GuestService`'s making of the guest's parts, `HostService` (its hosting; its own
destructor needs `HostEngine`'s and `HostConversation`'s, never made here), and what `CharacterService` calls that
tier 2 leaves out: `AnimationSystem` / `FaceGenSystem` (their `Setup`s mirrored: components only),
`AiStateService::ReadState`, `EffectVisualsService::ReadEffects` (not in these worlds).

The flows (`cutscene_flows.cpp`): a guest's world script takes everyone's controls and the guest leaves, so the
host's come back; the host's world script, so the guest gets its own back as it leaves; two cutscenes at once; a
restriction stays with its game. With `CutsceneService::OnPlayerLeft`'s give-back removed, the first fails:
"never happened: the host's controls come back".

The world flows (`world_flows.cpp`): the host's player picks a lock and it opens on the guest's game; a guest's
pick is a proposal the host's world takes and sends back as its own; a guest's world locking a door is no
proposal; a guest takes gold and the host's chest holds what the guest's does; a cell a guest never loaded gets
the host's changes there when it loads it; a guest away while the host changes things gets them on joining again;
two guests, one's proposal reaching the other. Each ends with the checksum. With `GuestWorldState`'s `ByPlayer`
check removed, the third fails; with its cell-state ask removed, the last-but-one two fail.

The copy flows (`copy_flows.cpp`): an NPC the host's game loads gets a copy on the guest's game; it goes when the
host's NPC goes, before the host's game has its assignment back and after; a guest's player gets a copy on the
host's game, which goes when the guest leaves. The first run found a bug: the host's rules send the spawn with
the assignment, so a guest has its copy a frame before the host's game handles its answer; an NPC unloading in
that frame was dropped by `OnAssignCharacter` without telling the host, and the guest's copy stayed for good.
Fixed (`CharacterService::OnAssignCharacter` sends `RequestRemoveCharacter` for it); without the fix the race case
fails.

The actor checksum, `Party::Agree()` (`ActorDifferences`): every actor a game owns in the session (its
`LocalComponent`) has exactly one copy on every other game, flagged a copy, in the owner's cell within 64 units
(`FollowOwner` leaves a copy that near where it is), dead or alive and enabled or disabled as the owner's actor; a
guest's reference held for a host NPC that went (`HeldCopyComponent`) matches the host's reference the same way;
no other actor anywhere is flagged a copy. It waits for agreement (copies trail their owner by the interpolation's
300 ms) and fails listing what differs. The copy, travel and death flows end with it.

The flows added with it (2026-10-23), each checked by breaking the line it covers (rebuilt, the flow fails,
restored):

- `copy_flows.cpp`, "the host's NPC goes away": a quest moves the host's placed Vilkas to an unloaded cell and the
  guest's Vilkas follows there, held as a copy (without `FollowOwner`'s move, its hold, or `ReadWhereabouts`, it
  fails); he comes back and is the host's copy again (without the spawn dropping the hold); he is disabled and so is
  the guest's (without `DisableActor`); a placeatme guard walks out and its copy is deleted (without
  `OnRemoveCharacter`'s delete).
- `copy_flows.cpp`, late join: the host's game in the sinkhole with Aela and a dead guard, the guest's in QASmoke;
  the guest joins, is brought to the party and gets every copy, the guard dead. It failed first: nothing brought a
  guest joining elsewhere (the bug below). Without the pull, or the host's actors sent on its arrival, it fails.
- `death_flows.cpp`: the host's player kills a guard; the copy shows its health at -100 and stays alive (deferred
  kill), then dies with the owner's death state (without the relay, or `ApplyPendingDeathStates`' kill, it fails).
  A guest's player takes a deadly hit, goes down (the session's death system makes it essential), gets up at
  QASmoke's COC marker after `PlayerService`'s 5 s, and the host spawns its copy again (without
  `PlayerRespawnRequest`, or the copy's `RequestRespawn`, it fails). Tier 2 has no animation replay, so the copy
  never goes down itself; the check is that it is a new actor (`FakeEngine::Actor::Made`, a fresh extension in
  Skyrim), as the respawn is what ends a copy's replayed bleedout there. Outdoors (the fake's exteriors: a
  worldspace's cells on the 4096-unit grid, the 5 by 5 grid loaded around the player's cell, walking without a
  teleport), a guest's player gets up in its own cell and the host stays put, next to it and three cells away; the
  second failed first: the respawn's move led the party (bug 5). Without `PlayerService`'s `OnRespawnMove`, or
  `TravelService`'s `kRespawn`, it fails.
- `travel_flows.cpp`: a placed NPC stays with its cell, held as a copy, when the host leads away, and when the guest
  leads (its arrival drops its copies first; without `CancelServerAssignment`'s hold it fails), and is the host's
  copy again on the party's return; an NPC the host's game loads in the frame a following guest's arrival reaches
  the host's rules (stepped one game at a time, `Party::Step`) reaches the guest. Without `SetActorPersistent` on
  players' copies the host-travels flow fails (the fake deletes a created actor with its cell).

Bugs they found, each fixed with the flow failing without the fix (and a `BridgeTests` case for the host's rules):

1. **A guest joining away from the host stayed there** (the known gap "neither pulled nor pulls"):
   `HostCharacters::OnAssignCharacter` now sends a player whose game announces itself elsewhere a
   `NotifyPartyTravel` to the host's player (`BringToParty`), and the tether does the rest.
2. **An NPC the host's game announced as a following guest arrived never reached it.** The announcement skips a
   player not with the host; its arrival sends the host's actors from the host's game, which handles its answer
   only on its next update, so one announced in that frame was in neither. Now a host actor its game has not moved
   yet keeps the players that arrived meanwhile, and its first movement sends them its spawn.
3. **A guest that led got spawns of what the host left behind.** The host's arrival sends the players ahead of it
   the host's actors in the same frame, before discovery drops the NPCs of the cell it left; a placed NPC's spawn
   re-flagged the guest's held reference, a placeatme NPC's made a copy in the guest's cell at the old cell's
   position. `BuildSpawnRequests` skips an NPC with no 3D now.
4. **Statics shared by the two games:** `ActorValueService`'s four send timers (the guest's sends took the host's
   turn, so the host's NPC's health and death never went out) and `PlayerService`'s respawn flag (the host, not
   down, cleared it every frame, so a guest's respawn never came). Members now.
5. **A respawn outdoors pulled the party** to the respawned player when they were out of its loaded grid:
   upstream's `RespawnPlayer` moves the player, and the arrival led. `PlayerService` tells `TravelService` before
   it (`OnRespawnMove`), and the arrival is sent as `EnterCellRequest::Travel::kRespawn`, which leads nobody.
6. **A guest joining outdoors four cells from the host stayed there** (found in Skyrim, 2026-10-23): outdoors
   the place is the worldspace, so it counted as with the host. A joiner out of the host's player's loaded grid
   is sent to it too now (`HostCharacters::InHostGrid`; flow "A guest joins outdoors four cells from the host",
   `BridgeTests` "a player joining outdoors out of the host's grid").
7. **A guest brought to the party at its join had no copy of the host's player, nor the host of it** (found in
   Skyrim, 2026-10-23, `guest-joins-elsewhere` and the four-cell join). The copies made at the join were lost on
   the way (the host's, made four cells off before it was persistent, deleted that frame; the guest's dropped as
   its travel began), and an arrival sends the host's actors only, players' copies being taken to travel with the
   party. Its arrival now sends the players' characters both ways (`HostCharacters::SendPlayersMet`; a game that
   still has the copy refreshes it). The fake learned the loss first: a created actor moved into a cell not
   loaded is deleted, which made the flow and the spec fail as in Skyrim. `BridgeTests` "a player brought to the
   party gets the players' characters again". Skyrim then showed the host still without the guest's copy: the
   guest's "where I was as I joined" arrival (`kBeforeJoining`) comes after its assignment and, outdoors in the
   same worldspace, counts as with the host, so the resend went before the travel. Only the arrival by the
   party's travel (`kTether`) counts now (`BridgeTests` "a player brought to the party outdoors gets them where it
   arrives"; the fake's guest sends that arrival before its assignment, so tier 2 did not show the order).
8. **What a player got or equipped before its session id came back was lost** (found under network conditions,
   2026-10-23): `InventoryService` dropped a change for an actor with no session id yet ("failed to find session
   id"), and the host's spawn of it has its inventory as announced, so a copy never had it. Changes of an actor
   still waiting for its assignment are queued now and sent once it is ours (`InventoryService::RunPendingChanges`;
   flow "What a player does before its session id comes back", which steps only the guest's game so the answer
   waits).

The equipment flows (`equipment_flows.cpp`, `InventoryService` in every game, 2026-10-23): a player's weapons in
either hand, a swap and a sheathe, and armor reach its copy; a host NPC's weapon reaches its copy, which takes
no equip of its own; a late joiner's copy of the host's player comes dressed (the spawn's inventory). The actor
checksum compares what each copy wears with its owner's. Each fails with its line broken (the send, the apply,
the slot, the unequip, the armor's equip, the host's relay, the spawn's inventory). `InventoryService`'s two
timers were statics two games would share (the snapshot's and the naked check's); members now, as the rule says
(no flow shows it: the snapshot comes every 3 s, past `Until`'s 2).

The item flows (`item_flows.cpp`, the same day): a player's gold gained and spent reaches its copy and not the
other player; a dropped held sword leaves its copy's hands and inventory (one removal: the host relays a drop as
a count); a host NPC's gold reaches its copy; a guest joining later gets another guest's player with its gold
and sword (the host's copy, `CharacterService::ReadCopy`). The checksum compares what each copy carries. A
player's drawn weapon reaches its copy, and a joiner sees the host's player's hands up (`equipment_flows.cpp`).
The fake has no graphs, so each replayed action is refused (`World::Update` drains the copies' queues) and the
copy's hands come from `InventoryService`'s convergence, its fallback in Skyrim. Each fails with its line broken.

Clock, weather, markers and time scale (`clock_weather_flows.cpp`, `shared_flows.cpp`, 2026-10-23; `GuestClock`,
`GuestWeather`, `MapService`, `TimeScaleService` in every game): a joining guest takes the host's weather and
date, the host's change of weather and a wait reach it, leaving gives its weather back; a guest's map marker
shows on the host's and the other guest's map and goes with its removal; a kill cam on a guest's game slows the
host's clock and both run at full speed after. The fake's calendar runs on real time, as the engine's frame
delta does (run by frames, the host saw drift every frame and resent its clock). `GuestClock`'s
"has the host's clock" is per game now; the engine hook keeps its static.

Magic, effect visuals and AI state (`magic_flows.cpp`, 2026-10-23; `MagicService`, `EffectVisualsService`,
`AiStateService` in every game, each including `SessionIds.h` for `Utils.h`): a guest's Flames replays on its copy
from the same hand, and is not sent back; a guest's spell on a host NPC is applied by the host's game, cast by its
copy of the guest, and the NPC turns on it; a host NPC's burning plays on its copy and ends with it; a host NPC
fighting the guest's player fights the guest's own player on the guest's game, which counts it for its player's
combat. The fake has each actor's casts, effects, reactions, AI fields and hit visuals, as our hooks raise them
(`HookSpellCast`, `HookAddTarget`, `EffectEvents`, `AiEvents`).

Combat and objects (`combat_flows.cpp`, `object_flows.cpp`, the same day; `CombatService`, `ObjectService` in every
game): a guest's sword hit on a host NPC is applied by the host's game, the guest's copy taking nothing and
showing the guard's health bar, and its killing blow credits the host's copy of the guest; a guest's fire bolt
launches from its copy on the host's game from the same place; a guest's lever pull replays by its copy, a
locked gate's activation (a lockpick attempt) is not passed on. To build natively: `CombatService` takes its two
engine upkeep calls from `Games/ActorUpkeep.h` (out of `Actor.h`), `ProjectileLaunchedEvent` holds a `glm::vec3`
and `ActivateEvent` only the form ids it was read into (its engine pointers had no reader).

Dialogue (`dialogue_flows.cpp`, the same day; `DialogueService` in every game, `HostConversation` made by the
hosting seam as `HostService` makes it, `GuestDialogue` by the guest's): a guest talking to its copy of a host NPC
starts the host's game's conversation with the NPC in the background (the host plays on), the guest's menu opens
on its copy with the host's game's list, its pick is said by the host's game alone and the next list shows, and
its goodbye ends the conversation on both. `FakeDialogue.h` is the engine's side: a speaker, a menu, the list
built from a script of topics and our hooks' filters where the engine asks them.

The baseline's last four (the same day): a player's and a host NPC's health lost and regained show on their
copies (`values_flows.cpp`); a guest opening a door opens it on the host's game, and the host closing it reaches
the guest (`world_flows.cpp`; the fake's doors swing on activation and have the open-state row); the host's
globals reach a joining guest and follow its changes (`world_flows.cpp`; `HostGame::ReadGlobals` and
`GuestEngine::SetGlobal` on the fake's globals); the host kicks a guest, whose copy goes, and it joins again
(`session_flows.cpp`; `HostService::KickPlayer` in the hosting seam). The rest of `test-coverage.md`'s "none
yet" rows are details of covered features, left until after the MVP.

The travel flows (`travel_flows.cpp`): the host travels and the guest follows (the tether), the players' copies
coming along; a guest travels and the host and the other guest follow; an NPC stays with its cell when the party
leaves it, and one loading where the party is now gets a copy there. The check is that every game sees the party
in one cell: its own player and each player's copy. Without `RunPresence`, or without the tether's move, all three
fail. The fake engine's travel: the player's move loads its new cell and unloads the old one and reports a
teleport (`ConsumeTeleported`); an actor is in the process lists only while its cell is loaded. Found on the way:
`RunLocalUpdates` throttled snapshots with a function-local `static`, so with two games in one process only one
sent any; it is a member now (`m_lastSnapshot`). Statics in services are state the whole process shares.

To make `CharacterService`, `ModSystem` and the conversions build without the engine: `CharacterService` dropped two
includes it did not use (`MoveActorEvent.h`, `TESCamera.h`) and takes the session ids from `SessionIds.h`; the
session-id lookups and script value conversions moved out of `Utils.cpp` (`SessionIds.cpp`, `ScriptValues.cpp`;
a form's being in this game is `IFormBridge::FormExists`); `ModSystem` reads the loaded plugins from
`IFormBridge::GetLoadedMods` in place of `ModManager` (it takes the form bridge). Before that, for `CutsceneService`
and `GuestWorldState`, three header changes:
`Papyrus/PartyNatives.h` has no engine types (its `Scope` moved to `PartyNativesScope.h`), the script value
conversions moved out of `Utils.h` into `ScriptValues.h`, and `GuestEngine.h` no longer includes
`Papyrus/GlobalCall.h` (it names the callback's `std::function` itself). `ActorValueService` includes
`SessionIds.h` in place of `Utils.h` (the session-id lookups were all it used).

What the fake world assumes, from the engine (`docs/design/world-state-sync.md`): a cell's references are in
memory once it loaded and stay after it unloads, so a change to a cell a guest never loaded cannot apply there; a
guest's game raises its player's lock and container changes as events and applies the host's without raising
any; the host's game records each change it takes as its own.

What the fake's actors assume (each to check in Skyrim unless marked measured):

- An actor `Actor::Create` made (a copy) is deleted with its cell unless persistent, and its form id goes to the
  next reference made (measured: `docs/re/crashes/2026-10-14-reused-copy-form-id.md`). A placeatme NPC stays in its
  unloaded cell (measured 2026-10-23: the same form id and base, with 3D, once its cell loads again; not looked up
  while unloaded).
- A copy at or below 0 health neither dies nor goes down (measured: actor-ai-and-copies.md, "A copy is in deferred
  kill"); a non-copy dies at 0 health, or goes down if essential (`SetPlayerRespawnMode`). An essential base's
  NPC goes down at the console's kill too, owner and copy (measured: Lydia, `EssentialBases`); the specs kill a
  chicken.
- `PlayerCharacter::RespawnPlayer` in an interior ends the bleedout, restores health and moves the player to the
  cell's COC marker, a travel the engine reports (`ConsumeTeleported`), so a respawning guest's arrival leads
  (`kOwn`) and the others, in the same interior, stay (read from our `RespawnPlayer`; not measured).
- `DisableActor` takes 3D away (out of the process lists); `ApplyRemoteMovement` puts the actor at the position,
  even out of the loaded grid (measured 2026-10-23: a player's copy four cells off is kept, a copy, with no 3D and
  no cell, and is the same reference with 3D on return; the fake keeps its 3D). `MoveActorTo` puts a created
  actor that is not persistent into a cell not loaded out of the game (measured: a player's copy spawned four
  cells off, before `SetActorPersistent`, is gone that frame).
- An NPC placed in the plugins is in every game, its 3D loaded while its cell is. Its enable and disable reach a
  copy only through the copy messages: the world-state enabled row skips actors (measured 2026-10-23).
- Containers have no lids or menus: a lid's recorded activator (`chest-take-all`'s bug,
  `guest-world-events.md`) shows only in Skyrim.
- Outdoors the loaded grid is the 5 by 5 around the player's own cell (measured 2026-10-23: dimension 5, around
  the player's cell; the engine's lag in moving it not modelled), and an exterior cell's COC placement is its
  centre (wrong: measured off-centre; only respawn uses it, parked). `RespawnPlayer` outdoors goes to the COC point
  of the grid's centre cell (measured: its `MoveTo` input).

What the fake's equipment assumes (`FakeEngine::Equip`, from our hooks in `EquipManager.cpp`; not measured):

- An actor's own equip or unequip (the player's menu, an NPC's AI) raises `EquipmentChangeEvent` for an actor that
  is not a copy; a copy refuses one that is not ours (`ScopedEquipOverride`, which `EquipManager::Equip` sets).
- A hand holds one weapon, and an equip into it takes the old one off with no event (the hook's
  `ScopedUnequipOverride`). An armor takes off a worn armor sharing a biped slot, also with no event.
- Equipping an item the actor does not have adds one.
- Items: an actor's own gain or loss raises `InventoryChangeEvent` (the hooks on `AddInventoryItem`,
  `RemoveItem`), a drop raises one marked a drop; another game's change applies under `ScopedInventoryOverride`
  and raises none. A copy's 3D replays no action (no graphs): every queued action is refused.
- Ours (`ActorEquipItem`, `EquipManager::Equip` not applied at once) puts no armor over armor: measured
  2026-10-23, a player swapping iron armor for steel in the menu sends one request (the steel; the iron comes off
  inside the equip, no event), and its copy wears the steel and the helmet within 95 ms after
  `OnNotifyEquipmentChanges`' unequip-all, equip, re-equip. The mechanism (refused or applied in another order) is
  not known; the fake refuses.

### Network conditions

Every flow also runs with the guests' traffic over a bad line: `COOP_NET="delay=50,jitter=30,loss=5,seed=1"` puts a
UDP relay on loopback between each guest's enet client and the host's server (`UdpRelay.h`), holding each
datagram for the delay plus up to the jitter each way (so some arrive out of order) and dropping the loss's
share, from the seed; enet's own resends do the rest, as on the internet. The host's own game stays in-process.
`Party::Until` waits 20 s instead of 2 under it, and a failure names the conditions and the seed.
`scripts/net_check.sh` runs two profiles (50/30/5% and 120/80/10%), about 1.5 and 5 minutes for every flow: before
a push that touches messages or their order, not in the gate. First runs (2026-10-23): everything passed but one
flow, which found bug 8; one section needs loopback's timing (the guest's copy before the host's own answer) and
says so under `COOP_NET` (a warning, not a pass).

### What Skyrim taught the fake

The fake's `Game.DisablePlayerControls` takes what Skyrim takes, read with the harness's `screen` (2026-10-23):

- movement: `0x401` (movement and jumping)
- fighting `0x40`, looking `0x2`, sneaking `0x80`, menu `0x100` (`kMainFour`)
- camera switch, activate, the journal: nothing in `ControlMap`
- the defaults (movement, fighting, menu, activate): `0x541`

The first fake had movement as `0x1` only.

### Growing it: blockers, and the size of each

Today 13 of the 26 service files build natively (`ActorValueService`, `CharacterService`, `CommandService`,
`CutsceneService`, `GuestWorldState`, `MapService`, `PlayerService`, `RunnerService`, `SessionService`, `TimeScaleService`,
`TransportService`, `TravelService`, and `ModSystem` among the systems). What stopped the rest, first error each
(`CharacterService` was only three unused or misplaced includes):

1. **Engine structs in services** (`HostService`; `InventoryService`, `CombatService` and `ObjectService` were a
   header each, `MagicService` and the others below `Utils.h`, all in tier 2 now): `Games/Primitives.h` fails its own `static_assert` (`sizeof(NiRefObject)`), as
   it should: the engine's layout is the MSVC ABI's (`long` is 4 bytes there, 8 on Linux). They can never build
   natively; their engine calls go behind a bridge first. Large: one service at a time, as a flow needs it.
2. **`VersionDb.h` / `intrin.h` through the Papyrus and engine helpers** (`AiStateService`,
   `DialogueService`, `EffectVisualsService`, `PapyrusService`, every `Guest/` service, `GuestService`): Address
   Library pointers and intrinsics, reached through headers like `Utils.h` and `Papyrus/*.h`. Medium: often only
   a header that declares too much (as `PartyNatives.h` and `Utils.h` did); split the declarations a service uses
   from the engine's.
3. **The engine glue's entry points** (`GlobalCall`, `GuestScripts`, `PlayerNatives`): faked one function at a
   time in `Seams.cpp` / `FakeEngine.cpp`. Small each; keep each fake as thin as its glue.
4. **`HostService`'s hosting**: done as a seam (`HostSeams.cpp`). Splitting the real one (network, authority and
   `HostGame` from bridges, apart from `HostEngine`'s reads) would put its wiring under test too. Medium.
5. **The checksums:** `Party::Differences()` compares the fake worlds' references (locks, items), `Party::Agree()`
   their actors (cell, position, dead, enabled, copy flag). They grow with the fake: doors' open state, enabled,
   globals and quests next; an actor's values, bleedout and AI state once a flow needs them.
6. **One `World` per game in one process:** handled. `World::Get()` is the game being stepped; the fake engine's
   state is per game. Real glue keeps module statics (`PartyNatives`' holds), which is fine because the fake
   replaces the glue, but pure rules must be instance state (`ControlHolds` is).

## Tier 3: black box

- **For:** two real games, the harness and hot reload (`scripts/coop_test.py`, `Code/client/TestHarness/`).
- **When:**
  - building a new feature: fast, targeted runs to learn how Skyrim behaves, since we have no data on it yet.
    What's learned goes into the fake engine, and from then on tiers 1-2 gate the feature.
  - an existing feature: only the scenarios a change affects (`scenario --tag ...` or by name), before pushing.
- **Run:** `coop_test.py duo --keep-running` once, then `reload` and `scenario <names>` per change.
- **Budget:** a tag in a few minutes; the full suite never unasked.

`duo --keep-running` pins a repeatable fixture and records `home.json` for both games: placement and cell,
equipment, level, health/magicka/stamina maxima, and the host's world fixture (clear weather `0x81A`, noon, the
recorded date and time scale). Old home files acquire
these fields once without replacing their recorded player positions. `settle_baseline` applies the fixture once
through harness console/input controls and waits for the commanded state; it never retries or hot-reloads a
copy to make the check pass. Residual differences fail the scenario that left them and stop the run.

Only the host's weather and calendar are set; the guest follows through normal sync. Weather/calendar agreement
is checked **before** cleanup, so resetting to clear weather cannot erase a weather desync. Scenarios can still
force a different weather or advance time, and their checks run before the fixture returns. Noon prevents
ordinary clock advancement crossing midnight between short scenarios; the date is restored after explicit waits
and fast travel. The engine's `raw_days` remains cumulative and its sync is checked: existing harness controls
cannot rewind it, and setting `GameDaysPassed` alone is overwritten next frame. The per-process midnight counter
also remains cumulative and is not exposed by the harness. No save load or forced guest calendar conceals this.

QASmoke is an isolated interior; NPCs do not wander in from adjacent exterior cells. Reset removes owned test
NPCs, restores the recorded cell and player placement, maximum values and depleted pools, stops casts/combat and
releases held input. Baseline checks reject leftover NPCs or copies. Quests, script timers, effects and arbitrary
reference state remain each scenario's cleanup responsibility; a fixture does not rewind the whole world.

The shared specs in section (c) register automatically as `spec:<name>`, tagged `spec` plus their system tags, for
`scenario`, `affected` and `soak`. `Code/tests/specs/*` is the exception to `affected`'s test-directory exclusion:
changing shared data selects the `spec` tag, while changes to the fake's runner still stay in tiers 1-2. Run
`python3 scripts/coop_specs.py` or `coop_test.py scenario --tag spec` to check the same contracts in Skyrim.

The four cutscene twins and `lock-reaches-others` now run only as shared specs. Their old Python functions and
registry entries are retired; the two leaving/control specs retain the `session` tag as well as `scripts`.
`chest-take-reaches-others` is retired as a name, but its unique UI checks remain as `chest-take-all`: both players
use the container menu's Take All, items reach the acting player and its copy, and the other game's menu and
own player inventory stay unchanged. The chest spec covers count changes/proposals without driving that UI,
so it does not replace this input/ownership contract.

The retained UI check found a real distinction on pinned build `ff3bad3f7834`: when the host took the chest,
the guest's `ContainerMenu` opened (the guest-to-host direction did not). The named test failed in 7.8 s while
all six count/lock/cutscene specs passed again. This remains an activation/UI isolation handoff; the test keeps
the assertion and closes any container menu during cleanup so the failure does not contaminate later cases.

Validated 2026-10-23 on the running duo: `guest-spawns-no-critters` (6.7 s), `weather-follows-host` (14.2 s), the
25 hand-written `world` scenarios (241.3 s), and the 24 hand-written `scripts` scenarios (185.7 s), all passing.
The six shared specs then present also passed in Skyrim (21.5 s), including all four cutscene contracts and the
new lock/container contracts. Deliberate rain, date/hour, maximum-health and depleted-stamina drift reset in
1.16 s in one application of the fixture, with both games checked afterward.

A later full check pinned DLL `ff3bad3f7834` and verified each game's active generation by SHA256 (hot reload's
retained old mappings are not proof of the active build). Its captured ModSystem, Utils and CharacterService
sources match `0e6ade88`. All 41 spawn-tag cases passed within the full run; the full run passed 137/138 in
1100.9 s, or 1168.9 s including its fresh boot. `firebolt-client-hits-host-npc` failed the final checksum: the
owner's cached `JumpLand` fields differed from its copy's zero fields, after the spell/health checks had passed.
This remains C++ regression work for tiers 1-2, not a green full suite. Evidence and the exact source hashes are in
`build/coop-test/validation/pinned-ff3bad3f7834/`. Later ActorValue/Player/HostCharacters work in the shared tree
was not silently substituted into that run.

Follow-up builds on 2026-10-23 were captured with their source hashes and active DLL generations under
`build/coop-test/validation/`. Build `8ed928c99087` passed all 95 affected selections for HostCharacters,
CharacterService, ActorValueService and PlayerService, including the 41 spawn cases. Firebolt passed there;
the earlier failure is not thereby proved fixed. After the respawn arrival change, build `35bfae3b44ee` passed
78/79 additional affected selections: `party-panel` failed its checksum after hot reload, with a 54-degree
owner/copy heading difference. Both runs kept their build fixed; neither was a full suite run.

The actor vocabulary in `coop_specs.py` binds each named actor for that spec run, resolves a spawned copy by
session id after joining again, and reads held placed copies with `actor_place` even without 3D. Staged actors
are sent home and thawed; spawned actors are removed, including on a failed step. `deadly_hit` uses health
damage, and `down` reads bleedout with the spec's `within` deadline. `together` checks one cell and both players'
copies; it does not mean all of upstream's post-death motion has finished.

On `35bfae3b44ee`, `npc-disabled-copy-follows` and `npc-sent-home-copy-follows` passed (2.7 and 2.2 s). The latter
retires the old `host-npc-leaves-copy-follows` function and name; disable/enable coverage is in the other spec.
The two specs using Lydia as a mortal NPC failed: `.kill` put her in bleedout, with `dead: false` on owner and
copy. Their data was left unchanged (since: the fake's Lydia is essential too, and the specs kill a chicken). `guest-down-gets-up` reached its down, recovery and actor checks, but
failed the baseline reset after recovery motion displaced the player (the spec is removed since: respawn is
parked for the spectate design, `docs/ideas.md`). These are retained failures, not green
shared-spec coverage. Step readouts are in `pinned-35bfae3b44ee/actor-specs.json`.

The world-state enabled row does **not** cover placed actors: `HostEngine::OnFormChange` and
`ReferenceStates::RecordedRows` accept `FormType::Reference`, excluding actors. The disabled Vilkas stayed a
disabled copy until `NotifyCharacterSpawn` re-enabled it; the guest log records that path, without an enabled
object row. A held reference still follows the whereabouts at removal, not later movement while the host no
longer simulates it.

Engine measurements confirmed the 5-by-5 exterior grid around the player's cell. Readouts use this repo's
TES names (`centerGrid` at B0/B4, `currentGrid` at B8/BC); CommonLib calls them `currentGrid` and `currentQueued`.
A cell's COC placement is **not necessarily its geometric center**: the live `RespawnPlayer` move in cell
(4, -12) used roughly (17224.8, -47204.5), rather than (18432, -47104). The interior used QASmoke's COC marker.
Later ragdoll/physics positions are separate samples, not the commanded destination. A placed-at-runtime NPC
returned at its original form id after cell unload and `pcb`, although `actor_place` could not find it while
unloaded. Joining from a different interior restored a living and a dead host NPC correctly.

The pre-fix distant respawn pulled the host from grid (4, -12) to (8, -12). With `kRespawn`, the host stayed
exactly put in the corresponding controlled-health-damage measurement. A separate failure remains: the host
lost its guest copy and did not show it again after return movement and travel home. Joining four exterior cells
away also left the guest away from the host; `WithHost` groups an entire worldspace. Preserve these failures
when growing the fake, rather than treating a passed no-tether check as a complete respawn contract.

The chest readout found `HUD Menu` alone immediately after the guest's Take All. The guest then applied
`open C2CE8` from the host's world state and reopened `ContainerMenu` about a second later. A debugger captured
`ReferenceStates::ApplyOpen` calling `BGSOpenCloseForm::SetOpenState`, followed by the engine's open callback
calling `ContainerMenu::OpenMenu` with the guest's own player. The relayed activation correctly used the host
player's copy. `take_all` already waits for a stack containing only the HUD, so a stronger menu-close predicate
does not fix this reopening. Preserve the UI test and model the open/close callback's activator state for a fast
regression, not just item counts. `guest-menu-transitions.json` and both chest readout files retain the sequence.

In a separate owned-chest take, the guest's activation replay called `Actor::StealAlarm` once with the host
player's copy. The captured non-player branch returned before crime processing; the copy was not a player
teammate. No guest bounty changed in any of the nine hold factions over the observed post-action frames. The
host also gained no bounty in that fixture, so it is not a positive witness/bounty control. This establishes
the replay's early exit in that run, not a claim about every mod's crime rules. See `chest-followup.json` and
`alarm-debugger.log` under `pinned-35bfae3b44ee/`.

## Tier 4: soak

- **For:** crashes, deadlocks, retained memory and order bugs. `soak` boots a fresh duo for each shuffled round,
  recording the attempted order, crash reports, all lock patterns, memory and frame stalls. Correctness failures
  are recorded too, but they remain tiers 1-3's job.
- **Run:** `coop_test.py soak --hours 6` or `--rounds N`, optionally `--seed S`. Without `--tag` this explicitly
  runs the full suite, including input, restart scenarios and shared specs. `--tag scripts --rounds 2` is a smoke
  test of the soak runner, not evidence that the full suite soaked. `--dry-run` inspects scope without games.
- **When:** before a release, unattended. **Budget:** hours.
- **Cost:** the older 132-scenario suite exceeded 10 minutes. The later explicit 138-case pinned run (input,
  restart and six specs included) took 1100.9 s for scenarios and 1168.9 s with its fresh boot: budget roughly
  20 minutes at that scope. The retirement removes five duplicate bodies; cold boots can still add minutes.

## How a feature moves down the tiers

```
new feature
  tier 3: run the real thing, targeted; read the engine (harness readouts, IDA)
     |  what Skyrim does, written down (docs/re/, then the fake engine)
     v
  tier 1: the logic split out (pure rule + thin glue), its cases in Catch2
  tier 2: the flow between games against the fake
     |  from here tiers 1-2 gate every change
     v
  tier 3: only its affected scenarios, before a push
  tier 4: in the soak, before a release
```

**Every bug gets a test** in the fastest tier that can show it, failing without the fix: Factorio credits its
tests with the desyncs they caught because each became one. A bug Skyrim finds (a scenario, a soak, play) gets a
tier 1 test or tier 2 flow if the fake can show it; if only Skyrim can, a scenario, and what was learned about the
engine goes into the fake. The first: the copy left by an NPC unloading between its assignment and the host's
answer (`copy_flows.cpp`, found by tier 2 itself).

Keeping the fake honest, as contract tests do: the same scenario spec runs against the fake (tier 2) and against
Skyrim (tier 3) now and then. When they disagree, Skyrim is right and the fake is fixed.

## Trickiest first

Done (`ClientTests`, 173 assertions):

- `PartyRules` (`party_tests.cpp`): the cutscene share rule and the control holds.
- `ScriptTags` (`script_tags_tests.cpp`): GuestScripts' tag table (calls, stacks, timers, delayed natives) and the
  cause rules (`CauseOfCall`, `NativeCalled`, `Within`), with the chain from a guest's lever to a stage's fragment.
- `OwnEvents` (`own_events_tests.cpp`): which events are this game's player's (`IsOwnEvent`, VM handles), what
  happens to each (`Deliver`: run, run as this game's player, drop), and a guest's own quests (`OwnQuests`).
- `SameName` / `NameKey` (`engine_names_tests.cpp`), portable since (no `_strnicmp`).
- `CodeReach` (`code_reach_tests.cpp`): NativeReach's reading of a native's code (Zydis over x64 assembled in the
  test: a leaf ending at its ret, unwind entries chained to one function, tail jumps, a plugin's cells), which of
  the party's calls it makes, and how far reach follows calls. The process is asked through `Code`; the glue
  answers from Windows' unwind data and memory.
- The guest's side of world-state sync is tier 2 (`world_flows.cpp`, above): its logic is the flow, not a rule.

Each was checked by breaking the rule (the native-depth rule, the story without a player, a native's event, a quest
already running, chained parts, a leaf's ret): a test fails each time.

Next, in order:

1. **The copy contract** (`docs/design/copy-contract.md`): what a copy may run. Mostly engine glue, so a pure rule
   comes out only where a decision is made (which upkeep runs, which graph events replay).

## Proposals

### (a) `coop_test.py affected`: scenarios from changed files (built)

```
coop_test.py affected [--base REF] [--run] [--check]
  changed = git diff --name-only REF + git ls-files --others --exclude-standard
            REF defaults to HEAD: committed changes since REF, tracked working changes, untracked files
  tags    = union of every matching SOURCE_TAGS pattern's tags
  print the tags, each reason (file and pattern), and the selected scenarios
  --run   use scenario's runner, baseline, cleanup and desync/lock checks
  --check list every uncovered file under Code/ (except Code/tests/) and fail if any exist
```

`docs/` and `Code/tests/` do not select Skyrim scenarios; tiers 1-2 still gate test changes. Other prose outside
those directories, such as `CLAUDE.md`, is ignored too. All files under `Code/` and `scripts/` participate,
including headers, build recipes and assets; source/script extensions elsewhere participate as well. An
unmatched source prints `no tag for <file>` and fails before any scenario starts. Deleted paths still count.
Git is read from the repository root with NUL-separated paths, so spaces in names and the caller's directory do
not change the selection. Git errors fail the command instead of producing an empty selection.

`SOURCE_TAGS` is an ordered list of `(glob, {tags})` beside `TAGS`. Matching uses `fnmatch` on repository-relative
paths: `*` also spans `/`, so `Services/*InventoryService.*` covers its header and Generic implementation.
All matches contribute, rather than the first winning. Tags and paths print in sorted order, reasons in table
order for each path, and scenarios in their normal suite order. Every table tag must exist in `TAGS`, except
`core`; an unknown tag fails at startup, including for `--help`.

The map follows responsibilities read from the code:

- Each game's services select their system: inventory, magic, combat, values, AI, dialogue, travel, or scripts.
  `InventoryService` selects only `inventory`; Papyrus' `Party*` selects only `scripts`. Broad family/directory
  globs cover related headers and implementations, with narrower entries where a file has its own system.
- The host's session and player list select `session`; its character ownership and arrivals select `spawn`,
  `movement` and `cells`. Host/guest clock, weather, conversation and world-state services follow those systems.
  Host/guest service entry points include `session` and the script/world systems they construct or route.
- `HostEngine` reads globals, quests, objects and containers; `GuestEngine` applies them and runs player natives.
  Their tags include those systems. The common `SkyrimBridge` and its interfaces span every system and are core.
- Skyrim's Papyrus VM and cause rules select scripts and, where relevant, quests. Actor, animation, input,
  magic, forms and persistence glue follow the state they drive. Both existing quest tag spellings (`quest`
  and `quests`) are retained where the shared quest/script rules reach them; `TAGS` is unchanged.
- Harness `CommandsInput` selects input/hands/magic/movement, `CommandsSession` selects session/reload, and
  `CommandsWorld` selects its world/quest/script/dialogue/inventory/object/AI/animation systems. The last also
  serves shared status and log readouts, so it is core. `CommandsStaging` selects its inventory/movement/AI/UI
  systems and is core because console commands and placement establish the baseline. Harness dispatch,
  `EngineCalls`, status, control files and diagnostic readouts are core; dedicated traces/fragments select
  their systems.
- Message families and structs select their payload's system; shared payloads, factories, opcodes, wire and
  network code are core. So are `World`, transport, hooks, engine storage, shared dependencies, build recipes
  and Python harness tooling. Changes there can invalidate any targeted result.

`core` means the full suite: the command prints **"core changed: the full suite covers this, run it before a
release"**. It never expands core into a full run, even with `--run`. With core and other tags, `--run` runs only
the other tags' scenarios and says the full suite was not run; core alone runs nothing. If the other tags
together select every scenario, `--run` refuses that too: a full suite needs an explicit `scenario` invocation.
Selection shares `scenario`'s own tag filter, including its restart opt-in and input rules; targeted runs keep
those rules. `--check` audits coverage without Git or Skyrim; combine it with `--run` to check before selecting.

To inspect the run path without contacting either game, use `affected --run --dry-run`. `scenario --dry-run`
uses the same selection helper. Hidden `--files a b c` replaces Git's path list for focused checks, including
nonexistent/deleted paths; `--files` with no paths selects nothing. For example:

```
coop_test.py affected --files Code/client/Services/Generic/InventoryService.cpp
coop_test.py affected --run --dry-run --files Code/client/World.cpp Code/client/Games/Skyrim/Papyrus/PartyNatives.cpp
coop_test.py affected --check
```

Coverage says a glob exists, not that it is the right one. When a responsibility or scenario changes, review
its tags as well as running `--check`. Later, harness logs could propose patterns from a full run
(`affected --learn`); that is not built.

### (b) `coop_test.py soak`: tier 4 (built)

```
coop_test.py soak [--hours H | --rounds N] [--seed S] [--tag a,b]
  each round
    duo boot (fresh games, so leaks and boot-order bugs show)
    scenario: the full suite, --shuffle with the round's seed
    collect: crashes (CrashLoggerSSE logs, `crash`), lock-order reports (status.json, engine-only too),
             game memory per scenario, frame stalls (World::FrameStats), each FAIL with its seed and order
    a crash: keep the order up to it, boot again, go on with the next round
  summary (build/coop-test/soak/<time>/report.json, compared with the last soak's)
    new crash signatures (top frames), new lock patterns, scenarios that failed for the first time,
    memory that grows round over round; the rest is one line ("12 rounds, nothing new")
```

The default budget is six hours. Budgets are checked between rounds: a started round finishes, so the limit can
overrun by one round. The first shuffle seed is recorded (random if omitted); round N uses `S + N - 1`.
Tag selection uses `scenario`'s filter, including its restart opt-in. With no tag, all registered scenarios are
explicitly named, including input/restart and specs. Every scenario goes through `command_scenario`'s normal
baseline, cleanup, desync/world and lock checks. This is a fresh-process soak, not a test of unbounded growth in
one game; use long sessions as well when investigating lifetime leaks.

Each invocation creates `build/coop-test/soak/<time>-<id>/report.json`. It is atomically checkpointed before the
boot and at every scenario start/result, so the planned order, attempted prefix, seed and current scenario
survive interruption. Each `round-NNNN/` keeps `runner.log`, both games' status/plugin/SKSE logs and any new raw
CrashLogger logs with readable `.report.txt` reports from the same `crash_report` used by `crash`. The original
Proton/boot logs remain under `runs/`, linked by `run_dir`. An aborted round records unrun names; its successor
stops both games and boots afresh. Interrupting saves the report and stops the test games, as does completion.

Before and after every scenario, the report records each game's Linux process id, resident/swap/virtual memory,
plugin build, raw lock reports and frame counters. Frame/stall deltas count only within the same process and
plugin generation; longest-frame/update values are cumulative maxima. These counters already come from harness `status.frames`,
so no production interface or C++ readout was added. Missing memory/readouts remain unavailable, not zero
measurements; after an exit, the last status file is retained with `stale_status: true`. The existing scenario
lock gate still fails on new unreviewed patterns with our code on the stack; soak collection additionally keeps
engine-only and reviewed reports for comparison.

The first round compares findings with the last invocation's report; later rounds compare with that history
and earlier rounds of this invocation. Reports list new crash signatures (exception plus the top five probable
module/offset frames), new lock-site pairs (either direction is the same pair), and scenario names failing for
the first time. Every failure retains its full result, step, seed and order index even if already known. A boot
or baseline error with no scenario is reported separately. Memory growth is a candidate trend: completed rounds'
end-of-suite RSS plus swap must rise every round and by at least 16 MiB overall. Incomplete rounds are excluded;
different shuffled orders can retain different caches, so this is evidence to investigate, not a proven leak.
Both round and invocation summaries include these trends. All underlying samples remain in the report.

The summary is one line when nothing is new; known failures remain in the report and pass counts. Crashes,
failed scenarios, boot/baseline errors or interruptions return failure. New engine-only lock patterns and memory
trends are findings without changing a passing scenario into a correctness failure. Heap-history/quarantine
alternation from `repro` remains a later extension, not an option implemented by `soak`.

Smoke check 2026-10-23: `soak --rounds 2 --tag scripts --seed 20261023`, 28/28 passing in each round, with
different orders (seeds 20261023 and 20261024). Rounds took 271.0 s and 239.3 s, including fresh boots of 66.3 s
and 31.6 s; no crashes or scenario failures. The report is
`build/coop-test/soak/20261023-120430-92ae83/report.json`. It retained 12 lock patterns: 11 reviewed ones and one
unreviewed engine-only pattern. Round 2 added one reviewed pattern and flagged the guest's end memory rising
22.9 MiB (2485.9 to 2508.9 MiB); the host's fell. These are fresh processes with different orders, so the memory
flag needs more comparable rounds before calling it a leak. Frame stalls and per-scenario memory are retained
in the report. Fault-injection checks additionally covered an interrupted/crashed prefix, the next fresh boot,
stable crash signatures and copied reports, known findings, missing samples, and frame-counter resets.

Follow-up after retiring the duplicate scenarios: four fresh `scripts` rounds on pinned DLL `ff3bad3f7834`,
seeds 20261030-20261033, passed 24/24 each. The report is
`build/coop-test/soak/20261023-125857-af8ede/report.json`; all 400 sampled active-game DLL hashes matched the pin.
Rounds took 429.3, 219.8, 228.7 and 225.7 s (1103.6 s total); the first boot took 238.2 s, the others 28.5-37.3 s.
There were no crashes or scenario failures. Two lock pairs newly observed relative to the previous report were
already reviewed patterns. Per-scenario frame-stall deltas totaled 67 on the host and 56 on the guest; cumulative
longest-frame values include boot/loading and must not be read as scenario-only stalls.

| Fresh round | Host end RSS + swap (MiB) | Guest end RSS + swap (MiB) |
|---|---:|---:|
| 1 | 2444.19 | 2497.78 |
| 2 | 2485.46 | 2527.11 |
| 3 | 2504.88 | 2525.57 |
| 4 | 2508.35 | 2538.71 |

The guest did not grow every round; its earlier two-sample flag is unresolved, not confirmed or dismissed.
The host did, by 64.2 MiB overall, with smaller increments each round. Different orders and fresh processes
can retain different caches; the earlier report also used 28 scenarios rather than 24. These samples identify
candidates, not a measured lifetime leak. A controlled long session is needed to distinguish continued growth
from caches; this targeted run does not substitute for a release soak of the full suite.

The earlier unreviewed engine-only pair (`16ABBD > E92C16` versus `1B064A > 165105`, SkyrimSE.exe RVAs) was
traced through both captured stacks and Skyrim 1.6.1170 disassembly. An IO worker restoring saved Havok state
takes an ExtraDataList write lock before the bhkWorld write lock; player positioning takes the world lock before
updating ExtraMultiBoundRef under the extra-data lock. It is a potential lock-order cycle, not an observed
deadlock or a proven harmless path. No suppression was added. Evidence and the remaining vanilla-harness
isolation step are in `build/coop-test/validation/current-20261023/engine-lock-review.md`.

### (c) Shared specs: one spec, two runners (built, the Skyrim side not run yet)

The cutscene scenarios were already steps (`script_call`, `disconnect_client`, `screen(...)["controls"]`). Written
as data in `Code/tests/specs/*.json`, the same steps run against the fake (tier 2) and against Skyrim (tier 3):

```json
{"name": "leaver-cutscene-gives-back",
 "steps": [
   {"do": "script", "game": "guest", "call": "Game.DisablePlayerControls", "args": [true, true, false, false, false, true, true, false, 0]},
   {"expect": "controls", "game": "host", "equals": "0xFFFFFABE"},
   {"do": "leave", "game": "guest"},
   {"expect": "controls", "game": "host", "equals": "0xFFFFFFFF"}],
 "cleanup": [...]}
```

- **Vocabulary:**
  - Steps: `script` (`call` as `Class.Function`, `args`, `by: "player"` for a player's own action), `leave`,
    `join`, and `marker` (a party shake from one game, waited for on the other: everything sent before it has run
    there).
  - World steps: `lock` (the player locks or picks it, the harness's `lock`), `add_items` / `remove_items` (the
    console's `additem` / `removeitem` on a container).
  - Actor steps (2026-10-23), on the host's actors: `spawn_npc` (`base`, `as`: the host's player places one,
    `spawn_host_npc`), `stage_npc` (`ref`, `as`: a placed NPC to the stage, `stage_host_npc`), `send_home`,
    `kill`, `disable`, `enable` (`actor`: `move_actor` home, the console's `kill`/`disable`/`enable`);
    `deadly_hit` (`game`: its player takes more than its health); `go` (`game`, `cell: "elsewhere"`: a travel
    while away, Riverwood in Skyrim, an exterior in the fake). Each spawn or stage waits until the host's game owns
    the actor and every guest has its copy. An `actor` is a name an `as` gave or a hex id.
  - Expectations: `controls` (`equals` a mask, or `taken`), `shake` (`shaking`), `lock` (`locked`, `level`),
    `items` (`item`, `count`); for actors `actors agree` (`Party::Agree` in the fake, `check_desync` in Skyrim),
    `actor` (`game`, `actor`, and any of `dead`, `disabled`, `copy`: the host's actor as that game has it, its copy
    on a guest), `together` (every player where the host's is), `down` (`game`, `down`: its player bleeding out;
    `within` seconds for Skyrim's wait, a respawn takes several). An expectation waits for its state, or with `now` checks it at once: "stays" after
    a marker.
  - A reference is a hex id (QASmoke's small chest, `0xC2CE8`) or a role each runner finds: `inner-door` is
    `inner_door()` in Skyrim and a stand-in in the fake's QASmoke. `tags` are the scenario's tags besides `spec`.
  - `cleanup` always runs, for the duo, which outlives a scenario; the fake runs it too.
- **Tier 2:** `GameTests "[spec]"` runs every spec on a fresh party, one section each (`-c <name>` runs one). A
  failure names the spec, the step and the state ("restriction-stays-with-game, step 4: ... controls FFFFFFFF").
  The files are read at run time: editing a spec needs no rebuild.
- **Tier 3:** `scripts/coop_specs.py [names]` registers each spec as the scenario `spec:<name>` (tags `spec`,
  `scripts`) and runs it with `coop_test.py scenario`'s runner, baseline and checks included. Each step maps to
  `script_call` / `player_script_call` / `disconnect_client` / `rejoin_client` / `screen_marker`, each expectation
  to `wait_for` on `screen` (5 s). `coop_specs.py --dry` prints those calls without the duo.
- **The specs so far:** `cutscene-controls-reach-party`, `leaver-cutscene-gives-back`,
  `overlapping-cutscenes-keep-controls`, `restriction-stays-with-game` (a hand-written scenario's steps each, with
  Skyrim's measured masks: `0xFFFFFABE` for the defaults, `0xFFFFFFBF` for fighting only); `lock-reaches-others`
  and `chest-items-reach-others` (world-state sync: a lock both ways; sweetrolls added on the host, removed on the
  guest). The second assumes a guest's console `removeitem` raises the same container event as its player's take:
  the Skyrim run says. Actors (2026-10-23, the fake only so far): `npc-death-reaches-copy`, `npc-disabled-copy-follows`,
  `npc-sent-home-copy-follows` (the twin of `host-npc-leaves-copy-follows`), `guest-joins-elsewhere`
  (`guest-down-gets-up` dropped: respawn is parked); each failed in the fake with the line it covers broken (the death relay, `FollowOwner`'s
  disable and move, `BringToParty`). Writing the disabled one found that a held reference does not
  follow its owner's actor while the host's game does not simulate it: enabled and sent home in one go, the host
  sends nothing, and the guest's stays disabled on the stage (the contract's scope: it follows where the actor was
  at removal). Its cleanup waits for the copy to be enabled first; whether Skyrim's world-state enabled row covers
  placed actors, which would re-enable it there, is for the Skyrim run.
- **Expectations are in the engine's own values** (the mask Skyrim shows), so a fake that guesses wrong fails the
  spec on tier 2. The jumping bit was found this way, by hand.
- **When:** tier 2 on every change (it's in `scripts/check.sh`); tier 3 when the fake engine changes, and in
  every soak round.
- **Open:** running the four in Skyrim (the duo was busy); `coop_test.py` importing `coop_specs.register` so
  `scenario`, `affected` and `soak` see them; `affected` mapping `Code/tests/specs/` to the `spec` tag. Once a
  spec passes on both, its hand-written scenario goes and the spec is the only copy.
