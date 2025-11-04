# P2P Architecture Refactor Plan

## Core Problem

The current architecture treats the host as a client connecting to an embedded server. This causes:
- Redundant localhost networking
- Duplicate state (client World + server World)
- Unnecessary serialization/deserialization
- Complexity in every system

## Solution

**Host never connects to their own NetworkBridge. Host directly owns the World, NetworkBridge just forwards updates to peers.**

## Architecture Transformation

### Current Structure

```
Host Process:
├─ World (client-side)
│   └─ TransportService → connects to localhost:10578
├─ Server::GameServer (embedded)
│   ├─ Server::World (authoritative)
│   └─ Processes messages from clients
└─ All systems duplicated (client + server versions)

Peer Process:
├─ World (client-side)
│   └─ TransportService → connects to host IP
└─ Receives updates from server
```

### Target Structure

```
Host Process:
├─ World (THE authoritative state)
│   ├─ All game systems (CharacterService, InventoryService, etc.)
│   └─ NO TransportService (doesn't connect to itself!)
└─ NetworkBridge
    ├─ Listens for peer connections
    ├─ Receives peer inputs → applies to World
    └─ Broadcasts World changes → sends to peers

Peer Process:
├─ World (local replica)
│   └─ All game systems (receive-only mode)
└─ NetworkClient
    ├─ Connects to host IP
    ├─ Sends inputs to host
    └─ Receives updates from host
```

## Major Changes Required

### 1. Remove Server::World Concept

**Current:**
- `Code/server/World.h` - Separate server-side World
- `Code/client/World.h` - Separate client-side World

**Target:**
- One `World` class that works in both host and peer modes
- Flag: `World::m_isHost` to determine behavior

**Files to modify:**
```
Code/client/World.h/cpp     - Add m_isHost flag, host mode logic
Code/server/World.h/cpp     - DELETE (merge into client World)
Code/server/GameServer.h    - Refactor to NetworkBridge
```

### 2. Create NetworkBridge (Replaces GameServer for Host)

**Current:** `Server::GameServer` - Full game server with World management

**Target:** `NetworkBridge` - Thin network layer that references World

```cpp
// NEW: Code/client/NetworkBridge.h
class NetworkBridge {
public:
    NetworkBridge(World& aWorld);

    void StartListening(uint16_t port, uint8_t maxPlayers);
    void StopListening();
    void Update(); // Called every frame

    // Receive peer messages and apply to World
    void OnPeerMessage(ConnectionId_t peerId, const ClientMessage& msg);

    // Broadcast World changes to peers
    void BroadcastUpdate(const ServerMessage& msg);
    void BroadcastToAllExcept(const ServerMessage& msg, ConnectionId_t excludeId);

private:
    World& m_world; // Reference to host's World (not owned!)
    TiltedPhoques::Server m_server; // Underlying network transport
    Map<ConnectionId_t, PeerInfo> m_peers;
};
```

**Key difference:** NetworkBridge **references** World, doesn't own it. It's just a forwarding layer.

### 3. Refactor HostService

**Current:** HostService creates embedded `GameServer`

**Target:** HostService creates `NetworkBridge` and sets `World::m_isHost = true`

```cpp
// Code/client/Services/HostService.h
struct HostService {
    bool StartHosting(uint16_t port, uint8_t maxPlayers);
    void StopHosting();

private:
    World& m_world;
    std::unique_ptr<NetworkBridge> m_networkBridge; // NEW: replaces m_pGameServer
    bool m_isHosting{false};
};

// Code/client/Services/HostService.cpp
bool HostService::StartHosting(uint16_t port, uint8_t maxPlayers)
{
    // Set World to host mode (authority)
    m_world.SetIsHost(true);

    // Create network bridge (just forwards messages, doesn't own state)
    m_networkBridge = std::make_unique<NetworkBridge>(m_world);
    m_networkBridge->StartListening(port, maxPlayers);

    m_isHosting = true;

    // Host does NOT connect to localhost!
    // m_world.GetTransport().Connect() is NEVER called for host
}
```

### 4. Refactor World Class

**Add host/peer mode:**

```cpp
// Code/client/World.h
struct World : entt::registry {
    World();

    void SetIsHost(bool isHost);
    bool IsHost() const { return m_isHost; }

    // Services work differently based on mode
    CharacterService& GetCharacterService();
    InventoryService& GetInventoryService();
    // ...

    // Host mode: Has NetworkBridge (optional, only when hosting)
    NetworkBridge* GetNetworkBridge() { return m_networkBridge; }
    void SetNetworkBridge(NetworkBridge* bridge) { m_networkBridge = bridge; }

    // Peer mode: Has TransportService
    TransportService& GetTransport(); // Only valid for peers!

private:
    bool m_isHost{false};
    NetworkBridge* m_networkBridge{nullptr}; // Set by HostService when hosting

    // Existing members...
    entt::dispatcher m_dispatcher;
    TransportService m_transport; // Only used by peers
};
```

