# Architecture

**Analysis Date:** 2026-03-27

## Pattern Overview

**Overall:** Entity-Component System (ECS) with Service-Oriented Architecture and Message-Based Networking

**Key Characteristics:**
- EnTT 3.10.0 registry-based ECS for all actor and object state
- Message-driven request/response pattern for client-server communication
- Event-dispatcher for internal service communication
- Dual-architecture: Client plugin (SKSE) and standalone server
- Host-based P2P model: Host player runs embedded server, others connect directly

## Layers

**Network Transport Layer:**
- Purpose: Handles UDP communication via GameNetworkingSockets + TiltedPhoques protocols
- Location: `Code/libraries/networking/`
- Contains: `Client.hpp`, `Server.hpp`, packet serialization, connection management
- Depends on: GameNetworkingSockets library, TiltedPhoques base classes
- Used by: Client `TransportService`, Server `GameServer`

**Message Encoding Layer:**
- Purpose: Serialize/deserialize game state updates with bandwidth optimization
- Location: `Code/encoding/`
- Contains: `Messages/` (58 client opcodes, 61 server opcodes), `Structs/` (shared data types)
- Depends on: TiltedPhoques::Buffer, math libraries (GLM)
- Used by: Transport layer, Services for message creation/handling

**Service Layer (Client):**
- Purpose: Manages game system synchronization and local state
- Location: `Code/client/Services/`
- Contains: CharacterService, InventoryService, QuestService, MagicService, OverlayService, etc.
- Depends on: World registry, TransportService, event dispatcher
- Used by: World (context), Systems, message handlers

**Service Layer (Server):**
- Purpose: Authoritative game state management and player coordination
- Location: `Code/server/Services/`
- Contains: PlayerService, CharacterService, PartyService, ScriptService, CalendarService, etc.
- Depends on: World registry, event dispatcher
- Used by: GameServer, message handlers, admin interface

**Component Layer:**
- Purpose: Data containers for ECS entities
- Location: `Code/client/Components/` and `Code/server/Components/`
- Contains: LocalComponent, RemoteComponent, InterpolationComponent, AnimationComponents, etc.
- Depends on: entt::registry
- Used by: Services, Systems via entt views

**System Layer:**
- Purpose: Game logic iteration over component combinations
- Location: `Code/client/Systems/`
- Contains: InterpolationSystem, AnimationSystem, FaceGenSystem, RenderSystemD3D11, etc.
- Depends on: Components, World registry
- Used by: World::Update(), Services

**Game Integration Layer:**
- Purpose: Skyrim-specific hooks, memory patching, RTTI introspection
- Location: `Code/client/Games/Skyrim/`
- Contains: SKSE plugin interface, form lookups, event handling, behavior modification
- Depends on: SKSE SDK, Skyrim executable structures
- Used by: Client services for actor manipulation

**UI Layer:**
- Purpose: In-game web-based overlay and launcher
- Location: `Code/skyrim_ui/` (Angular), `Code/libraries/ui/` (CEF integration), `Code/tp_process/` (worker process)
- Contains: Angular components, CEF client, UI IPC bridge
- Depends on: Angular 16, CEF, D3D11 hooks
- Used by: OverlayService, player interactions

**Admin & Scripting Layer:**
- Purpose: Server administration and Lua-based custom game logic
- Location: `Code/server/Services/ScriptService`, `Code/server/Scripting/`
- Contains: Lua 5.1 bindings via Sol2, admin commands
- Depends on: Sol2, spdlog
- Used by: Server for extending game rules without recompilation

## Data Flow

**Actor Movement Sync (Primary Flow):**

1. Client detects position change via Skyrim form hooks
2. `CharacterService::OnActorAdded()` processes new actors
3. Local actor spawned in client World registry as `entt::entity` with `LocalComponent`
4. Movement tracked via Skyrim event listener
5. `ClientReferencesMoveRequest` created with `Vector3_NetQuantize` (64-bit packed positions)
6. `TransportService::Send()` serializes message via differential encoding
7. UDP packet transmitted to server
8. Server `CharacterService::OnReferencesMoveRequest()` receives packet
9. Server updates World registry entity position
10. Server broadcasts `ServerReferencesMoveRequest` to nearby clients (cell-based culling)
11. Remote clients receive broadcast in `TransportService::OnConsume()`
12. `CharacterService::OnReferencesMoveRequest()` updates remote entity position
13. `InterpolationSystem` creates smooth motion from snapshot points
14. `AnimationSystem` syncs animation variables from `RemoteAnimationComponent`

**State Management:**

Client World:
- Maintains local player's loaded cells and actors
- Mirrors remote players and their actors via network updates
- Separates `LocalComponent` (player-controlled) from `RemoteComponent` (synced)

Server World:
- Authoritative registry of all player actors
- Maintains party/group state via `PartyService`
- Quest/skill progress owned by PlayerService
- NPC state synchronized on demand per cell

**Event Flow (Internal Communication):**

1. Skyrim event occurs (e.g., actor death, quest update)
2. SKSE hook captures event in `Games/Skyrim/Events/`
3. Game-specific handler dispatches to entt::dispatcher
4. Relevant service subscribes via `m_dispatcher.sink<EventType>().connect<&Service::OnEvent>()`
5. Service processes event, may create network message
6. `TransportService` queues message for sending
7. Server receives, dispatches server-side event
8. Other services handle server event (e.g., PartyService for group notification)

