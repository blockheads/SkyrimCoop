# SkyrimCoop Network Architecture

**Written:** 2026-03-29
**Status:** Active design document — update as implementation evolves

## Core Principle: Single Simulation, Multiple Renderers

The host's Skyrim is the ONE simulation. Clients do not independently simulate synced state — they receive results from the host and render them.

This is a fundamental departure from Skyrim Together Reborn (STR). STR's server was headless — no Skyrim engine, no hooks, no game state. It could only relay what clients told it, and each client simulated independently. This caused persistent desync because two Skyrim instances running the same scenario will diverge (NPC AI, physics, scripts, random outcomes all differ per machine).

Our architecture eliminates divergent-simulation desync by design: one machine simulates, everyone else renders the result.

### Why This Works

The host runs Skyrim + an embedded GameServer in the same process. The GameServer has full access to the host's game state via hooks. This means:

- The server can read actual game positions, not just what clients report
- The server can hook any game event (actor spawn, physics change, script trigger)
- The server IS the game — there's no gap between "what the server thinks" and "what's actually happening"

This is the same architecture used by Deep Rock Galactic (UE4 listen server), Borderlands (UE3/4), Left 4 Dead 2 (Source Engine), Terraria (merged server+client), and Helldivers 2. Every successful 2-4 player co-op game converges on this model.

## Authority Model

| State | Authority | Mechanism |
|-------|-----------|-----------|
| NPC positions/AI | Host | Host simulates, broadcasts results. Clients suppress AI (SetRemote). |
| World objects/physics | Host | Host hooks physics changes, sends results. Clients apply. |
| Player character position | Each player's own client | Client reads own position, sends to host. Host relays to other clients. Trusted (co-op, not adversarial). |
| Quest state | Host | Host runs quests. Clients receive quest stage updates. |
| Weather/time | Host | Host sends weather ID + game time. Clients force-set. |
| Script-driven events | Host | Host runs Papyrus, sends effects (doors, traps, containers). |
| Random outcomes (loot, dialogue) | Host | Host makes all random decisions, sends results. |
| Inventory/equipment | Host-authoritative | Changes validated by host before applying. |
| Actor values (health, magicka) | Host | Host computes, broadcasts. |

**Clients never roll their own simulation for anything the host controls.**

## State Sync: Two-Layer Model

### Layer 1: Event-Driven Fast Path (Immediate)

Standard request/response messages handle real-time updates:

- `CharacterSpawnRequest` — actor appears
- `NotifyRemoveCharacter` — actor removed
- `ServerReferencesMoveRequest` — position/rotation update
- (Future: inventory changes, combat events, quest updates, etc.)

These provide low-latency responsiveness. When the host spawns an actor, clients see it within one network round-trip.

### Layer 2: Periodic Reconciliation (1-Second Safety Net)

A `WorldStateSync` message sent every 1 second from server to each client containing:

- Complete list of synced entity serverIds
- Current position/rotation for each
- (~1KB at 50 actors x ~20 bytes each — trivial bandwidth)

Client reconciliation on receipt:
- **ServerId on server but not on client** -> spawn it (request full data if needed)
- **ServerId on client but not on server** -> remove the orphan
- **Both exist but position differs** -> correct to server position

This makes the system **self-healing**. Even if a spawn event is missed, a remove event is lost, or a bug causes state divergence, the next 1-second sync fixes it. Maximum desync duration: 1 second.

### Why Both Layers

The event-driven fast path handles 99% of cases with sub-100ms latency. The reconciliation loop is the safety net that guarantees eventual convergence. Individual events can be buggy, lost, or processed out of order — the reconciliation catches everything.

This is the same hybrid model used by Source Engine (snapshot + delta), Terraria (dirty flags + periodic full sync), and Unreal Engine (property replication + relevancy checks).

## Client-Side Interpolation

Clients don't teleport remote actors to server positions — they interpolate smoothly.

### Pipeline

```
Host's Skyrim (actor moves)
  -> host hooks detect position change
  -> server broadcasts ServerReferencesMoveRequest
  -> client's native process receives, sets interp.targetPos
  -> InterpolationSystem::Update() runs every frame
     -> lerps currentPos toward targetPos (glm::mix, kLerpSpeed=8.0)
  -> ApplyRemotePositions() sends smoothed position to relay DLL
  -> DLL calls SetPosition() in client's Skyrim
```

At 60fps with kLerpSpeed=8.0, alpha = 0.133 per frame. The actor covers ~13% of remaining distance each frame — smooth exponential catch-up reaching target in ~200-300ms. Standard for co-op games.

### Position Update Channels

- **Reliable ordered (enet6):** Spawn, remove, reconciliation sync, quest state, inventory
- **Unreliable sequenced (enet6):** Position/rotation updates — stale positions are worthless, so retransmission wastes bandwidth and adds latency. The 1-second reconciliation sync catches any gaps.

## Connection Lifecycle

### Client Connect (Fresh Join)

1. Client connects via enet6
2. Server authenticates (mod list validation — strict filename match)
3. Server sends full baseline: all currently synced entities with complete state (position, rotation, appearance, formId)
4. Client creates ECS entities + InterpolationComponents
5. Client sends spawn commands to relay DLL for each entity
6. Normal sync begins (events + 1-second reconciliation)

### Client Disconnect