### 5. Refactor Services to be Host/Peer Aware

Every service needs to handle two modes:

**Host Mode:** Directly modify World, broadcast changes via NetworkBridge
**Peer Mode:** Send requests via TransportService, apply server responses

**Example: CharacterService**

```cpp
// Code/client/Services/CharacterService.cpp

void CharacterService::UpdatePosition(entt::entity entity, Vector3 position)
{
    // Update local World (both host and peer do this)
    auto& movementComponent = m_world.get<MovementComponent>(entity);
    movementComponent.position = position;

    if (m_world.IsHost())
    {
        // Host: Broadcast to peers via NetworkBridge
        ServerReferencesMoveRequest msg;
        msg.Tick = m_world.GetTick();
        // ... fill message ...
        m_world.GetNetworkBridge()->BroadcastUpdate(msg);
    }
    else
    {
        // Peer: Send input to host
        ClientReferencesMoveRequest msg;
        msg.Tick = m_world.GetTick();
        // ... fill message ...
        m_world.GetTransport().Send(msg);
    }
}
```

**This pattern applies to:**
- CharacterService
- InventoryService
- MagicService
- QuestService
- CalendarService
- CombatService
- WeatherService
- StringCacheService
- Every service that does networking!

### 6. Message Handling Refactor

**Current:**
- Client services register handlers for `ServerMessage`
- Server services register handlers for `ClientMessage`

**Target:**
- Host: Directly apply changes to World, broadcast via NetworkBridge
- Peer: Send `ClientMessage`, receive `ServerMessage`

**Example: CharacterService Message Handler**

```cpp
// Peer mode: Handle server updates
void CharacterService::OnServerMoveUpdate(const ServerReferencesMoveRequest& msg)
{
    // Only peers process this (host already has the state)
    if (m_world.IsHost())
        return;

    // Apply server's authoritative update
    for (auto& movement : msg.Updates)
    {
        auto entity = GetEntityById(movement.Id);
        auto& pos = m_world.get<MovementComponent>(entity);
        pos.position = movement.Position;
    }
}

// Host mode: Handle peer inputs (NEW - moved from server code)
void CharacterService::OnPeerMoveInput(ConnectionId_t peerId, const ClientReferencesMoveRequest& msg)
{
    // Only host processes peer inputs
    if (!m_world.IsHost())
        return;

    // Get peer's character entity
    auto peerEntity = GetPeerCharacter(peerId);

    // Apply movement (with validation)
    auto& pos = m_world.get<MovementComponent>(peerEntity);
    pos.position = msg.Position;

    // Broadcast to all peers
    ServerReferencesMoveRequest broadcast;
    // ... fill with peer's new position ...
    m_world.GetNetworkBridge()->BroadcastUpdate(broadcast);
}
```

### 7. StringCache Example (Concrete Implementation)

**Host Mode:**
```cpp
// When host encounters new string
void CachedString::Serialize(...)
{
    auto id = StringCache::Get()[*this];
    if (!id)
    {
        if (World::Get().IsHost())
        {
            // Host: Add immediately, get ID right away
            id = StringCache::Get().Add(*this);

            // NetworkBridge will detect dirty cache and broadcast
            // (handled in NetworkBridge::Update())
        }
        else
        {
            // Peer: Mark as wanted, send full string
            StringCache::Get().AddWanted(*this);
        }
    }
    // ... serialize id or full string ...
}

// NetworkBridge::Update() - called every frame
void NetworkBridge::Update()
{
    auto& cache = StringCache::Get();
    if (cache.ProcessDirty())
    {
        // Broadcast new strings to all peers
        for (auto& peer : m_peers)
        {
            auto startId = peer.stringCacheId;
            auto update = cache.Serialize(startId);
            peer.stringCacheId = startId;
            Send(peer.connectionId, update);
        }
    }
}
```

**Peer Mode:**
```cpp
// Peer receives StringCacheUpdate from host
void World::OnStringCacheUpdate(const StringCacheUpdate& msg)
{
    StringCache::Get().Deserialize(msg);
}
```

## Migration Strategy

### Phase 1: Preparation (Low Risk)
1. Add `World::m_isHost` flag (default false)
2. Add `World::SetIsHost()` / `IsHost()` methods
3. Add guards in services: `if (m_world.IsHost()) { /* future code */ }`
4. Create empty `NetworkBridge` class (stub implementation)
5. **No behavior changes yet** - just setup

