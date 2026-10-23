# Host authority migration

Plan for removing the embedded server from the host: the host's Skyrim is the only authority, the
host has one `World`, and a bridge on the host talks to peers directly. (The earlier root planning docs,
`WHY_DIRECT_AUTHORITY_IS_REQUIRED.md`, `P2P_ARCHITECTURE.md` and `DIRECT_AUTHORITY_REFACTOR_STRATEGY.md`, were
deleted on 2026-10-21; they are in git history before that.) This document is the concrete path from today's code.

## Why

- **Physics and other engine systems.** Physics only exists in the host's Havok. Syncing it means
  the host reads each body's result after the step and sends it to peers, who hold those bodies as
  keyframed and blend between updates. Today every synced value takes a capture on the host, a
  request, a server handler and component, a notify, and an apply on the peer, with the host
  sending everything to itself. For hundreds of bodies at a high rate that is not workable. After
  the migration it is: read on the host, one message, apply on the peer. The same goes for doors,
  traps, containers, projectiles and ragdolls.
- **Stale copies.** The server `World` is a copy of state that lives in the host's engine. It can
  disagree with it, and it has: the weapon flag a respawned copy got after a hot reload, and the
  host rejecting its own client as a version mismatch.
- **Duplicate code.** Most server services mirror a client service. One place per feature instead
  of two.

