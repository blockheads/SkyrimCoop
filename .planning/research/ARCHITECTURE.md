# Architecture Research

**Domain:** Embedded host-authoritative server in a single MinGW SKSE DLL
**Researched:** 2026-03-30
**Confidence:** HIGH — based on direct code inspection of all integration points

---

## Standard Architecture

### System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                SkyrimTogetherClient.dll (host process)              │
│                                                                     │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  Client World (entt::registry)                                 │ │
│  │  ├─ TransportService  <── dual-mode: loopback or enet6         │ │
│  │  ├─ CharacterService, InventoryService, MagicService, etc.     │ │
│  │  └─ HostService  ──────────────────────────────┐               │ │
│  └────────────────────────────────────────────────│───────────────┘ │
│                           ┌────────────────────────┘               │
│                           │ owns + ticks each frame                 │
│                           ▼                                         │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  GameServer (Server::GameServer)                               │ │
│  │  ├─ Server World (entt::registry — separate instance)          │ │
│  │  │   ├─ PlayerService, CharacterService, InventoryService      │ │
│  │  │   ├─ CalendarService, MapService, QuestService, MagicService│ │
│  │  │   └─ ScriptService (Lua/Sol2)                               │ │
│  │  ├─ enet6 listener port (remote clients connect here)          │ │
│  │  ├─ InjectMessage(bytes, size, connectionId)                   │ │
│  │  └─ SetLoopbackRoute(connectionId, sendCallback)               │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                    ^                   |                             │
│                    |                   v                             │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │  LoopbackTransport (in-process, no socket)                     │ │
│  │  ├─ ClientMessage → serialize → GameServer::InjectMessage()    │ │
│  │  └─ GameServer send callback → deserialize → client handlers   │ │
│  └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────┐
│   SkyrimTogetherClient.dll (peer process) │
│   ├─ TransportService (enet6 only)        │
│   └─ CharacterService, etc.              │
│   connects to host IP:10578 via enet6     │
└──────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Location |
|-----------|---------------|----------|
| `HostService` | Owns `GameServer` unique_ptr; calls `GameServer::Update()` from client UpdateEvent each frame; manages host lifecycle (StartHosting / StopHosting) | `Code/client/Services/HostService.h` — EXISTS |
| `LoopbackTransport` | In-process message bridge: serialize `ClientMessage` into `GameServer::InjectMessage()`; register server send callback via `SetLoopbackRoute()` | `Code/native_client/transport/LoopbackTransport.h` — PORT to `Code/client/transport/` |
| `ServerMessageRouter` | Deserialize raw server message bytes into typed `ServerMessage` objects; dispatch to per-opcode handlers | `Code/native_client/transport/ServerMessageRouter.h` — PORT to `Code/client/transport/` |
| `TransportService` | Dual-mode: in host mode delegates `Send()` to `LoopbackTransport`; in client mode serializes + sends via enet6. `OnConsume()` handles inbound bytes in both modes. | `Code/client/Services/TransportService.h` — MODIFY |
| Client `World` | `entt::registry` for all client-side ECS; holds all client services in context; already includes `HostService` in `World.h` | `Code/client/World.h` — EXISTS |
| `GameServer` | `TiltedPhoques::Server` subclass; enet6 port listener for remote clients; `InjectMessage()` injects client bytes directly; `SetLoopbackRoute()` registers callback for server->host delivery | `Code/server/GameServer.h` — EXISTS with loopback API |
| Server `World` | Separate `entt::registry` for authoritative server state; owned by `GameServer`; has all server services in context | `Code/server/World.h` — EXISTS |

---

## Recommended Project Structure

The build system already has the dependency graph wired. No new directories are mandatory, but porting the transport layer into the client is the clearest path.