## Key Abstractions

**World (Client):**
- Purpose: Central registry combining ECS + services
- Examples: `Code/client/World.h`
- Pattern: Inherits `entt::registry`, stores dispatcher and all services in context
- Access: `World::Get()` singleton pattern
- Services accessed via `GetCharacterService()`, `GetTransport()`, etc.

**World (Server):**
- Purpose: Server-side authoritative registry
- Examples: `Code/server/World.h` in Server namespace
- Pattern: Inherits `entt::registry`, manages player manager and scripting
- Access: Passed to all services during initialization

**TransportService (Client):**
- Purpose: Network communication gateway
- Examples: `Code/client/Services/TransportService.h`
- Pattern: Extends `TiltedPhoques::Client`, acts as transport abstraction
- Methods: `Send()` for outgoing messages, `OnConsume()` for incoming
- Message handlers: Array of function pointers indexed by opcode

**CharacterService (Dual):**
- Purpose: Actor/player state synchronization
- Examples: `Code/client/Services/CharacterService.h`, `Code/server/Services/CharacterService.h`
- Pattern: Subscription-based event handling, message request/response pairs
- Client responsibilities: Local ownership, movement detection, animation interpolation
- Server responsibilities: Authoritative state, ownership assignment, broadcast to players

**Message Factory:**
- Purpose: Opcode-driven deserialization of binary network data
- Examples: `Code/encoding/Messages/ClientMessageFactory.h`, `ServerMessageFactory.h`
- Pattern: Extract opcode from reader, instantiate correct message type, deserialize payload
- Used by: `TransportService::OnConsume()` to parse incoming packets

**Components (ECS Data):**
- Purpose: Pure data containers with no behavior
- Examples: `LocalComponent`, `RemoteComponent`, `InterpolationComponent`
- Pattern: Simple structs with public members, initialized in services
- Storage: In World registry via `registry.emplace<ComponentType>(entity, ...)`

**Systems (ECS Iteration):**
- Purpose: Logic that operates on component combinations
- Examples: `InterpolationSystem`, `AnimationSystem`
- Pattern: Static methods taking World, component types, and tick/delta parameters
- Invocation: `World::Update()` calls systems in sequence

## Entry Points

**Client:**
- Location: `Code/client/main.cpp`
- Triggers: SKSE plugin load on Skyrim startup
- Responsibilities: Initialize address library, load game hooks, instantiate TiltedOnlineApp, begin main loop
- Creates: World singleton, initializes all services via World constructor

**Server (Embedded in Host Client):**
- Location: `Code/client/Services/HostService.h` (proposed/partial)
- Triggers: When client selects "Host" mode instead of connecting
- Responsibilities: Instantiate `GameServer`, listen for client connections
- Creates: Server World, services, player manager

**Server (Standalone):**
- Location: `Code/server_runner/` executable
- Triggers: Launched as dedicated process
- Responsibilities: Initialize server, load mod database, bind to port
- Creates: Server World, services, scripting engine

**UI Entry:**
- Location: `Code/skyrim_ui/src/main.ts` (Angular)
- Triggers: OverlayService initializes CEF browser
- Responsibilities: Bootstrap Angular app, initialize state management
- Communicates with: C++ via `tp_process` IPC bridge

## Error Handling

**Strategy:** Graceful degradation with recovery attempts and logging

**Patterns:**

Network Errors (TransportService):
- Connection loss triggers `OnDisconnected(EDisconnectReason)` event
- Services unsubscribe from dispatcher
- Client respawns local player, clears remote entities
- Logs via spdlog at ERROR level

Serialization Errors (Message parsing):
- Invalid opcode in `ClientMessageFactory` returns nullptr
- `OnConsume()` logs warning, discards packet
- No exception thrown (avoid crash in network thread)

State Desync (CharacterService):
- Server doesn't find entity for movement request: logs warning, updates on next valid sync
- Client receives actor deletion while still owning: cancels assignment, spawns as remote
- Factions/quest data mismatch: server is authoritative, client accepts sync

Lua Errors (ScriptService):
- Sol2 catches Lua runtime errors, logs stack trace
- Continues server execution, script function may return nil
- Admin notified via admin panel

## Cross-Cutting Concerns

**Logging:**
- Framework: spdlog 1.13.0 with structured logging
- Pattern: `Log(Info|Warning|Error|Debug)(Category, Message)` in services
- Configuration: Runtime level adjustment via console/admin panel

**Validation:**
- Pattern: Check form IDs, cell coordinates, and numeric ranges at service boundaries
- Server validates all client data before applying (don't trust clients)
- Client validates server responses to prevent crashes on corrupt data

**Authentication:**
- Mechanism: `AuthenticationRequest` with server password
- Pattern: `PlayerService::OnAuthenticate()` verifies before adding player
- Host-P2P mode: Uses Steam/EOS friend invite instead of password

**Serialization Modes:**
- `SerializeRaw()`: Full baseline state for new connections or periodic sync
- `SerializeDifferential()`: Delta encoding for bandwidth optimization (e.g., only changed fields)
- Quantization: `Vector3_NetQuantize` compresses float positions into 64-bit integers

---

*Architecture analysis: 2026-03-27*