Not a goal: speed for the host (the localhost hop costs well under a millisecond), or fixing copy
replay fidelity (Lydia's stagger, the anger scenario). Those are separate work.

## End state

```
Host Skyrim.exe                                   Peer Skyrim.exe
  engine (the authority)                            engine (replica, own player only)
    | hooks                                           ^ apply
  client World (one per process)                    client World
  services ── Send(request) ──> HostAuthority ──────>  TransportService (unchanged)
                                  |  rules per message:
                                  |   apply a peer's request to the host's copy
                                  |   forward the result to the other peers
                                  └─ enet6 listener for peers (no self-connection)
```

- **No `Code/server/`**, no server `World`, no host connection to itself.
- **Peers do not change.** They connect to the host, send requests about what they own (their
  player), and receive notifications. The wire messages stay as they are, so this is a host-side
  refactor.
- **Services keep one code path.** A service on the host still sends its request (`Send(...)`). In
  host mode, `TransportService` hands it to the bridge in-process instead of to a socket. The bridge
  runs the same rule it runs for a peer's request, with the host as the sender. This keeps
  "if hosting" checks out of the services.
- **The host assigns session ids** from its own `World`. Its own actors get an id when they are
  discovered, with no round-trip. A peer's player gets one when the peer asks.
- **Late joiners are served from live state:** what the host's engine shows (its own actors, and
  its copies of peers' players), plus small records in the host's `World` for what a copy cannot
  show. No server-side cache.
- **The bridge has two kinds of send:** reliable ordered (today's messages) and unreliable
  latest-value (movement now, physics later).

## Where today's server logic goes

The handlers fall into five groups. The appendix lists each message.

| Group | What the server does today | After |
|---|---|---|
| **Session** | Connections, auth, version and mod-list check, player list, string cache | The bridge. String cache entries are assigned by the host directly (no `AddWanted` round trip). |
| **Characters** | Assign ids and owners, spawn copies for others, late-join spawns, movement batching, removal | The host's `CharacterService` plus the bridge. Ids from the host's `World`, movement batched at the host's tick. |
| **Relay** (most) | Check the sender owns the actor, keep a record, forward a notify | A bridge rule per message: apply to the host's copy (by dispatching the same notify the peer would get, so the existing client handlers do the work) and forward to the other peers. Records only where a late joiner needs them. |
| **Host-decided** | Time, weather, quests, objects, locks, party, waypoints, chat | Read from or applied to the host's engine. The host is the authority for the world; peers ask, the host decides. |
| **Dropped** | Lua scripting (`ScriptService`, `Scripting/`), admin protocol and `AdminService`, server console commands, message recording, `EnforceProximity` (replaced by the party tether) | Deleted. Can come back on the host later if wanted. |

## Phases

Each phase ends with the full shuffled suite as green as before it started. Nothing is committed
without review.

### Phase 0: Ground truth and tests

- Check the message table below against the code: sender, server handler, state kept, client
  handler, and whether a scenario covers it.
- Guard the backbone with scenarios before phase 2. Each later family gets its scenarios at the
  start of its own phase (time, weather, doors and containers, quests in phase 4; weather needs an
  outdoor baseline, the current one is indoors).
  - `late-join-client` (added, passes): the client leaves and rejoins while the host's world
    changed. It must see the host's player and NPC as they are now, and the host must drop the
    client's character while it is away.
  - `host-time-reaches-client` (added, **fails today**): the host sets the hour. It must stay set
    on the host and reach the client. It fails because the server's clock drives the host's (see
    below); it passes once phase 4 is done.
- Harness additions: a `disconnect` command on the game's command socket, and a `game` section in
  the status (time, weather, party, connection). The baseline reset rejoins a client that left.
- Decided: `coop_test.py net` (headless bots against a standalone `GameServer`) goes. The
  `fake_player` bot stays as a peer that joins a hosting Skyrim (`game` mode). The standalone
  server tests in `Code/tests` are deleted with `Code/server/` in phase 5.

**What the host gets back from its own server today** (it must get these locally after the
migration):

| Echo | What depends on it |
|---|---|
| `AuthenticationResponse` | The host's player id, server settings and mod list (`ConnectedEvent`) |
| `AssignCharacterResponse` | The session ids of the host's own actors, and that the host owns them |
| `AssignObjectsResponse` | The session ids of the host's objects (containers, doors) |
| `NotifyTimeSettings` | **The host's game clock is switched off and driven by the server's clock** (`CalendarService::OnTimeUpdate`). The host is not the authority on time today. |
| Party messages | Whether the host is the leader, which decides who sends weather. Today nobody is in a party (status shows `in_party: false` on both games), so nobody leads. |
| `StringCacheUpdate` | The host's string ids come back from the server |
| Chat (`NotifyChatMessageBroadcast`) | The sender sees its own message only because the server echoes it |

### Phase 1: The host stops using a socket

- In host mode, `TransportService::ConnectLocal` connects to the embedded server in-process
  (`GameServer::OpenLoopback`). The host's messages go in with `InjectMessage`; the server's replies
  come back through the loopback route, are queued, and are handled on the transport's next
  update, so no client handler runs inside another service. The session tick is the server's own
  (`TransportService::GetTick`). No `127.0.0.1` connection.
- Found on the way: `TransportService::Send` serializes inside a scratch-allocator scope. An
  in-process server running inside that scope allocated its 1MB reply buffer from the 256KB
  scratch and crashed. The bytes are now copied out and handed over after the scope.
- Found on the way: the server's loopback intercept only covered reliable sends, so the host never
  got movement updates (they are unreliable). It now covers every send.
- Found on the way: the server took "the player with the lowest id" as the host. A host that left
  and hosted again after a peer joined lost its NPCs to the peer. The host is now the player on the
  in-process connection (`PlayerManager::SetHostConnection`). Scenario `host-rejoins` guards it.
- The `HostAuthority` front door moves to phase 2, together with its first rules (an empty one would
  only be scaffolding).
- Checked: the suite (same results as before the phase), hot reload 30/30, and the host leaving and
  hosting again (`host-rejoins`).

### Phase 2: The bridge, movement and late join

Ids stay as they are for now: the old services that still pass messages on (inventory, actor
values, magic…) find actors in the server `World` by those ids and check ownership there. Switching
the id source first would break all of them at once, so ids move last (phase 3b), when only one
lookup is left to change.

- **2a. `HostAuthority`.** Owned by `HostService`, installed as the front door of
  `GameServer::OnConsume`: a message the bridge has a rule for is handled there, everything else by
  the old services. A relay rule is "forward as a notification to everyone but the sender". The
  host is one of the recipients: its copy gets the change through the in-process route like any
  peer. Ownership checks go through one bridge function (`OwnerOf(sessionId)`), which reads the
  server `World` until phase 3b.
- **2b. Movement.** `ReferencesMoveRequest` is forwarded straight to the other players as
  `NotifyReferencesMove` (unreliable), instead of being stored in the server's
  `MovementComponent` and batched at 50Hz. `NotifyWorldState` is deleted: it was broadcast every
  second, and no client ever handled it.
- **2c. Late join from live state.** When a peer's player enters, the host builds a
  `NotifyCharacterSpawn` for every character it knows. Its own actors are read from its engine;
  copies of other peers' players combine a small record kept when they spawned (appearance, face
  tints, base) with the copy's live state. The server's records (`Serialize`) are no longer used for
  this. `late-join-client` guards it.
- **The host leaving ends the session.** The host is the session, so it cannot leave and come back
  while peers stay: stopping hosting disconnects everyone (the harness's `disconnect` on the host
  does that). `host-rejoins` checks that a host who hosts again still owns its NPCs.

Done (2a-2c): `HostAuthority` with the movement rule and the late-join handler. The server's movement
handler, its 50Hz batching and `NotifyWorldState` are deleted.

Found on the way: both `World`s keep their services in the registry's context, which outlives the
`World`'s own dispatcher, so the services' event connections were released against a destroyed
dispatcher. The server's `AdminService` also stayed alive as a spdlog sink with such a connection.
Both crashed hot reloads now and then (reliably when the host left and hosted again). Each `World`
now destroys its services first, and `AdminService` detaches with its `World`.

### Phase 3: Relay families, then ids

**3a. Relay families.** Each becomes a bridge rule (ownership through `OwnerOf`), and its server
service and records go. One family per step, with the suite after each:

1. Inventory and equipment: `RequestInventoryChanges`, `RequestEquipmentChanges`, the snapshot, `DrawWeaponRequest`.
2. Actor values: values, max values, health, death.
3. Magic and combat: spell cast, interrupt, add target, remove spell, projectile launch.
4. Character extras: factions, mount, AI package, dialogue, subtitle, experience, level, respawn.

Each family's rules have headless tests in `Code/tests/bridge` (`BridgeTests`, native Linux build, GCC
linked with lld: `scripts/xmake-linux.sh build -j16 BridgeTests && build/linux/x86_64/debug/BridgeTests`). They feed requests into a test-mode
server as the transport sends them and check who receives what. The bridge needs no client `World`
for that: the host's game reaches it as a callback (`SpawnSource`, from `CharacterService`).