```
Code/
├── client/
│   ├── Services/
│   │   ├── HostService.h/.cpp        # EXISTS -- manages GameServer embed + lifecycle
│   │   ├── TransportService.h/.cpp   # MODIFY -- add m_pLoopback, dual-mode Send()
│   │   └── ... (all other services unchanged)
│   ├── transport/                    # CREATE -- port from Code/native_client/transport/
│   │   ├── LoopbackTransport.h/.cpp  # PORT -- remove TcpClient dependency
│   │   └── ServerMessageRouter.h/.cpp # PORT -- pure deserialization, no changes
│   └── World.h/.cpp                  # EXISTS -- HostService already in ctx()
├── server/
│   └── GameServer.h/.cpp             # EXISTS -- InjectMessage + SetLoopbackRoute in place
└── encoding/                         # UNCHANGED -- shared message definitions
```

### Structure Rationale

- **`Code/client/transport/` subfolder:** Mirrors the pattern already established in `Code/native_client/transport/`. Keeps loopback and message routing isolated from game service logic. Avoids polluting `Services/` with networking plumbing.
- **No server directory changes:** The server is linked as a static library. `Code/client/xmake.lua` already has `add_deps("SkyrimTogetherServer")` and `add_includedirs("../server")`. No new build configuration required.
- **No World merge:** Two separate `entt::registry` instances coexist. Server World owns authoritative actor state under `ConnectionId_t` keys; client World owns local/remote actor views under form IDs and server IDs. Merging requires rewriting every server service handler — the loopback delivers a working system first.

---

## Architectural Patterns

### Pattern 1: In-Process Loopback Bridge

**What:** `LoopbackTransport` serializes a `ClientMessage` into a raw byte buffer (same format as enet6 would send), then calls `GameServer::InjectMessage(buffer, size, kHostConnectionId)` directly. For the return direction, `GameServer::SetLoopbackRoute(kHostConnectionId, callback)` stores a lambda; `GameServer::Send()` checks if the target `ConnectionId_t == m_loopbackConnectionId` and calls the lambda instead of enet6.

**When to use:** Every message the host's client sends. The loopback replaces the enet6 socket path entirely for the host player.

**Trade-offs:** One serialization+deserialization round-trip per message in host mode. This is microseconds (in-process memcpy) vs milliseconds (enet6 UDP). The round-trip is the correct trade-off because it preserves all server-side validation logic unchanged.

**Key constant:** `LoopbackTransport::kHostConnectionId = 0xFFFF'FFFF'0000'0001ULL` — large enough that it cannot collide with real enet6 peer IDs (assigned from peer pointer addresses, which are small integers on 64-bit Windows under typical heap layout).

```cpp
// From Code/native_client/transport/LoopbackTransport.h (to be ported)
void LoopbackTransport::SendToServer(const ClientMessage& acMessage)
{
    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // packet type prefix byte (same as enet6 path)
    acMessage.Serialize(writer);
    m_server.InjectMessage(buffer.GetWriteData(), writer.Size(), kHostConnectionId);
}
```

### Pattern 2: Dual-Mode TransportService

**What:** `TransportService` gains a `std::unique_ptr<LoopbackTransport> m_pLoopback` member and a `SetHostMode(GameServer&)` method. In `Send()`, if `m_pLoopback` is non-null, route through it; otherwise use the existing enet6 path. All message handler registration and `m_messageHandlers[]` array remain identical in both modes.

**When to use:** Callers (CharacterService, InventoryService, MagicService, etc.) call `m_transport.Send(msg)` as today. No changes needed in any caller.

**Trade-offs:** A single null pointer check per send. Alternatives (compile-time specialization, separate class per mode) add complexity for no practical gain at 2-4 player scale.

```cpp
// Proposed addition to TransportService.cpp
bool TransportService::Send(const ClientMessage& acMessage) const noexcept
{
    if (m_pLoopback)  // host mode: in-process delivery
    {
        m_pLoopback->SendToServer(acMessage);
        return true;
    }
    // client mode: existing enet6 path unchanged
    TiltedPhoques::Buffer buffer(1 << 16);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8);
    acMessage.Serialize(writer);
    TiltedPhoques::PacketView packet(
        reinterpret_cast<char*>(buffer.GetWriteData()), writer.Size());
    return Client::Send(&packet);
}
```

