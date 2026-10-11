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

### Phase 2: Characters and ids (the backbone)

Everything else refers to actors by server id, so this moves first and as one unit.

- The host's `World` hands out server ids. Its own actors get an id at discovery, and the
  `AssignCharacterRequest` round trip for host-owned actors goes away.
- A peer's `AssignCharacterRequest` (its player) is answered by the bridge with an id from the host.
  The host spawns its copy, and the other peers get a `CharacterSpawnRequest` built from it.
- Late join: the bridge builds spawn messages from the host's `World` and engine (`BuildActorData`
  already exists on the client).
- Movement: the host collects its own actors' movement and peers' updates (already applied to its
  copies) and sends `ServerReferencesMoveRequest` to peers at its tick, on the unreliable channel.
- Removal and respawn follow ownership as today.
- Add the `HostBridge` front door in `GameServer::OnConsume`: a message the bridge has a rule for
  is handled there, everything else by the old services.
- The old server `CharacterService` and its components stop being used. Other old services still
  find what they need (ownership) through a small lookup the bridge provides until they move too.

### Phase 3: Relay families

These are mechanical once ids live on the host. One family per step, with the suite after each:

1. Inventory and equipment: `RequestInventoryChanges`, `RequestEquipmentChanges`, the snapshot, `DrawWeaponRequest`.
2. Actor values: values, max values, health, death.
3. Magic and combat: spell cast, interrupt, add target, remove spell, projectile launch.
4. Character extras: factions, mount, AI package, dialogue, subtitle, experience, level, respawn.

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