Done:
- **1. Inventory and equipment.** Item and equipment changes are relayed from anyone (a peer can
  hand the host's follower a sword; the owner's game applies it like every copy). The equipment
  snapshot and hands are relayed from the owner only. Dropped items stay unsynced. The server's
  `InventoryService` is deleted.
- **2. Actor values.** Values, max values and death are relayed from the owner only (clients only
  send them for actors they own). A hit on someone else's actor goes to its owner alone. The
  server's `ActorValueService` is deleted.
- **3. Magic and combat.** Casts, interrupts and projectiles are relayed from the caster's owner
  only. A spell effect landing on someone else's actor goes to its owner alone (like a hit); an
  effect ending is relayed from anyone. The server's `MagicService` and `CombatService` are deleted.
- **4. Character extras.** Factions are relayed per actor from its owner (immediately; the client
  already sends them at most every 2s). Mount, AI package, dialogue, subtitle and experience are
  relayed from anyone. Experience goes to everyone else, since everyone is in the host's party (the
  client only sends it while in a party, so it starts once phase 4 makes the party). The server's
  handlers for these, its 2s faction batching and `bEnableXpSync` are deleted.
  Respawn (`RequestRespawn`, `PlayerRespawnRequest`) moves to 3b: a non-owner respawning a peer's
  player drops its copy and asks for a fresh spawn, and the record that spawn needs (appearance)
  moves to the host's `World` with assignment. Player level stays on the server with the player
  list (session, phase 5).

The bridge lives in `Code/client/Services/Host/`. `HostAuthority` (named `HostBridge` until 2026-10-20) is the front door, the deliveries
and the plain relays; the parts that keep state register their own rules with it: `HostCharacters`
(ids, owners, spawns, removal, respawn, movement) and `HostClock` (the session's time);
`HostWeather` (the session's weather) only sends. What the
bridge reads and changes in the host's game comes in as `HostGame` callbacks. What a joining
player is sent is listed in one place, `HostAuthority::PlayerEntered`. A plain relay is one line in
the bridge's constructor, naming the request and who
gets it (`Relay<SpellCastRequest, NotifySpellCast>(FromOwner, &SpellCastRequest::CasterId)`). A
request and its notification inherit one payload struct (`Code/encoding/Messages/Payloads.h`):
its fields and their bytes are written once, and the relay copies the payload. Such a pair is two
`using` lines of `Relayed<Payload, Opcode>` (`Messages/RelayedMessages.h`, defined in `Relayed.h`). Only items and
equipment changes pass on something different (`ToNotify`). `Code/tests/bridge/wire_tests.cpp` pins
the bytes of every relayed message. Deliveries: `FromOwner` (the actor's owner says it;
everyone else replays it), `FromAnyone`, and `ToOwner` (done to another's actor; its owner alone
applies it). Rules with more to them are member functions (`On<...>`): movement, items, factions.

Found on the way: the server's "this character is already managed" answer (an `AssignCharacterResponse`
built from its records) never reaches a peer, because only the host announces NPCs. It goes with
assignment in 3b. Two other readers of the records that stopped updating: gold loss on respawn
(off by default, `fGoldLossFactor` 0) and a non-owner's `RequestRespawn`; both move in step 4.

**3b. Ids and assignment.** Done. The bridge keeps the characters: who owns each, the cookie it
was announced with, and for players a spawn record (appearance, base, face; kept current with their
movement), since the host's copy of a player cannot show those and is dropped when it respawns.
- `AssignCharacterRequest`: a player's own character, or (the host only) one of the host's actors,
  gets an id. The owner gets `AssignCharacterResponse`, everyone else a spawn; a player's character
  also gets every other character (the host's actors from its engine, players from their record
  and the host's copy). The host still asks in-process like any player, so its services are
  unchanged.
- Removal by the owner, `CancelAssignmentRequest` (nothing handled it: a copy announced and then
  cancelled stayed on peers forever), and a player leaving remove characters everywhere.
- `RequestRespawn`: the owner's goes to everyone (and updates the record's appearance); anyone
  else's is answered with a fresh spawn. `PlayerRespawnRequest`: everyone else respawns the
  player's character. Gold loss on death (`fGoldLossFactor`, off by default) is dropped.
- Ids come from the server `World`'s entity ids (`GameServer::NewCharacterId`) while objects still
  get theirs there, so the two cannot collide; phase 4 moves objects and the ids become a counter.
- Deleted: the server's `CharacterService` (and its Lua binding), `OwnerView`, the character events,
  the late-join hook, `OwnerConnection`, and `EnforceProximity` (it compared stale positions; the
  party tether replaces it). The client's `SpawnRecordComponent` went to the bridge's records.

Still on the server, reading positions that had been stale since phase 2: teleport to a player
(`OverlayService::OnTeleport`, `CommandService` teleport). Dropped in phase 4 (Decisions).


### Phase 4: Host-decided state

- Time and weather: the host's engine is the clock. Peers get it on join and on change.
  Done: `HostClock` and `HostWeather` (a peer forces the host's weather over its own each frame;
  the host shares nothing without a sky or while its map is open). Only the host's game spawns
  world encounters.
- Cell arrivals: done. Everyone is where the host is (the party tether). A game teleported by the
  engine (its `PositionPlayerEvent`) drops its copies and sends `EnterCellRequest`; once it is with
  the host, `HostCharacters` sends it everyone with the host and them it. A player still on the way
  gets nothing until the host is there.
  Every kind of travel sends the event (doors, map and carriage fast travel, scripted moves, `coc`:
  all end in Address Library ID 40438, `docs/re/systems/player-travel.md`); scenarios
  `party-uses-door`, `party-fast-travels`, `party-changes-cells`, `teleport-keeps-copies`.
  Since 2026-10-13 the `EnterCellRequest` also carries the player's inventory, values and hands, so a
  copy spawned from the host's record is the player as it arrived (it carried the join-time
  inventory before: the copy held the wrong spell until the equipment reconcile).