### Pattern 3: "Host Doesn't Send to Itself" via Broadcast Exclusion

**What:** `GameServer::SendToPlayers(const ServerMessage&, const Player* apExcludeSender)` already takes an exclusion parameter. When the server processes a message from `kHostConnectionId`, it calls `SendToPlayers(msg, pHostPlayer)`, skipping the loopback entirely for that broadcast. The host's client state is updated directly by the originating client service — not by receiving a server echo.

**When to use:** Every server broadcast that originates from host-side game state changes (position updates, NPC spawns, AI state, etc.).

**Trade-offs:** The host's client `CharacterService::OnReferencesMoveRequest` only processes entities with `RemoteComponent` — it already ignores local player. No additional guard needed. The exclusion in `SendToPlayers` prevents the echo. This keeps client services unaware of host mode.

### Pattern 4: HostService Ticking GameServer on Client Frame

**What:** `HostService::OnUpdate()` is connected to the client's `UpdateEvent` dispatcher. It calls `m_pGameServer->Update()` (which calls `TiltedPhoques::Server::Update()`, processing enet6 events, then `OnUpdate()` which fires the server's own `UpdateEvent` for server services).

**When to use:** Always. The host's Skyrim game loop drives both client and server ticks. `HostService` already implements this.

**Trade-offs:** Single-threaded execution — a slow server tick stalls the client frame. Acceptable at 2-4 player scale. A background thread would need mutex protection on the server World (not thread-safe), so defer until profiling shows a problem.

---

## Data Flow

### Host: Local Player Movement (complete pipeline)

```
Skyrim's game thread (via SKSE hook)
    -> CharacterService::OnUpdate() detects LocalComponent position dirty
    -> Creates ClientReferencesMoveRequest
    -> TransportService::Send(msg)
        [host mode: m_pLoopback != nullptr]
        -> LoopbackTransport::SendToServer(msg)
            -> serializes to buffer (same format as enet6)
            -> GameServer::InjectMessage(buffer, kHostConnectionId)
                -> GameServer::OnConsume()
                -> ClientMessageFactory::Extract() parses opcode
                -> m_messageHandlers[kReferencesMoveRequest](msg, kHostConnectionId)
                -> Server::CharacterService::OnReferencesMoveRequest()
                    -> updates authoritative MovementComponent in server World
                -> GameServer::SendToPlayers(ServerReferencesMoveRequest, excludeHostPlayer)
                    -> for each remote peer: enet6 Send()
                    -> loopback callback NOT called (host excluded)
```

### Remote Peer: Position Update Received

```
Remote peer receives ServerReferencesMoveRequest via enet6
    -> TiltedPhoques::Client::OnConsume() fires
    -> TransportService::OnConsume() (client mode)
    -> ServerMessageRouter::OnServerMessage()
    -> ServerMessageFactory::Extract() -> ServerReferencesMoveRequest
    -> m_messageHandlers[kServerReferencesMoveRequest](msg)
    -> CharacterService::OnReferencesMoveRequest()
    -> Updates InterpolationComponent.targetPos for RemoteComponent entity
    -> InterpolationSystem::Update() runs each frame
        -> glm::mix(currentPos, targetPos, alpha)  (kLerpSpeed=8.0)
    -> ApplyRemotePositions() writes smoothed position to Skyrim actor via hooks
```

### Remote Peer: Input Sent to Host

```
Remote peer's CharacterService detects local player movement
    -> Creates ClientReferencesMoveRequest
    -> TransportService::Send(msg)
        [client mode: m_pLoopback == nullptr]
        -> serializes + sends via enet6 to host IP:10578
            -> Host's GameServer::OnConsume() receives
            -> Processes, updates server World authoritative position
            -> GameServer::SendToPlayers(ServerReferencesMoveRequest, excludeRemotePeer)
                -> loopback callback fires for host player
                    -> Host's TransportService::OnConsume() receives bytes
                    -> ServerMessageRouter::OnServerMessage()
                    -> CharacterService::OnReferencesMoveRequest()
                    -> updates RemoteComponent entity for the remote peer's actor
                -> enet6 sends to all OTHER remote peers
```

