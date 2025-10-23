# SkyrimCoop Architecture Overview

This document provides a detailed outline of how the current codebase works, including the client-server communication flow, service architecture, and key systems. This is essential reading before converting to the P2P host-based model.

## Table of Contents
1. [High-Level Overview](#high-level-overview)
2. [Application Lifecycle](#application-lifecycle)
3. [Client-Server Communication Protocol](#client-server-communication-protocol)
4. [Service Architecture](#service-architecture)
5. [Entity Component System (ECS)](#entity-component-system-ecs)
6. [Key Data Flows](#key-data-flows)
7. [Network Message System](#network-message-system)
8. [UI Integration](#ui-integration)

---

## High-Level Overview

### Current Architecture: Client-Server Model

```
┌─────────────────┐         UDP (GameNetworkingSockets)        ┌─────────────────┐
│  Skyrim Client  │ ◄──────────────────────────────────────► │  GameServer     │
│  (SKSE Plugin)  │                                             │  (Standalone)   │
├─────────────────┤                                             ├─────────────────┤
│ TransportService│                                             │ PlayerService   │
│ CharacterService│                                             │ CharacterService│
│ InventoryService│                                             │ InventoryService│
│ QuestService    │                                             │ QuestService    │
│ MagicService    │                                             │ ScriptService   │
│ PartyService    │                                             │ AdminService    │
│ ...             │                                             │ ...             │
└─────────────────┘                                             └─────────────────┘
         │                                                               │
         │                                                               │
    ┌────▼────┐                                                     ┌────▼────┐
    │  World  │ (entt::registry)                                    │  World  │ (entt::registry)
    │  - ECS  │                                                     │  - ECS  │
    └─────────┘                                                     └─────────┘
```

**Key Concept:** Server is the **source of truth** for:
- Actor ownership and state
- World state synchronization
- Combat resolution
- Quest progression (optional)
- Party management

---

## Application Lifecycle

### Client Startup (Code/client/main.cpp)

1. **SKSE Plugin Load** (`SKSEPluginLoad()`)
   - Skyrim loads the DLL via SKSE
   - Version database loaded from Address Library
   - `TiltedOnlineApp` instance created

2. **TiltedOnlineApp::InstallHooks2()**
   - Hooks into Skyrim's game engine functions
   - Intercepts events (actor spawn, item pickup, spell cast, etc.)

3. **TiltedOnlineApp::BeginMain()**
   - Initializes all client services
   - Creates `World` (ECS registry)
   - Starts `TransportService` (network client)
   - Initializes UI overlay (CEF)
   - Begins update loop

### Server Startup (Code/server/main.cpp)

1. **CreateGameServer()** (exported DLL function)
   - Server runner loads `libSTServer.so` / `STServer.dll`
   - Creates `GameServerInstance` wrapper

2. **GameServerInstance::Initialize()**
   - `GameServer` extends `TiltedPhoques::Server`
   - Binds network socket (default UDP port 10578)
   - Loads server configuration (INI file)
   - Initializes Lua scripting engine (Sol2)
   - Creates all server services
   - Starts listening for connections

3. **GameServerInstance::Update()** (tick loop)
   - `GameServer::Update()` called every frame
   - Processes incoming packets
   - Updates all service systems
   - Broadcasts state changes to clients

---

## Client-Server Communication Protocol

### Transport Layer

**Technology:** Valve's GameNetworkingSockets (UDP-based, reliable ordered messages)

**Client:** `TransportService` extends `TiltedPhoques::Client`
**Server:** `GameServer` extends `TiltedPhoques::Server`

### Message Flow Pattern

```
┌──────────┐                                    ┌──────────┐
│  Client  │                                    │  Server  │
└────┬─────┘                                    └────┬─────┘
     │                                                │
     │  1. AssignCharacterRequest                    │
     │  ─────────────────────────────────────────►   │
     │     (Cookie, FormId, Position, ActorData)     │
     │                                                │
     │                    2. Server validates         │
     │                       Creates entity in World  │
     │                       Assigns ServerId         │
     │                                                │
     │  3. AssignCharacterResponse                   │
     │  ◄─────────────────────────────────────────   │
     │     (Cookie, ServerId, Owner=true/false)      │
     │                                                │
     │  4. ClientReferencesMoveRequest               │
     │  ─────────────────────────────────────────►   │
     │     (ServerId[], Position[], Rotation[])      │
     │                                                │
     │                    5. Server updates positions │
     │                       Broadcasts to nearby     │
     │                                                │
     │  6. ServerReferencesMoveRequest               │
     │  ◄─────────────────────────────────────────   │
     │     (ServerId[], Position[], Rotation[])      │
     │     (broadcast to all nearby clients)         │
     │                                                │
     │  7. Apply remote position updates             │
     │     Trigger interpolation system              │
     │                                                │
```

### Key Message Types

**Client → Server (58 opcodes):**

| Message | Purpose | Frequency |
|---------|---------|-----------|
| `AuthenticationRequest` | Initial connection, send credentials | Once on connect |
| `AssignCharacterRequest` | Request ownership of an actor | When actor spawns |
| `ClientReferencesMoveRequest` | Movement updates | High frequency (every frame with changes) |
| `RequestActorValueChanges` | Health/magicka/stamina updates | On change |
| `RequestInventoryChanges` | Equipment/item changes | On change |
| `SpellCastRequest` | Spell casting | On cast |
| `EnterInteriorCellRequest` | Cell transition | On load door |
| `PartyInviteRequest` | Party management | User action |
| `RequestQuestUpdate` | Quest progress (optional) | On objective change |

**Server → Client (61 opcodes):**

| Message | Purpose | Frequency |
|---------|---------|-----------|
| `AuthenticationResponse` | Confirm connection, assign PlayerId | Once on connect |
| `AssignCharacterResponse` | Grant ownership, provide ServerId | On assignment |
| `CharacterSpawnRequest` | Spawn remote player | When player enters range |
| `ServerReferencesMoveRequest` | Movement broadcast | High frequency |
| `NotifyActorValueChanges` | Health/resource updates | On change |
| `NotifyInventoryChanges` | Equipment sync | On change |
| `NotifySpellCast` | Spell casting events | On cast |
| `NotifyPlayerList` | Online players | Periodic / on change |
| `NotifyPartyInfo` | Party state | On change |
| `NotifyRemoveCharacter` | Despawn remote player | When player leaves range |
| `NotifySettingsChange` | Server config updates | On admin change |

### Serialization

**Raw Serialization:**
```cpp
void AssignCharacterRequest::SerializeRaw(Buffer::Writer& aWriter) const {
    aWriter.WriteBits(Cookie, 32);
    ReferenceId.Serialize(aWriter);
    FormId.Serialize(aWriter);
    Position.Serialize(aWriter);
    // ... serialize all fields
}
```

**Differential Serialization (optimization):**
```cpp
void ActorValues::SerializeDifferential(Buffer::Writer& aWriter) const {
    // Only serialize changed values
    aWriter.WriteBits(m_changedFlags, 16);
    if (m_changedFlags & kHealthChanged)
        aWriter.WriteFloat(Health);
    // ... only serialize changed fields
}
```

---

## Service Architecture

### Client Services (Code/client/Services/)

Services register event handlers and process game state:

```cpp
// Example: CharacterService constructor
CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport)
    : m_world(aWorld), m_dispatcher(aDispatcher), m_transport(aTransport)
{
    // Listen to game events
    m_referenceAddedConnection =
        aDispatcher.sink<ActorAddedEvent>().connect<&CharacterService::OnActorAdded>(this);

    // Listen to network messages
    m_assignCharacterConnection =
        aDispatcher.sink<AssignCharacterResponse>().connect<&CharacterService::OnAssignCharacter>(this);

    // Listen to update tick
    m_updateConnection =
        aDispatcher.sink<UpdateEvent>().connect<&CharacterService::OnUpdate>(this);
}
```

**Key Client Services:**

| Service | Responsibility |
|---------|---------------|
| `TransportService` | Network communication, message routing |
| `CharacterService` | Actor spawning, movement, animation sync |
| `InventoryService` | Equipment and item synchronization |
| `ActorValueService` | Health, magicka, stamina tracking |
| `MagicService` | Spell casting, effects |
| `CombatService` | Combat state, target tracking |
| `QuestService` | Quest objective tracking (optional) |
| `PartyService` | Group management, waypoint sharing |
| `CalendarService` | Time synchronization |
| `WeatherService` | Weather synchronization |
| `OverlayService` | CEF UI lifecycle management |
| `ImguiService` | Debug UI overlay |
| `PapyrusService` | Papyrus script function registration |

### Server Services (Code/server/Services/)

Services maintain authoritative game state:

```cpp
// Example: CharacterService message handler
void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const {
    auto& message = acMessage.GetMessage();
    Player* pPlayer = acMessage.GetSender();

    // Create entity in server's World
    entt::entity entity = m_world.create();

    // Apply actor data from client
    ApplyActorData(entity, message.CurrentActorData);

    // Assign server ID and ownership
    uint32_t serverId = m_world.GetNextServerId();
    m_world.emplace<ServerIdComponent>(entity, serverId);
    m_world.emplace<OwnerComponent>(entity, pPlayer->GetConnectionId());

    // Send response to client
    AssignCharacterResponse response;
    response.Cookie = message.Cookie;
    response.ServerId = serverId;
    response.Owner = true;
    pPlayer->Send(response);

    // Broadcast spawn to nearby players
    BroadcastActorData(pPlayer, entity, message.CurrentActorData);
}
```

**Key Server Services:**

| Service | Responsibility |
|---------|---------------|
| `PlayerService` | Connection management, authentication |
| `CharacterService` | Authoritative actor state, spawn/despawn |
| `InventoryService` | Item ownership, crafting, loot |
| `ActorValueService` | Authoritative health/resource values |
| `MagicService` | Spell validation, effect processing |
| `CombatService` | Combat rules, damage calculation |
| `QuestService` | Quest state tracking (optional sync) |
| `PartyService` | Party membership, permissions |
| `CalendarService` | Authoritative in-game time |
| `WeatherService` | Authoritative weather state |
| `MapService` | Cell and worldspace management |
| `ScriptService` | Lua scripting engine |
| `AdminService` | Administrative commands |
| `CommandService` | Console command processing |

---

## Entity Component System (ECS)

### World Class

```cpp
struct World : entt::registry {
    // Inherits all entt::registry functionality
    // Provides entity creation, component management, views
};
```

### Component Examples

**Client Components** (`Code/client/Components/`):

```cpp
struct LocalComponent {
    // Marks entities controlled by this client
    uint32_t FormId;
};

struct RemoteComponent {
    // Marks entities controlled by remote clients
    uint32_t ServerId;
    uint32_t PlayerId;
};

struct InterpolationComponent {
    // Smooths movement between network updates
    Vector3 StartPosition;
    Vector3 TargetPosition;
    float TimeElapsed;
    float Duration;
};

struct ActorValuesComponent {
    float Health;
    float Magicka;
    float Stamina;
    // ... other stats
};
```

**Server Components** (`Code/server/Components/`):

```cpp
struct ServerIdComponent {
    uint32_t Id; // Unique ID assigned by server
};

struct OwnerComponent {
    ConnectionId_t ConnectionId; // Which player owns this entity
};

struct CharacterComponent {
    GameId FormId;
    GameId CellId;
    Vector3_NetQuantize Position;
    Rotator2_NetQuantize Rotation;
    ActorData Data;
};

struct InventoryComponent {
    Inventory Items;
    uint32_t Gold;
};
```

### ECS Query Examples

**Client: Find all local actors that moved:**
```cpp
auto view = m_world.view<LocalComponent, FormIdComponent, MovementComponent>();
for (auto entity : view) {
    auto [local, formId, movement] = view.get(entity);
    if (movement.HasMoved()) {
        // Send position update to server
        SendMovementUpdate(formId.Id, movement.Position);
    }
}
```

**Server: Broadcast to players in range:**
```cpp
void GameServer::SendToPlayersInRange(const ServerMessage& acMessage, entt::entity acOrigin) const {
    auto originPos = m_world.get<CharacterComponent>(acOrigin).Position;

    auto players = m_world.view<PlayerComponent, CharacterComponent>();
    for (auto player : players) {
        auto playerPos = m_world.get<CharacterComponent>(player).Position;
        if (Distance(originPos, playerPos) < kBroadcastRange) {
            auto* pPlayer = m_world.get<PlayerComponent>(player).GetPlayer();
            pPlayer->Send(acMessage);
        }
    }
}
```

---

## Key Data Flows

### 1. Player Connection Flow

```
Client                                  Server
  │                                       │
  │ 1. Connect via UDP                   │
  │ ─────────────────────────────────►   │
  │                                       │
  │ 2. AuthenticationRequest             │
  │    - Username                         │
  │    - Password (if required)           │
  │    - Mod list                         │
  │ ─────────────────────────────────►   │
  │                                       │
  │                  3. Validate credentials
  │                     Check mod compatibility
  │                     Assign PlayerId
  │                                       │
  │ 4. AuthenticationResponse            │
  │    - PlayerId                         │
  │    - ServerInfo (name, tick rate)     │
  │    - Settings                         │
  │ ◄─────────────────────────────────   │
  │                                       │
  │ 5. NotifyPlayerList                  │
  │    - List of online players           │
  │ ◄─────────────────────────────────   │
  │                                       │
  │ 6. Player loads into game            │
  │    Actors spawn in world              │
  │    AssignCharacterRequest sent        │
  │                                       │
```

### 2. Actor Spawn Flow

**Scenario:** Player encounters an NPC in Skyrim

```
Client                                  Server
  │                                       │
  │ 1. Skyrim spawns actor                │
  │    FormId = 0x00013478               │
  │    Position = (100, 200, 50)         │
  │                                       │
  │ 2. ActorAddedEvent fired             │
  │    CharacterService::OnActorAdded    │
  │                                       │
  │ 3. Check if actor should sync        │
  │    - Is it a valid NPC?               │
  │    - Not already synced?              │
  │                                       │
  │ 4. Create entity in World            │
  │    Add LocalComponent                 │
  │    Add WaitingForAssignmentComponent  │
  │                                       │
  │ 5. AssignCharacterRequest            │
  │    - Cookie (for matching response)   │
  │    - FormId, Position, Rotation       │
  │    - ActorData (appearance, faction)  │
  │ ─────────────────────────────────►   │
  │                                       │
  │            6. Server validates request
  │               Creates entity
  │               Assigns ServerId = 42
  │               Checks ownership rules
  │                                       │
  │ 7. AssignCharacterResponse           │
  │    - Cookie (matches request)         │
  │    - ServerId = 42                    │
  │    - Owner = true                     │
  │    - ActorValues (health, etc.)       │
  │ ◄─────────────────────────────────   │
  │                                       │
  │ 8. Store ServerId in entity          │
  │    Remove WaitingForAssignment        │
  │    Apply server's ActorValues         │
  │                                       │
  │            9. Broadcast to nearby players
  │               CharacterSpawnRequest
  │                                       │
  │ 10. Other clients receive spawn      │
  │     Create remote actor entity        │
  │     Add RemoteComponent               │
  │                                       │
```

### 3. Movement Synchronization Flow

**Client-Authoritative Movement** (reduces latency):

```
Client A (Owner)                Server                  Client B (Observer)
     │                            │                           │
     │ 1. Player moves            │                           │
     │    Position changes         │                           │
     │                            │                           │
     │ 2. Update loop detects     │                           │
     │    RunLocalUpdates()        │                           │
     │                            │                           │
     │ 3. ClientReferencesMoveReq │                           │
     │    ServerId = 42            │                           │
     │    Position = (105,205,50)  │                           │
     │ ───────────────────────►   │                           │
     │                            │                           │
     │                            │ 4. Validate movement      │
     │                            │    Update entity position  │
     │                            │                           │
     │                            │ 5. ServerReferencesMoveReq│
     │                            │    ServerId = 42          │
     │                            │    Position = (105,205,50)│
     │                            │ ─────────────────────────►│
     │                            │                           │
     │                            │                           │ 6. Apply position
     │                            │                           │    Start interpolation
     │                            │                           │    Smooth movement
```

### 4. Inventory/Equipment Flow

```
Client                                  Server
  │                                       │
  │ 1. Player equips sword                │
  │    EquipmentChangeEvent fired         │
  │                                       │
  │ 2. InventoryService detects           │
  │    Build equipment delta              │
  │                                       │
  │ 3. RequestInventoryChanges           │
  │    - ServerId                         │
  │    - Items added: [Sword +1]          │
  │    - Items removed: []                │
  │    - Equipped: [Sword +1]             │
  │ ─────────────────────────────────►   │
  │                                       │
  │            4. Server validates
  │               - Player owns item?
  │               - Item exists in inv?
  │               Update InventoryComponent
  │                                       │
  │ 5. NotifyInventoryChanges            │
  │    (broadcast to nearby/party)        │
  │ ◄─────────────────────────────────   │
  │                                       │
  │ 6. Other clients update visuals      │
  │    Remote actor now holds sword       │
  │                                       │
```

### 5. Spell Casting Flow

```
Client                                  Server
  │                                       │
  │ 1. Player casts fireball              │
  │    SpellCastEvent fired               │
  │                                       │
  │ 2. MagicService::OnSpellCast         │
  │    SpellCastRequest                   │
  │    - ServerId                         │
  │    - SpellId                          │
  │    - TargetServerId                   │
  │    - CastTime                         │
  │ ─────────────────────────────────►   │
  │                                       │
  │            3. Server validates
  │               - Has magicka?
  │               - Spell known?
  │               Process spell effect
  │               Deduct magicka
  │                                       │
  │ 4. NotifySpellCast                   │
  │    (broadcast to nearby)              │
  │ ◄─────────────────────────────────   │
  │                                       │
  │ 5. Other clients play animation      │
  │    Show spell effect visuals          │
  │                                       │
```

---

## Network Message System

### Message Handler Registration

**Client (TransportService.cpp):**
```cpp
TransportService::TransportService(...) {
    // Register handlers for server messages
    m_messageHandlers[kAuthenticationResponse] = [this](auto& msg) {
        HandleAuthenticationResponse(*reinterpret_cast<AuthenticationResponse*>(msg.get()));
    };

    m_messageHandlers[kAssignCharacterResponse] = [this](auto& msg) {
        m_dispatcher.trigger(*reinterpret_cast<AssignCharacterResponse*>(msg.get()));
    };

    // ... 61 total handlers for all server opcodes
}
```

**Server (GameServer.cpp):**
```cpp
void GameServer::BindMessageHandlers() {
    // Register handlers for client messages
    m_messageHandlers[kAuthenticationRequest] = [this](auto conn, auto& msg) {
        HandleAuthenticationRequest(conn, *reinterpret_cast<AuthenticationRequest*>(msg.get()));
    };

    m_messageHandlers[kAssignCharacterRequest] = [this](auto conn, auto& msg) {
        m_dispatcher.trigger(PacketEvent{conn, *reinterpret_cast<AssignCharacterRequest*>(msg.get())});
    };

    // ... 58 total handlers for all client opcodes
}
```

### Message Processing Loop

**Client:**
```cpp
void TransportService::OnConsume(const void* apData, uint32_t aSize) {
    Buffer::Reader reader(apData, aSize);

    ServerOpcode opcode;
    reader.ReadBits(opcode, 8);

    auto& handler = m_messageHandlers[opcode];
    if (handler) {
        auto message = ServerMessage::CreateFromOpcode(opcode);
        message->Deserialize(reader);
        handler(message);
    }
}
```

**Server:**
```cpp
void GameServer::OnConsume(ConnectionId_t aConnectionId, const void* apData, uint32_t aSize) {
    Buffer::Reader reader(apData, aSize);

    ClientOpcode opcode;
    reader.ReadBits(opcode, 8);

    auto& handler = m_messageHandlers[opcode];
    if (handler) {
        auto message = ClientMessage::CreateFromOpcode(opcode);
        message->Deserialize(reader);
        handler(aConnectionId, message);
    }
}
```

---

## UI Integration

### CEF (Chromium Embedded Framework) Architecture

```
┌──────────────────────────────────┐
│  Skyrim Game Process             │
│  ┌────────────────────────────┐  │
│  │ SkyrimTogetherClient.dll   │  │
│  │  ┌──────────────────────┐  │  │
│  │  │  OverlayService      │  │  │
│  │  │  - Initializes CEF   │  │  │
│  │  │  - Renders overlay   │  │  │
│  │  └──────────┬───────────┘  │  │
│  └─────────────┼──────────────┘  │
│                │ IPC              │
│  ┌─────────────▼──────────────┐  │
│  │  tp_process.exe            │  │
│  │  (CEF Worker Process)      │  │
│  │  ┌──────────────────────┐  │  │
│  │  │  Angular App         │  │  │
│  │  │  - Chat UI           │  │  │
│  │  │  - Player List       │  │  │
│  │  │  - Settings Panel    │  │  │
│  │  └──────────────────────┘  │  │
│  └─────────────────────────────┘  │
└──────────────────────────────────┘
```

### Client ↔ UI Communication

**C++ to JavaScript:**
```cpp
// OverlayService.cpp
void OverlayService::SendMessageToUI(const std::string& aType, const nlohmann::json& aData) {
    auto message = nlohmann::json{
        {"type", aType},
        {"data", aData}
    };
    m_browser->GetMainFrame()->ExecuteJavaScript(
        "window.dispatchEvent(new CustomEvent('fromClient', {detail: " + message.dump() + "}))"
    );
}
```

**JavaScript to C++:**
```typescript
// client.service.ts
export class ClientService {
  sendMessage(type: string, data: any) {
    // CEF intercepts this and routes to C++
    (window as any).cefQuery({
      request: JSON.stringify({ type, data }),
      onSuccess: (response) => { /* ... */ },
      onFailure: (error) => { /* ... */ }
    });
  }
}
```

### UI State Management

**NgELF Store** (reactive state):
```typescript
// store.service.ts
@Injectable()
export class StoreService {
  private store = createStore({ name: 'skyrimcoop' });

  playerList$ = this.store.pipe(select(state => state.players));
  isConnected$ = this.store.pipe(select(state => state.connected));

  updatePlayerList(players: Player[]) {
    this.store.update(state => ({ ...state, players }));
  }
}
```

---

## Critical Insights for P2P Conversion

### Current Ownership Model

**Problem:** Server arbitrates all ownership and state
- Server assigns `ServerId` to every entity
- Server decides who owns each actor
- Ownership can transfer between players dynamically

**P2P Solution:** Host always owns NPCs/world state
- Simplify to: Host owns all non-player entities
- Clients only own their player character
- Remove ownership transfer logic

### Current Cell Management

**Problem:** Multiple players can load different cells
- Server tracks which players are in which cells
- Broadcasts updates only to nearby players
- Complex cell handoff logic

**P2P Solution:** Host's loaded cells are authoritative
- Only sync actors in host's loaded cells
- Clients who wander too far see desync (acceptable for co-op)
- Simplify broadcast to "all connected clients"

### Current Quest System

**Problem:** Optional sync, server tracks progress
- Each player can be on different quest stages
- Server stores quest state per player
- Complex merge logic

**P2P Solution:** Host's quests are authoritative
- All players sync to host's quest progress
- Simpler implementation, better co-op experience
- Matches player expectation (host is "DM")

### Current Authentication

**Problem:** Server validates credentials, mod lists
- Requires server infrastructure
- Password protection
- Admin privileges

**P2P Solution:** Use platform friend system
- Steam/EOS lobby invites
- No password needed (friend-based)
- Host has all admin privileges by default

---

## Next Steps for P2P Conversion

Based on this architecture, the conversion should:

1. **Embed GameServer in Client:**
   - Create `HostService` that instantiates `GameServer`
   - Run server update loop in client thread
   - localhost connection from client to embedded server

2. **Simplify Ownership:**
   - Remove `OwnerComponent` logic
   - Host always owns NPCs
   - Remove ownership transfer messages

3. **Simplify Broadcasting:**
   - Remove cell-based filtering
   - Broadcast to all connected clients
   - Accept some bandwidth increase for simplicity

4. **Force Quest Sync:**
   - Enable quest sync by default
   - Remove per-player quest state
   - Use host's quest progress

5. **Replace Authentication:**
   - Remove password system
   - Use Steam/EOS friend invites
   - Direct P2P connections

See CLAUDE.md for detailed conversion strategy.
