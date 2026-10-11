# Host authority migration

Plan for removing the embedded server from the host: the host's Skyrim is the only authority, the
host has one `World`, and a bridge on the host talks to peers directly. Background and the reasons
are in `WHY_DIRECT_AUTHORITY_IS_REQUIRED.md`, `P2P_ARCHITECTURE.md` and Phase 4 of
`DIRECT_AUTHORITY_REFACTOR_STRATEGY.md`. This document is the concrete path from today's code.

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
  services ── Send(request) ──> HostBridge ──────>  TransportService (unchanged)
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
- **The host assigns server ids** from its own `World`. Its own actors get an id when they are
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
| `AssignCharacterResponse` | The server ids of the host's own actors, and that the host owns them |
| `AssignObjectsResponse` | The server ids of the host's objects (containers, doors) |
| `ServerTimeSettings` | **The host's game clock is switched off and driven by the server's clock** (`CalendarService::OnTimeUpdate`). The host is not the authority on time today. |
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
- The `HostBridge` front door moves to phase 2, together with its first rules (an empty one would
  only be scaffolding).
- Checked: the suite (same results as before the phase), hot reload 30/30, and the host leaving and
  hosting again (`host-rejoins`).

### Phase 2: The bridge, movement and late join

Ids stay as they are for now: the old services that still pass messages on (inventory, actor
values, magic…) find actors in the server `World` by those ids and check ownership there. Switching
the id source first would break all of them at once, so ids move last (phase 3b), when only one
lookup is left to change.

- **2a. `HostBridge`.** Owned by `HostService`, installed as the front door of
  `GameServer::OnConsume`: a message the bridge has a rule for is handled there, everything else by
  the old services. A relay rule is "forward as a notification to everyone but the sender". The
  host is one of the recipients: its copy gets the change through the in-process route like any
  peer. Ownership checks go through one bridge function (`OwnerOf(serverId)`), which reads the
  server `World` until phase 3b.
- **2b. Movement.** `ClientReferencesMoveRequest` is forwarded straight to the other players as
  `ServerReferencesMoveRequest` (unreliable), instead of being stored in the server's
  `MovementComponent` and batched at 50Hz. `WorldStateSync` is deleted: it was broadcast every
  second, and no client ever handled it.
- **2c. Late join from live state.** When a peer's player enters, the host builds a
  `CharacterSpawnRequest` for every character it knows. Its own actors are read from its engine;
  copies of other peers' players combine a small record kept when they spawned (appearance, face
  tints, base) with the copy's live state. The server's records (`Serialize`) are no longer used for
  this. `late-join-client` guards it.
- **The host leaving ends the session.** The host is the session, so it cannot leave and come back
  while peers stay: stopping hosting disconnects everyone (the harness's `disconnect` on the host
  does that). `host-rejoins` checks that a host who hosts again still owns its NPCs.

Done (2a-2c): `HostBridge` with the movement rule and the late-join handler. The server's movement
handler, its 50Hz batching and `WorldStateSync` are deleted.

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

Each family's rules have headless tests in `Code/tests/bridge` (`BridgeTests`, native Linux build:
`XMAKE_CONFIGDIR=build/linux-config xmake build BridgeTests`). They feed requests into a test-mode
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

The bridge is one file. A plain relay is one line in its constructor, naming the request and who
gets it (`Relay<SpellCastRequest, NotifySpellCast>(FromOwner, &SpellCastRequest::CasterId)`). A
request and its notification inherit one payload struct (`Code/encoding/Messages/Payloads.h`):
its fields and their bytes are written once, and the relay copies the payload. Only items and
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

Still on the server, reading positions that have been stale since phase 2: teleport to a player
(`OverlayService::OnTeleport`, `CommandService` teleport). Phase 4 answers them from the host's game.


### Phase 4: Host-decided state

- Time and weather: the host's engine is the clock. Peers get it on join and on change.
- Quests: the host's quest log is the authority, and stage changes are sent to peers.
- Objects: activation, locks and script animations. The host applies them in its engine and the
  result reaches peers. With the party tether, only the host's loaded cells matter.
- Party: everyone in the session, the host leads (see Decisions). The party request messages go.
- Waypoints, chat, player dialogue, overlay health: relays.
- Commands (`TeleportCommandRequest`, `SetTimeCommandRequest`): the host only for now.

### Phase 5: Delete the server

- Nothing falls through to `GameServer` any more. Move the enet6 listener into the bridge.
- Delete `Code/server/` (including Lua scripting, the admin service and recording), `admin_protocol`
  if nothing else uses it, the server tests in `Code/tests` that need a standalone server, the
  Docker and packaging files (`Dockerfile`, `Dockerfile.builder`, `docker-compose.yml`,
  `docker-multiarch-build.sh`), and the Linux server notes in `CLAUDE.md`.
- `HostService` becomes the bridge's owner (start/stop hosting).

### After: physics sync (own plan)

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

1. **Party:** everyone in the session is one party, automatically, and the host is the leader. No
   invites, kicks or leader changes.
2. **Teleport and set-time commands:** the host only, for now. Later a peer can ask for them as a
   Borderlands-style request that anyone can cancel, like leaving a cell under the party tether.
3. **The host owns every NPC.** A peer only ever owns its own player, so peers need no ids, no
   discovery and no cell tracking for anything else. Keeping peers this simple is the point of the
   refactor.

## Appendix: messages the server handles today

| Server service | Messages in | Group |
|---|---|---|
| GameServer | `AuthenticationRequest`, connect/disconnect | Session |
| StringCacheService | (dirty-cache broadcast) | Session |
| PlayerService | `PlayerLevelRequest`, `PlayerRespawnRequest` | Relay |
| CharacterService | `AssignCharacterRequest`, `ClientReferencesMoveRequest`, `RequestRemoveCharacter`, `RequestRespawn` | Characters |
| CharacterService | `RequestFactionsChanges`, `MountRequest`, `NewPackageRequest`, `DialogueRequest`, `SubtitleRequest`, `SyncExperienceRequest` | Relay |
| InventoryService | `RequestInventoryChanges`, `RequestEquipmentChanges`, `RequestEquipmentSnapshot`, `DrawWeaponRequest` | Relay |
| ActorValueService | `RequestActorValueChanges`, `RequestActorMaxValueChanges`, `RequestHealthChangeBroadcast`, `RequestDeathStateChange` | Relay |
| MagicService | `SpellCastRequest`, `InterruptCastRequest`, `AddTargetRequest`, `RemoveSpellRequest` | Relay |
| CombatService | `ProjectileLaunchRequest` | Relay |
| CalendarService | (time on join and on change) | Host-decided |
| WeatherService | `RequestCurrentWeather`, `RequestWeatherChange` | Host-decided |
| QuestService | `RequestQuestUpdate` | Host-decided |
| ObjectService | `AssignObjectsRequest`, `ActivateRequest`, `LockChangeRequest`, `ScriptAnimationRequest` | Host-decided |
| PartyService | `PartyCreateRequest`, `PartyInviteRequest`, `PartyAcceptInviteRequest`, `PartyLeaveRequest`, `PartyKickRequest`, `PartyChangeLeaderRequest` | Host-decided |
| MapService | `RequestSetWaypoint`, `RequestRemoveWaypoint` | Relay |
| OverlayService | `SendChatMessageRequest`, `PlayerDialogueRequest`, `RequestPlayerHealthUpdate`, `TeleportRequest` | Relay |
| CommandService | `TeleportCommandRequest`, `SetTimeCommandRequest` | Host only (for now) |
| ScriptService, AdminService | (Lua events, admin protocol) | Dropped |