### Host Startup Sequence

```
1. World::ctor() registers HostService in ctx()
2. User triggers "Host" (UI or auto-launch)
3. HostService::StartHosting(port=10578)
    a. Creates ConsoleRegistry
    b. Creates GameServer(console)
       -> GameServer::ctor() calls TiltedPhoques::Server::Host(port, tickRate)
       -> Binds enet6 UDP port
    c. Creates LoopbackTransport(gameServer)
    d. Registers loopback route:
       gameServer.SetLoopbackRoute(kHostConnectionId, [transport](data, size) {
           transport.OnConsume(data, size);  // inbound server->host path
       });
    e. Sets host mode on transport:
       world.GetTransport().SetHostMode(gameServer)  // stores m_pLoopback
    f. Sends AuthenticationRequest via loopback
       -> Host "authenticates" with its own embedded server
       -> PlayerService creates Player entity with IsHost=true flag
    g. HostService connects OnUpdate() to client UpdateEvent dispatcher
4. Each frame: HostService::OnUpdate() calls GameServer::Update()
5. Remote peers connect to host IP:10578 normally
```

### Client Fresh Join Sequence

```
1. Client TransportService::Connect("hostIP:10578")
2. TiltedPhoques::Client::OnConnected() fires
3. Client sends AuthenticationRequest (version, mod list)
4. GameServer::HandleAuthenticationRequest() validates, creates Player entity
5. GameServer sends AuthenticationResponse (accepted, localPlayerId)
6. GameServer sends full baseline:
   -> CharacterSpawnRequest for every currently-synced actor
7. Client CharacterService::OnCharacterSpawn() creates RemoteComponent entities
8. Client sends AssignCharacterRequest for local player
9. Normal sync: fast-path events + 1-second WorldStateSync reconciliation
```

---

## Scaling Considerations

| Players | Architecture | Notes |
|---------|-------------|-------|
| 1 (solo host, no peers) | Single-threaded, loopback only | Loopback fires but enet6 peer list is empty, zero network traffic |
| 2 (host + 1 peer) | Target case — single-threaded fine | All testing targets this configuration |
| 4 (host + 3 peers) | Single-threaded fine | 3 enet6 peers + 1 loopback |
| 8 (host + 7 peers) | Single-threaded starts to show latency | Server thread separation needed if server tick > 5ms |

### Scaling Priorities

1. **First bottleneck:** Server World iteration over all `OwnerComponent` entities per tick. At 4 players with 50 synced NPCs each = 200 entities. Well within single-thread budget at 60 tick.
2. **Second bottleneck:** `SendToPlayers` serializes the same message N times (once per peer). At 4 peers this is 4x serialization of the same bytes. Optimization: serialize once and memcpy to all peers. Irrelevant until profiling shows it.

---

## Anti-Patterns

### Anti-Pattern 1: True P2P World Merge (no loopback, no separate server World)

**What people do:** Eliminate `GameServer` entirely; make the host's client World directly authoritative; replace `GameServer` with a thin `NetworkBridge` that only broadcasts. This is what `P2P_ARCHITECTURE.md` describes as the end goal.

**Why it's wrong as a first step:** Every server service (`CharacterService`, `PlayerService`, `InventoryService`, etc.) is written against `ConnectionId_t`-keyed `PlayerManager`. All message handlers dispatch via `PacketEvent<T>(msg, pPlayer)`. Merging requires rewriting every handler and removing the `PlayerManager`. The loopback approach preserves all existing server validation logic and delivers two players in-game faster.

**Do this instead:** Implement loopback first. After the two-player session is stable, the World merge can be done incrementally one service at a time. The loopback is not a permanent architecture; it's the correct first step.

### Anti-Pattern 2: Running GameServer on a Background Thread

**What people do:** Start a `std::thread` for the server loop to avoid potential frame stalls.