- Party tether (2026-10-14): whoever travels by itself takes everyone along, the host too (decided
  2026-10-14: anyone leads, everyone follows). A game's own arrival is sent with `EnterCellRequest::Leads`;
  the host answers it with `NotifyPartyTravel` (cell, worldspace, position) to every other game. A game
  already there (the same interior cell; outdoors the same worldspace within its loaded grid) stays; the
  others move their player there with `MoveTo` (`TESObjectREFR::MoveTo_Impl` 56626), the same queued
  travel a door makes, loading screen and all. That arrival is sent with `Leads` false (it went where the
  tether sent it), so a follower never pulls anyone and the party cannot loop; a travel from before
  joining leads nobody either. A player whose game joins away from the host is sent to the host's player
  (2026-10-23, `HostCharacters::BringToParty`). A party that travels again before a follower got there (out of a door and
  straight back in, a follower's game stalled or still loading): until the follower reaches the latest
  place it was sent to, every arrival on the way is following too, and a new travel always moves it, even
  to where it still stands (its pending move would take it away; the engine keeps only the latest queued
  move). Scenario `party-quick-travel` stalls the host's game (SIGSTOP) while the client goes out and back
  in: before this the host went out alone. The client's side is `TravelService` (arrival, stepping aside, following),
  which tells `CharacterService` (`PlayerArrivedEvent`) to drop the old cell's copies and send the
  arrival. Bridge cases in `character_tests.cpp` ("Party tether"); scenarios `party-uses-door` (the
  host leads out, the client leads back in), `party-fast-travels` (each leads once, within one
  worldspace), `load-door-not-replayed` (the host follows the client out); the scenarios' `both_go`
  moves the host only and the client follows. Not covered yet: a travel request anyone can cancel
  (`docs/ideas.md`), and the gaps in `docs/progress.md` ("Migration").
- Arriving together (2026-10-14, replacing the step-aside of 2026-10-13): a door or a `coc` puts everyone on
  one marker, and they stay stacked there. On arrival in a new cell (`TravelService::StackOn`) the players on
  the spot pass through each other (`PlayerCollision`, the collision filter) until each walks off it (60
  units) or is the last one there; then it collides again. Every player joins the stack, followers before
  they get there. One that leaves the cell and comes back stays on it: a late follow can take a player that
  was already on the spot out and back in. A copy counts as present only where its owner is. A player off
  the spot collides at once, but leaves the stack only after half a second off it, so a one-frame jump does
  not end it (2026-10-15). Stacked players stay drawn: hiding them was for the dialogue gather, which is
  gone. A follower faces the way the leader arrived (`EnterCellRequest` and `NotifyPartyTravel` carry the
  heading; the engine levels the pitch on every arrival), so every game shows the same view. Scenarios
  `party-stacks-on-arrival` (the inn, then home: stacked, the same view, walk off, bump into the other) and
  `party-uses-door` (the same view after each door).
- Copies converge on their owner (2026-10-13): the owner's movement says where it is every 100 ms (cell,
  worldspace, position), and each game checks its copy against that (`CharacterService::RunPresence`).
  A copy whose owner is around this game's player (the same cell; outdoors the same worldspace, within
  the loaded grid) but which is in another cell or has no 3D is moved there, at most every 2 s; one the
  engine deleted is dropped and asked for again (`RequestRespawn` from a non-owner, which the host answers
  with a fresh spawn). A spawn for a copy a game already has refreshes it instead of being refused
  (`RefreshCopy`). Events stay the fast path; this makes a lost or refused one recoverable. Found by
  `party-fast-travels`: outdoors the host counts a player with it by worldspace, so a player still far off
  got a copy the engine deleted, and its real arrival spawn was refused. Other state that only arrives as
  events (dead or alive, inventory beyond equipment, the host's roster of characters) gets the same
  treatment when a scenario shows it drift, not before.
- Players' copies travel with the party (2026-10-14): a copy of another player is persistent and lives as
  long as the player is in the session; an arrival keeps it and `RunPresence` moves it to its owner at once.
  The host no longer sends players' characters on an arrival, only its actors there (to a player that
  arrived ahead of it, once the host arrives). A copy is the actor at its form id only while it is flagged
  a copy; otherwise it is gone (the engine reuses a deleted reference's form id). Details and the
  reasoning in `docs/design/copy-contract.md` ("Players' copies travel with the party").
- Quests: the host's game runs every quest and peers mirror it, under the party model
  (`docs/design/party-model.md`: the party is the Dragonborn). Shared dialogue comes first: anyone
  talking to a quest NPC is the host's player talking.
- Objects: activation, locks and script animations. The host applies them in its engine and the
  result reaches peers. With the party tether, only the host's loaded cells matter. They go by form
  id, so objects need no session ids: `AssignObjectsRequest` had no sender since `ec7d02d1` (the cell
  change that sent it was removed), and its leftovers (the server's `ObjectService`, the client's
  `ObjectComponent` and response handler) were deleted 2026-10-13. Container contents are not synced.
- Party: done, as no party at all: being connected is being in the session (see Decisions).
  The party messages, `PartyService` (server and client), `PartyComponent`, `SendToParty`,
  `bAutoPartyJoin` and `NotifyPlayerList` are gone. What a party gated (XP, player dialogue,
  waypoints, chat, overlay health) goes to everyone; leader checks are "is the host". Shared XP
  (upstream's party XP, kept): combat-skill experience a player earns goes out once a second
  (`SyncExperienceRequest`, relayed to everyone) and each other player adds it to its own last used
  combat skill. A player who has not used a combat skill since loading gets nothing. Scenario
  `shared-xp`. The session's
  players stay `NotifyPlayerJoined`/`NotifyPlayerLeft` from `GameServer` (it marks the host) until
  phase 5; the client keeps them in `SessionService`. The host alone can kick, through
  `HostAuthority::Kick` (overlay player list, debug view, `kick` test command). Quest sync stays off
  (`cSyncQuests` in the client's `QuestService`) until quests have their own design.
- Waypoints and chat: done 2026-10-13 as bridge rules; the server's `MapService`, `OverlayService` and
  `CommandService` are deleted. Waypoints (the map marker a player places) are plain relays,
  from anyone to everyone else; the client's `MapService` is registered again (it was dropped in
  `ec7d02d1`), scenario `map-marker-reaches-everyone`. A chat line goes to everyone, the sender too, under its
  name, cut to `kChatMaxLength` bytes without control characters (`ChatText`; 2026-10-19 it replaced
  upstream's markup strip, made for its HTML chat: RmlUi's `{{ }}` binding shows text as text, so stripping
  only ate text like `1 < 2 > 0`); players cannot send system messages. The chat is an RmlUi box
  (`Code/client/Ui/assets/chat.rml`): Enter in gameplay opens its line, the input dispatch hook (68655)
  gives it the keys and the game none while it is open, Enter sends, Escape cancels. Scenario
  `chat-reaches-everyone`, bridge cases in `chat_tests.cpp`. Dropped: the player list's health
  (`RequestPlayerHealthUpdate`; the party panel reads health from the copies), player dialogue lines in
  chat (`PlayerDialogueRequest` and its dialogue-menu hook, 35269), the server's Lua chat hook and
  bindings.
- Commands: `SetTimeCommandRequest` from any player sets the host's clock (peers follow it). Teleporting
  to a player is gone (2026-10-13): the messages, the overlay's and the debug view's buttons, the
  chat command's response and the unused `NotifyActorTeleport`.

### Follow-ups (noted, not scheduled)

- **Copy pose after a teleport.** Fixed 2026-10-12: copies start in their owner's graph state
  (`docs/re/systems/actor-ai-and-copies.md`).
- **Arrivals through doors and fast travel.** Done 2026-10-13: 40438 sends `PositionPlayerEvent` for
  every travel (`docs/re/systems/player-travel.md`).
- **Lock checker blames our hook for the engine's own inversion.** A `coc` run from our console
  hook is tagged "[ours]" (cell unload `ExtraDataList` lock against Havok's world lock); it is
  timing-dependent and failed `party-changes-cells` once. The checker should not count a console
  command frame as ours.
- **Known bugs:** the `duo` harness process once segfaulted on exit after PASS.
- Run the full scenario suite once phase 4 is done.

### Server sections (2026-10-13)

What is left on the server goes one section at a time: the bridge takes the logic over with its
tests (bridge cases, a scenario where a player sees it), then the server's piece is deleted. Test code
that needs the standalone server goes with the section that breaks it.

1. **Player levels:** done. The bridge's `PlayerLevelRequest` rule records the level (still on the
   server's player record, `GameServer::SetPlayerLevel`, until the session moves) and sends
   `NotifyPlayerLevel` to the others; late joiners get it in `NotifyPlayerJoined`. The client's
   `SessionService` updates its record, the party panel shows it. The server's `PlayerService` and its
   Lua binding are deleted; the client's never-sent `PlayerLevelEvent` too (a once-a-second check sends
   level-ups). Bridge cases in `player_tests.cpp`, scenario `party-shows-levels` (the baseline restores
   levels with `setlevel`).
2. **String cache:** dropped rather than moved (2026-10-13). It sent each animation event, face tint
   and script animation name once and an id after that, an optimisation for Skyrim Together's
   server sizes; for 2-4 players the names cost a few KB/s. Every game runs the same build (auth
   checks the commit), so the wire format changed with it: those fields are plain strings now.
   Deleted: `StringCache`, `CachedString`, `StringCacheUpdate` and its opcode, the client's and the
   server's `StringCacheService`, the join snapshot and the player's progress in it. Packet sizes
   were not measured; that belongs with stress tests. Bridge case: names arrive as sent with nothing
   shared first (`character_tests.cpp`); the action, spawn, object and session scenarios pass (39 of 39).
3. **Session**, split in steps, each with bridge cases and the session scenarios before the server's
   piece goes (`HostSession`):
   - **3a, auth gate**, the stages of `docs/design/join-flow.md`, one step each:
     - **3a.1, done:** `HostSession` answers `AuthenticationRequest` (the one message the bridge takes
       from a game that is not a player yet): our build, the player cap (8), the session's settings
       (upstream's defaults, now `HostSession::Settings`). The record, the mod ids and the joined
       notifications stay on the server behind temporary calls (`GameServer::AddPlayer`,
       `AnnouncePlayer`, `Disconnect`). Deleted from the server: its join handler, the Lua join hook,
       the settings and their console commands (`TogglePvp`, `SetDifficulty`), the ModPolicy check
       and its settings, the SKSE/MO2 flags, `PlayerJoinEvent`'s trigger (nothing listened). The
       server still reads a load order from disk at start (`ESLoader`) for the Lua bindings and a
       console command; it goes with them in section 4. Bridge cases in `session_tests.cpp`.
     - **3a.1b, done: refusals arrive, as data.** A refused game used to get nothing: the host's
       kick (`Server::Kick`, `enet_peer_disconnect`) emptied the peer's outgoing queue, refusal
       included. Now the kick waits for what is queued (`enet_peer_disconnect_later`). Following the
       error model (`join-flow.md`, "Errors"): the refused game's transport keeps a `JoinStatus`
       (state, a stable reason code such as `wrong_build` or `full`, one sentence, detail lines),
       worded by `JoinRefusal` from the response's data; the host's chat gets a system line naming
       who could not join and why (a session event, sent by `HostSession` to the host's connection).
       Nothing shows the status in game yet: the join panel will (`join-flow.md`); the harness
       reports it as `join`. Tested by bridge cases and the scenario `join-refused-shows-why`, which
       checks reason codes and the host's chat line and uses `coop_peer` (`Code/tests/peer`): a
       stand-in on the real network, as a host refusing every join or as a game asking to join.
     - **3a.2, done:** the game and SKSE versions (`GameVersions`, from SKSE at load through
       `RunningGame`) in the join request; the host compares them with its own (`HostGame::Versions`)
       after the build and refuses a mismatch with `kWrongGame` and its versions (reason
       `wrong_game`). The SKSE and MO2 "active" flags left both messages. Bridge cases, the encoding
       round trip, and `join-refused-shows-why` (stand-ins on another Skyrim and SKSE, both ways).
     - **3a.3, done:** the plugin list and load order against the host's game (`HostGame::Plugins`,
       read at hosting; `RunningGame::Plugins` builds the list the same way everywhere), compared by
       one shared function (`CompareLoadOrders`, `Code/encoding/Structs/LoadOrder.h`: missing, extra,
       light flag, the first place the order parts; names without case). A difference is refused with
       `kModsMismatch` and the host's list; the refused game computes and words the differences
       (`mods_differ`, one detail line each) and the host's chat says how many. Tested by bridge cases
       and with two real games: `join-refused-plugins-differ` restarts the client game with one more
       plugin (an empty `SkyrimCoopTest.esp` in its own `Plugins.txt`), which the host refuses.
       That test found that a game that ends without a word (crash, kill) was never dropped: enet6
       reports a timeout as its own event (`ENET_EVENT_TYPE_DISCONNECT_TIMEOUT`), which neither
       `Server::Update` nor `Client::Update` handled, so the host kept the player and took the next
       game on that peer for it ("asked to join again"), and a friend never noticed a host that died.
       Both handle it now; a kick no longer reports the same disconnection twice.
     - **3a.4, done:** the session's settings are the host's: `HostSession::Settings` reads the host's
       difficulty (`HostGame::ReadDifficulty`) for every join, and `HostSession::Update` sends
       `NotifySettingsChange` to the others when it changes. A friend's game takes and holds it
       (`PlayerService`) and gets its own difficulty back on leaving; the host's game is never
       changed. Before, every game, the host's too, was forced to Master each frame, so the host could
       not change its difficulty while hosting, and a second settings message overwrote what a friend
       got back on leaving. The co-op options (greetings, PvP, death system, homes, calendar) keep
       upstream's defaults until a settings screen. Bridge cases and `difficulty-follows-host`
       (through the harness's `difficulty` command, which sets it as the settings menu does).
   - **3b, player list, done:** `HostSession` announces a newcomer both ways (it learns everyone
     here, with the host marked; they learn it) and, through the bridge's leave handler, tells the
     others a player left (`NotifyPlayerLeft`, also after a timeout). Deleted from the server:
     `AnnouncePlayer` and its own `NotifyPlayerLeft`. The joined message lost its cell (only the dead
     CEF overlay read it, for a cell name). The records stay on the server behind accessors
     (`PlayerIdOf`, `PlayerNameOf`, `PlayerLevelOf`) until 3c. Bridge cases; on the real games the
     `session` scenarios, and `host-kicks-client` fails without the leave notice.
   - **3c, player records, done:** `HostPlayers` is the session's player list (id, name, level,
     whether the host: the game on the server's in-process route, `GameServer::LoopbackConnection`);
     `HostSession` adds and removes players by its join and leave rules, the other parts read the
     list (`Find`, `IsPlayer`, `Host`, `Connections`) and send through it (`SendToOthers`); it kicks
     (never the host). Ids are never reused within a session. The server reports every connection that
     ends (`SetDisconnectionHandler`) and the bridge decides what that means. Deleted from the server:
     `IsAuthenticated`, `HostConnection`, `PlayerIdOf`, `PlayerNameOf`, `PlayerLevelOf`,
     `SetPlayerLevel`, `KickPlayer`, `SendToOthers`, `PlayerConnections`. **The server keeps a shadow
     record** (`AddPlayer`, a `Player` in `PlayerManager`) for its last services (quests, admin, Lua),
     which receive messages the bridge does not handle with the sender's record; section 4 deletes
     them and the shadow with them. The bridge tests now join their players through the real join
     (the host over the in-process route) instead of creating server records. The record has room
     for an account id when stored characters come (`join-flow.md`, stage 4); not added before then.
   - **3d, mod id table, done:** every `GameId` on the wire is (session mod id, form id within the
     plugin); each game maps a session id to its own plugin index (`ModSystem`, from the join
     response's list). Since 3a.3 every game has the host's plugins in the host's order, so a
     plugin's session id is simply its place in the load order: `HostSession` returns the player's own
     list (its own names, for its game to find each plugin by) with those ids, and keeps no table.
     The server's table (`ModsComponent::AddStandard`/`AddLite`, one counter in first-seen order,
     the host's plugins first) is gone; its shadow record carries no mods. Tests first: the bridge
     case written against the old table found that it gave a plugin whose name differs only in case
     (`update.esm`) a new id, although the gate lets that game in; positions cannot disagree.
   - **Section 3 is complete.** What stays on the server: the shadow player record and the services
     that read it (quests, admin, Lua), the console, recording, the listener: section 4.
4. **The listener (phase 5).**

Quests stay on the server until they have a design.

### Phase 5: Delete the server

Done 2026-10-21 (`docs/progress.md`, "Phase 5: the server is gone"): `HostNetwork` is the listener.

- Nothing falls through to `GameServer` any more. Move the enet6 listener into the bridge.
- Delete `Code/server/` (including Lua scripting, the admin service and recording), `admin_protocol`
  if nothing else uses it, the server tests in `Code/tests` that need a standalone server, the
  Docker and packaging files (`Dockerfile`, `Dockerfile.builder`, `docker-compose.yml`,
  `docker-multiarch-build.sh`), and the Linux server notes in `CLAUDE.md`.
- `HostService` becomes the bridge's owner (start/stop hosting).

### After: physics sync (own plan)

Before it, world sync sends a physics object's rest position (`docs/progress.md`, "World sync: finishing it",
step 5): the same final layout on every game, each simulating the flight its own way. Live physics replaces the
flight, not the rest positions, and adds ragdolls (dead actors) and a guest's pushes, arrows and shouts as impulses.

Built on the bridge's unreliable channel:
- The host reads rigid body transforms and velocities after the Havok step for bodies near the party.
- Peers keyframe those bodies and blend between updates.
- A peer touching an object sends an interaction or impulse request, and the host applies it.

## Risks

- **Re-entrancy.** The bridge dispatching notifications into the host's dispatcher while a service
  is iterating its own views. Rules queue work and apply it at a fixed point in the host's update.
- **Host code that relies on echoes.** Some client code may only work because the host receives its
  own messages back from the server (for example an `AssignCharacterResponse` for its own actors).
  Phase 0's table must mark these.
- **Two id sources during phase 2.** The old services may look up actors by server-`World` entity.
  Phase 2 switches the id source in one step, behind the ownership lookup.
- **Hot reload.** The bridge lives in the client DLL, so a reload drops peer connections and peers
  rejoin, as today. Phase 1 checks this explicitly.
- **Tests that need a standalone server** (`net` mode, `TPTests` server round-trips) go away or move
  to peer-against-host tests.

## Decisions

1. **Party:** there are no parties: everyone connected is in the session, and the host is its
   authority. No invites or leader changes; the host alone can kick a player (a disconnect).
2. **Set-time command:** any player can run it; it runs on the host's clock, which everyone
   follows. **Teleporting to a player:** none. Under the party tether everyone is already in the
   same cell or worldspace, so nobody needs to teleport to anyone (decided 2026-10-13).
4. **Party tether:** anyone leads. A player's own travel (a door, fast travel, a scripted move, `coc`) takes
   the whole party along, the host too; there is no confirmation yet (decided 2026-10-14). A countdown
   anyone can cancel can come later (`docs/ideas.md`).
3. **The host owns every NPC.** A peer only ever owns its own player, so peers need no ids, no
   discovery and no cell tracking for anything else. Keeping peers this simple is the point of the
   refactor.

## Appendix: messages the server handles today

| Server service | Messages in | Group |
|---|---|---|
| GameServer | `AuthenticationRequest`, connect/disconnect | Session |
| StringCacheService | (dirty-cache broadcast) | Session |
| PlayerService | `PlayerLevelRequest`, `PlayerRespawnRequest` | Relay (done: bridge rules, service deleted) |
| CharacterService | `AssignCharacterRequest`, `ReferencesMoveRequest`, `RequestRemoveCharacter`, `RequestRespawn` | Characters |
| CharacterService | `RequestFactionsChanges`, `MountRequest`, `NewPackageRequest`, `DialogueRequest`, `SubtitleRequest`, `SyncExperienceRequest` | Relay |
| InventoryService | `RequestInventoryChanges`, `RequestEquipmentChanges`, `RequestEquipmentSnapshot`, `DrawWeaponRequest` | Relay |
| ActorValueService | `RequestActorValueChanges`, `RequestActorMaxValueChanges`, `RequestHealthChangeBroadcast`, `RequestDeathStateChange` | Relay |
| MagicService | `SpellCastRequest`, `InterruptCastRequest`, `AddTargetRequest`, `RemoveSpellRequest` | Relay |
| CombatService | `ProjectileLaunchRequest` | Relay |
| CalendarService | (time on join and on change) | Host-decided |
| WeatherService | `RequestCurrentWeather`, `RequestWeatherChange` | Host-decided (done: deleted, `HostWeather` sends the host's) |
| QuestService | `RequestQuestUpdate` | Host-decided |
| ObjectService | `AssignObjectsRequest`, `ActivateRequest`, `LockChangeRequest`, `ScriptAnimationRequest` | Relays by form id (done; ids dropped, service deleted). `ScriptAnimationRequest` deleted 2026-10-21: a script's animations are the host's world results (`NotifyObjectChanges`) |
| PartyService | `PartyCreateRequest`, `PartyInviteRequest`, `PartyAcceptInviteRequest`, `PartyLeaveRequest`, `PartyKickRequest`, `PartyChangeLeaderRequest` | Dropped (no parties; the host kicks through the bridge) |
| MapService | `RequestSetWaypoint`, `RequestRemoveWaypoint` | Relay (done, service deleted) |
| OverlayService | `SendChatMessageRequest`, `PlayerDialogueRequest`, `RequestPlayerHealthUpdate`, `TeleportRequest` | Relay (done, service deleted; teleport dropped) |
| CommandService | `TeleportCommandRequest`, `SetTimeCommandRequest` | Host-decided (set time: any player, on the host's clock; teleport dropped; done, service deleted; `/settime` in the chat, the hour reached as a wait does, 2026-10-20) |
| ScriptService, AdminService | (Lua events, admin protocol) | Dropped |