1. enet6 connection drops
2. **Server side:** removes player's owned entities, broadcasts `NotifyRemoveCharacter` to other clients, removes player from PlayerManager
3. **Client side:** native process destroys ALL RemoteComponent entities, sends `cmd_remove_actor` for every tracked pointer to DLL, clears serverId->pointer map. Clean slate.

### Client Reconnect

**Same as fresh connect.** No special reconnect logic. Server sends full baseline from current state. Client rebuilds everything from scratch.

This is what every successful co-op game does (L4D2, Terraria, DRG). No state recovery, no differential catch-up — just a fresh baseline.

### Host Disconnect

**Session ends.** No host migration. No co-op game in production implements host migration for this pattern. DRG's developers explicitly called it "too technically complex to transfer." This is accepted industry standard for co-op.

## Relay DLL Architecture

The relay DLL is a **stateless thin shim** inside Skyrim's process (via SKSE/Wine). It:

- Receives commands from native process via local TCP (localhost, ~1ms latency)
- Executes game function calls on Skyrim's game thread (via SPSC command queue)
- Reports results back via TCP (spawn_result with actor pointer, hook events)
- Holds ZERO tracking state — all identity mapping (serverId->pointer) lives in the native process

```
Native Process (Linux ELF)          Relay DLL (inside Skyrim/Wine)
  |                                    |
  |-- cmd_spawn_actor ------------>    |-- TESNPC::Create + Actor::Create
  |                                    |-- spawn_result {ptr} ------------>
  |-- cmd_set_position(ptr) ------>    |-- SetPosition(ptr)
  |-- cmd_remove_actor(ptr) ------>    |-- Actor::Delete(ptr)
```

The DLL is never a source of truth. If the native process restarts, it can re-send all spawn commands. If the DLL restarts, the native process detects the TCP disconnect and re-establishes.

## Cell Management (Future Phase)

**Design direction:** Host drives cell transitions. All clients follow.

- Host enters a new cell -> server broadcasts "go to this cell" -> clients get teleported
- Client cannot independently enter a different cell
- Eventually: Borderlands-style "traveling to host..." loading screen
- Only entities in host-loaded cells are synced

This is the Borderlands/DRG model: the host's loaded area IS the game world.

## Desync Prevention Summary

| Risk | Prevention | Recovery |
|------|-----------|----------|
| Missed spawn/remove event | 1-second reconciliation sync | Next sync auto-spawns/removes |
| Position drift | Interpolation toward server target | 1-second sync corrects |
| Stale actor pointers | actor_removed hook in DLL | Reconciliation removes orphans |
| Client Skyrim diverges | AI suppressed on remote actors, server overwrites positions | Server state always wins |
| TCP drop (DLL<->native) | Native detects, re-establishes | Re-send all spawn commands |
| enet6 drop (client<->server) | enet6 reliable delivery handles packet loss | Full reconnect on sustained loss |
| NPC AI divergence | Clients don't run AI on remote actors | Host's simulation is the only one |
| Physics divergence | Host hooks physics, sends results | Clients apply, don't simulate |
| Script divergence | Host runs scripts, sends effects | Clients apply, don't run scripts |

## Bandwidth Budget

| Component | Per Client |
|-----------|-----------|
| Position updates (50 actors, 10-20Hz, delta) | ~50 KB/s |
| Reconciliation sync (50 actors, 1Hz, full) | ~1 KB/s |
| Player input (20Hz) | ~2 KB/s |
| Events (combat, inventory, quest) | ~5 KB/s burst |
| **Total** | **~60 KB/s typical** |

For reference: Halo: Reach replicated a biped at 1 kbit/s at 10Hz in a competitive FPS. Valheim struggles at 60 KB/s only with 5,000+ building pieces. We're well within any modern connection.

## Research Basis

This architecture is informed by analysis of production co-op games:

- **Deep Rock Galactic** (UE4 listen server, 4 players) — host-authoritative, UE replication, no host migration
- **Borderlands** (UE3/4, 4 players) — host-authoritative, host's loaded area is the game world
- **Left 4 Dead 2** (Source Engine, 4 players) — snapshot/delta at 30Hz, interpolation, lag compensation
- **Terraria** (custom engine, 8 players) — server-authoritative NPCs, event-driven dirty flags, full resync on connect
- **Valheim** (Unity, 10 players) — cautionary tale: distributed zone ownership causes desync; full-state ZDO resends for correctness
- **Helldivers 2** (proprietary, 4 players) — host-authoritative P2P, modern netcode, no host migration

Key references:
- Glenn Fiedler (Gaffer on Games) — snapshot interpolation, state synchronization
- Valve Developer Wiki — Source Engine multiplayer networking
- Unreal Engine — listen server replication, relevancy, priority accumulator

## Implementation Phases

| Phase | What It Adds to This Architecture |
|-------|----------------------------------|
| Phase 16 | Actor spawning (TESNPC::Create), WorldStateSync reconciliation, disconnect cleanup, unreliable position channel, AI suppression |
| Phase 18 | Equipment sync, actor values, death/respawn, weapon drawn (same event + reconciliation pattern) |
| Future | Physics sync, script event sync, cell tethering, weather/time sync (extend the pattern to each state category) |

---

*This document describes the target architecture. Implementation proceeds incrementally through phases, each extending the same core pattern: host hooks game state -> server broadcasts -> client interpolates/applies -> reconciliation guarantees convergence.*