**Why it's wrong:** The server `World` (entt::registry) is not thread-safe. `InjectMessage()` and the loopback send callback are both called from the client thread. Without mutexes around every registry access, concurrent access from client and server threads is a data race. The current design explicitly avoids this by calling server ticks from `HostService::OnUpdate()` on the same thread as the client.

**Do this instead:** Keep `HostService::OnUpdate()` ticking the server from the client thread. If profiling later shows the server tick is expensive, add a mutex-guarded SPSC command queue — but only when the problem is measured.

### Anti-Pattern 3: Per-Service Host-Mode Guards

**What people do:** Add `if (World::Get().GetHostService().IsHosting()) { /* skip send or take shortcut */ }` checks inside `CharacterService`, `InventoryService`, etc.

**Why it's wrong:** Scatters host-mode logic across every service. Creates divergent code paths that are difficult to test and audit. The host/client distinction belongs only in the transport layer.

**Do this instead:** All mode routing lives in `TransportService::Send()`. Services call `m_transport.Send(msg)` as today. The transport decides loopback vs enet6. Services are unaware of mode.

### Anti-Pattern 4: Localhost enet6 Connection for Host

**What people do:** Have the host's `TransportService` connect to `127.0.0.1:10578` via enet6 (the original STR architecture).

**Why it's wrong:** This is explicitly what `P2P_ARCHITECTURE.md` documents as the "current problem" — double serialization, double bandwidth, loopback enet6 latency, duplicate echo messages. The abandoned v1.0 relay DLL architecture similarly used TCP IPC and demonstrated the fragility cost. The in-process `LoopbackTransport` eliminates all of this at negligible cost.

**Do this instead:** `LoopbackTransport::SendToServer()` calls `GameServer::InjectMessage()` directly. No socket, no connection state, no latency.

### Anti-Pattern 5: Symmetric Message Echo for Host

**What people do:** Let `GameServer::SendToPlayers()` include the host's loopback in every broadcast, mirroring the dedicated-server behavior where the "server always echoes to all clients."

**Why it's wrong:** Host's client services originate changes; they already have the local state. Echoing the change back causes double-application in `CharacterService` and corrupts InterpolationComponent targets.

**Do this instead:** Pass `pHostPlayer` as `apExcludeSender` in `GameServer::SendToPlayers()` when the originator is the host. The exclusion parameter already exists — see `GameServer::SendToPlayers(const ServerMessage&, const Player* apExcludeSender)`.

---

## Integration Points

### Files That Require Changes

| File | Change | Phase |
|------|--------|-------|
| `Code/client/Services/TransportService.h` | Add `std::unique_ptr<LoopbackTransport> m_pLoopback`; add `void SetHostMode(Server::GameServer&) noexcept` | Phase 1 |
| `Code/client/Services/TransportService.cpp` | Add host-mode branch in `Send()`; wire loopback `DeliverToHost()` callback to `OnConsume()` | Phase 1 |
| `Code/client/Services/HostService.cpp` | Implement `StartHosting()`: create ConsoleRegistry + GameServer, create LoopbackTransport, register loopback route, call `TransportService::SetHostMode()`, send AuthenticationRequest | Phase 1 |
| `Code/client/World.cpp` | Verify `HostService` is in ctx() and `OnUpdate()` is connected to dispatcher | Phase 1 |

### Files to Port (native_client to client)

| Source | Destination | Notes |
|--------|-------------|-------|
| `Code/native_client/transport/LoopbackTransport.h/.cpp` | `Code/client/transport/LoopbackTransport.h/.cpp` | Remove `TcpClient` dependency; keep `InjectMessage` and loopback callback pattern unchanged |
| `Code/native_client/transport/ServerMessageRouter.h/.cpp` | `Code/client/transport/ServerMessageRouter.h/.cpp` | Pure deserialization + dispatch logic; no changes needed |

### Build System (already complete)