### Phase 2: NetworkBridge Implementation (Medium Risk)
1. Implement `NetworkBridge::StartListening()` / `StopListening()`
2. Copy message handling from `GameServer` to `NetworkBridge`
3. Change HostService to create NetworkBridge instead of GameServer
4. **Host still connects to localhost** (not removed yet)
5. Test: Hosting still works

### Phase 3: Dual Mode Services (High Risk)
1. Refactor CharacterService to handle host/peer modes
2. Refactor StringCacheService to handle host/peer modes
3. Refactor InventoryService to handle host/peer modes
4. Continue for all services...
5. Each service refactor should:
   - Keep old code path working
   - Add new host code path
   - Test both modes

### Phase 4: Remove Localhost Connection (Critical)
1. Remove `TransportService::Connect()` call for host
2. Host no longer has TransportService connection
3. All services must use NetworkBridge for host mode
4. **Point of no return** - must be fully tested
5. Remove Server::World, Server::GameServer entirely

### Phase 5: Cleanup
1. Remove server-side Services (now handled by NetworkBridge)
2. Remove redundant code paths
3. Optimize message broadcasting
4. Performance testing

## Risk Mitigation

### Feature Flag Approach

Add a config flag to enable/disable P2P mode:

```cpp
// Config.h
struct Config {
    bool useTrueP2P = false; // Default: old behavior
};

// HostService.cpp
if (Config::Get().useTrueP2P)
{
    // New P2P path
    m_world.SetIsHost(true);
    m_networkBridge = std::make_unique<NetworkBridge>(m_world);
}
else
{
    // Old client-server path
    m_pGameServer = std::make_unique<Server::GameServer>(...);
}
```

This allows:
- Testing new code alongside old code
- Gradual rollout to users
- Easy rollback if issues arise

### Testing Strategy

For each refactored service:
1. **Unit tests** - Test host mode and peer mode separately
2. **Integration test** - Host + 1 peer
3. **Stress test** - Host + 8 peers
4. **Migration test** - Old code vs new code comparison

## Files to Create

```
Code/client/NetworkBridge.h          - NEW: P2P network layer
Code/client/NetworkBridge.cpp        - NEW: Implementation
Code/client/NetworkBridgeMessages.h  - NEW: Message handlers for NetworkBridge
Code/client/PeerConnection.h         - NEW: Per-peer state tracking
```

## Files to Heavily Modify

```
Code/client/World.h/cpp              - Add IsHost flag, NetworkBridge reference
Code/client/Services/HostService.h/cpp - Use NetworkBridge instead of GameServer
Code/client/Services/CharacterService.* - Dual mode (host/peer)
Code/client/Services/InventoryService.* - Dual mode
Code/client/Services/MagicService.*     - Dual mode
Code/client/Services/StringCacheService.* - Dual mode
... (all services that do networking)
```

## Files to Eventually Delete

```
Code/server/World.h/cpp              - DELETE: Merged into client World
Code/server/GameServer.h/cpp         - DELETE: Replaced by NetworkBridge
Code/server/Services/*               - DELETE: Moved to NetworkBridge or client Services
Code/server/main.cpp                 - DELETE: No standalone server
```

## Estimated Effort

- **Phase 1 (Preparation):** 2-3 days
- **Phase 2 (NetworkBridge):** 1 week
- **Phase 3 (Services refactor):** 3-4 weeks (20+ services)
- **Phase 4 (Remove localhost):** 1 week + extensive testing
- **Phase 5 (Cleanup):** 1 week

**Total: ~6-8 weeks of focused development**

## Success Criteria

✅ Host never connects to localhost
✅ Host has zero self-networking overhead
✅ StringCache works without redundancy
✅ All services work in both host and peer modes
✅ Performance improvement for host (lower latency, less CPU)
✅ Peers work identically to before
✅ No game-breaking bugs

## Questions to Resolve

1. **How to handle GameServer Lua scripting?**
   - Move to NetworkBridge? Or remove entirely?

2. **What about ServerConsole commands?**
   - Host should have console access to their World
   - Move commands to client-side for host?

3. **Authentication/validation?**
   - Host still needs to validate peer inputs (anti-cheat)
   - Where does this logic live in NetworkBridge?

4. **Backwards compatibility?**
   - Should we support old saves/network protocol during transition?
   - Version negotiation in handshake?

## Next Steps

1. Review this plan with team
2. Decide on feature flag approach (recommended)
3. Start Phase 1 (preparation)
4. Create NetworkBridge skeleton
5. Begin service-by-service refactoring

---

**This is a major refactor, but it's the right architecture for true P2P.**