| What | Where | Status |
|------|-------|--------|
| `add_deps("SkyrimTogetherServer")` | `Code/client/xmake.lua` | Already present |
| `add_includedirs("../server", {public = false})` | `Code/client/xmake.lua` | Already present |
| Server static lib links server World + all services | `Code/server/xmake.lua` | Already present |

No XMake changes needed. The dependency graph is complete.

### Internal Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| `TransportService` (client) ↔ `GameServer` | `LoopbackTransport::SendToServer()` and loopback send callback | In-process, no socket, no thread boundary |
| `HostService` ↔ `GameServer` | Direct method calls (`Update()`, `Initialize()`, `Kill()`) | HostService owns GameServer via `unique_ptr` |
| Client `World` ↔ Server `World` | Zero shared entt entities; message protocol only | Two completely separate `entt::registry` instances |
| `GameServer` ↔ remote enet6 peers | `TiltedPhoques::Server` enet6 socket | Unchanged from current standalone server architecture |
| Server `CharacterService` ↔ Client `CharacterService` | Typed message protocol (`ServerReferencesMoveRequest`, `CharacterSpawnRequest`, etc.) | Opcode-based dispatch; unchanged |

---

## Phase Build Order

Based on integration dependencies:

1. **Port LoopbackTransport and ServerMessageRouter** into `Code/client/transport/` — no external dependencies, unit-testable standalone via replay harness.
2. **Add `SetHostMode()` to TransportService** — dual-mode `Send()` branch; depends on (1).
3. **Implement `HostService::StartHosting()`** — construct GameServer + ConsoleRegistry, create LoopbackTransport, register callback, call `SetHostMode`, send AuthenticationRequest; depends on (1) + (2).
4. **Wire HostService into World** — verify ctx() registration and `OnUpdate()` connection to dispatcher; depends on (3).
5. **Test loopback auth flow** (replay or integration test) — host authenticates with its own server, receives AuthenticationResponse + AssignCharacterResponse; depends on (4).
6. **Character sync and spawn pipeline** — CharacterService, InventoryService, etc. are unchanged; they work once the loopback transport delivers messages correctly.

---

## Stale References to Update

The following references in project docs use v1.0 terminology that no longer matches the current architecture:

- `NETWORK-ARCHITECTURE.md` interpolation pipeline section still mentions "relay DLL" and "native process" as component names. The principles (two-layer sync, interpolation, authority model) are correct; replace component names with `CharacterService` / `TransportService` / `LoopbackTransport`.
- `NETWORK-ARCHITECTURE.md` "Relay DLL Architecture" section describes the abandoned TCP IPC design. It should be replaced with the LoopbackTransport description.
- `P2P_ARCHITECTURE.md` describes the long-term merged-World goal correctly but should note that the loopback bridge is the v2.0 first step, not a final architecture.

---

## Sources

- `Code/client/Services/HostService.h` — embed lifecycle interface (HIGH confidence)
- `Code/client/Services/TransportService.h` — current network interface (HIGH confidence)
- `Code/client/World.h` — client context services, HostService already registered (HIGH confidence)
- `Code/server/GameServer.h` — `InjectMessage()`, `SetLoopbackRoute()`, `SendToPlayers()` APIs (HIGH confidence)
- `Code/server/GameServer.cpp` — constructor, `OnConsume()`, `OnDisconnection()`, `SendToPlayers()` implementations (HIGH confidence)
- `Code/native_client/transport/LoopbackTransport.h` — proven loopback pattern (HIGH confidence)
- `Code/native_client/transport/ServerMessageRouter.h/.cpp` — proven message routing pattern (HIGH confidence)
- `Code/client/xmake.lua` — build dependency graph, `add_deps("SkyrimTogetherServer")` confirmed (HIGH confidence)
- `.planning/NETWORK-ARCHITECTURE.md` — authority model, two-layer sync, interpolation pipeline (HIGH confidence)
- `P2P_ARCHITECTURE.md` — loopback vs merged-World trade-off rationale (HIGH confidence)

---
*Architecture research for: SkyrimCoop MinGW full client with embedded host-authoritative server*
*Researched: 2026-03-30*
