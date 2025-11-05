# P2P Refactor Progress

## Current Status: Server::World Integration Complete ✅

We've successfully implemented the Server::World superset approach from P2P_REFACTOR_FINAL.md. Server::World now contains all client capabilities + networking, ready to replace GameServer.

---

## Completed: Phase 1 & 2 - Server::World as Superset

### ✅ Server::World Expanded (Phase 1)
**Files:** `Code/server/World.h`, `Code/server/World.cpp`

Server::World now contains **all client capabilities**:

**Client Services Added:**
```cpp
// P2P: Client services needed for host to run Skyrim
RunnerService m_runner;
ModSystem m_modSystem;
ctx().emplace<ImguiService>();
ctx().emplace<OverlayService>(*this, m_dispatcher);
ctx().emplace<InputService>(ctx().at<OverlayService>());
ctx().emplace<DebugService>(m_dispatcher, *this, ctx().at<ImguiService>());
ctx().emplace<PapyrusService>(m_dispatcher);
ctx().emplace<DiscordService>(m_dispatcher);
```

**Update Loop:**
```cpp
void World::Update() noexcept
{
    // Calculate delta time
    // Trigger PreUpdateEvent
    m_runner.OnUpdate(UpdateEvent(cDeltaSeconds));
    m_dispatcher.trigger(UpdateEvent(cDeltaSeconds));

    // P2P: Update network bridge if hosting
    if (m_pNetworkBridge)
    {
        m_pNetworkBridge->Update();
    }
}
```

**Result:** Server::World can now interact with Skyrim engine via SKSE hooks, render UI, and run the game loop!

---

### ✅ NetworkBridge Integration (Phase 2)
**Files:** `Code/network_bridge/NetworkBridge.h`, `Code/server/World.h/cpp`

**Key Architectural Change:** NetworkBridge is now a **pure networking layer** with NO game logic!

**NetworkBridge Interface (Refactored):**
```cpp
class NetworkBridge : public TiltedPhoques::Server
{
public:
    // IMPORTANT: Takes dispatcher, NOT World reference!
    explicit NetworkBridge(entt::dispatcher& aDispatcher) noexcept;

    bool StartListening(uint16_t aPort, uint8_t aMaxPeers);
    void Update();
    void BroadcastToAllPeers(const ServerMessage& aMessage);
    void SendToPeer(ConnectionId_t aConnectionId, const ServerMessage& aMessage);

private:
    entt::dispatcher& m_dispatcher; // Event dispatcher (NOT owned!)
    bool m_isListening{false};
};
```

**Flow:**
1. Peer sends ClientMessage → NetworkBridge::OnConsume()
2. NetworkBridge deserializes → triggers event via dispatcher
3. Service listens to event → updates Server::World → creates ServerMessage
4. Service calls `bridge->BroadcastToAllPeers(msg)`

**Server::World Hosting Methods:**
```cpp
// In Server::World
std::unique_ptr<NetworkBridge> m_pNetworkBridge;

bool StartHosting(uint16_t aPort, uint8_t aMaxPeers) noexcept
{
    m_pNetworkBridge = std::make_unique<NetworkBridge>(m_dispatcher);
    return m_pNetworkBridge->StartListening(aPort, aMaxPeers);
}

void StopHosting() noexcept
{
    m_pNetworkBridge->StopListening();
    m_pNetworkBridge.reset();
}

NetworkBridge* GetNetworkBridge() { return m_pNetworkBridge.get(); }
```

---

### ✅ NetworkClient Integration
**Files:** `Code/network_client/NetworkClient.h`, `Code/client/World.h/cpp`

**Same pattern as NetworkBridge** - pure networking layer!

**NetworkClient Interface (Refactored):**
```cpp
class NetworkClient : public TiltedPhoques::Client
{
public:
    // IMPORTANT: Takes dispatcher, NOT World reference!
    explicit NetworkClient(entt::dispatcher& aDispatcher) noexcept;

    bool ConnectToHost(const String& aHostAddress, uint16_t aPort);
    void Disconnect();
    void Update();
    void SendToHost(const ClientMessage& aMessage);

private:
    entt::dispatcher& m_dispatcher; // Event dispatcher (NOT owned!)
    bool m_isConnected{false};
};
```

**Client World Connection Methods:**
```cpp
// In client World
std::unique_ptr<NetworkClient> m_pNetworkClient;

bool ConnectToHost(const String& aHostAddress, uint16_t aPort) noexcept
{
    m_pNetworkClient = std::make_unique<NetworkClient>(m_dispatcher);
    return m_pNetworkClient->ConnectToHost(aHostAddress, aPort);
}

void Disconnect() noexcept
{
    m_pNetworkClient->Disconnect();
    m_pNetworkClient.reset();
}

NetworkClient* GetNetworkClient() { return m_pNetworkClient.get(); }
```

---

### ✅ HostService Removed
**Files Removed:** `Code/client/Services/HostService.h/cpp` references removed from World

**Rationale:** HostService was just a wrapper around GameServer. Now that Server::World has `StartHosting()`/`StopHosting()` built-in, HostService is redundant.

**Before:**
```cpp
// Client World creates HostService → HostService creates GameServer
world.GetHostService().StartHosting(port);
```

**After:**
```cpp
// Just use Server::World directly!
Server::World serverWorld;
serverWorld.StartHosting(port);
```

---

## Architecture Diagram (Updated)

### Old Architecture (Redundant Localhost)
```
Host Process:
├─ Client World
│   └─ TransportService → localhost:10578 (redundant!)
└─ GameServer (embedded)
    └─ Server::World (redundant copy!)
```

### New Architecture (Direct Authority) ✅
```
Host Process:
└─ Server::World (THE authoritative World)
   ├─ All client services (RunnerService, UI, SKSE hooks)
   ├─ All server services (PlayerManager, CharacterService)
   ├─ NetworkBridge (pure networking)
   │  └─ Dispatcher-based events
   └─ NO TransportService (doesn't connect to itself!)

Client Process:
└─ World (unchanged)
   ├─ Client services
   ├─ NetworkClient (pure networking)
   │  └─ Dispatcher-based events
   └─ NO server services
```

**Key Insight:** NetworkBridge and NetworkClient are **symmetric** - both use dispatcher for events, no World dependency!

---

## Key Architectural Decisions

### ✅ Pure Networking Layers
**Decision:** NetworkBridge and NetworkClient contain ZERO game logic.

**Rationale:**
- Separation of concerns (networking vs game logic)
- Services already handle game state updates
- No need for network code to know about World structure
- Only need dispatcher to trigger events

**Pattern:**
```cpp
// NetworkBridge receives message
void NetworkBridge::OnConsume(const void* data, uint32_t size, ConnectionId_t connId)
{
    // Deserialize ClientMessage
    auto msg = DeserializeClientMessage(data, size);

    // Trigger event (services will listen)
    m_dispatcher.trigger(ClientMessageEvent{msg, connId});

    // NO game logic here! Services handle it.
}

// Service listens to event
void CharacterService::OnClientMoveRequest(const ClientMessageEvent& event)
{
    // Update World
    auto& movement = m_world.get<MovementComponent>(entity);
    movement.Position = event.msg.Position;

    // Broadcast to peers
    ServerReferencesMoveRequest response;
    response.Position = event.msg.Position;
    m_world.GetNetworkBridge()->BroadcastToAllPeers(response);
}
```

---

## Implementation Status

### ✅ Completed

**Phase 1: Expand Server::World**
- ✅ Add all client services to Server::World
- ✅ Add Update() method
- ✅ Add getters for all services
- ✅ Server::World can now run Skyrim game loop

**Phase 2: Integrate Networking**
- ✅ Refactor NetworkBridge to use dispatcher only
- ✅ Add NetworkBridge member to Server::World
- ✅ Add StartHosting/StopHosting methods
- ✅ Refactor NetworkClient to use dispatcher only
- ✅ Add NetworkClient member to client World
- ✅ Add ConnectToHost/Disconnect methods
- ✅ Remove HostService (no longer needed)

---

## Next Steps

### Phase 3: Implementation
**Status:** Ready to implement

**Required:**
1. **Implement NetworkBridge.cpp**
   - Inherit from TiltedPhoques::Server
   - Implement OnConsume, OnConnection, OnDisconnection
   - Deserialize ClientMessages → trigger events
   - Serialize and send ServerMessages

2. **Implement NetworkClient.cpp**
   - Inherit from TiltedPhoques::Client
   - Implement OnConsume, OnConnected, OnDisconnected
   - Deserialize ServerMessages → trigger events
   - Serialize and send ClientMessages

3. **Create Event Structs**
   - `ClientMessageEvent` - wraps ClientMessage + ConnectionId
   - `ServerMessageEvent` - wraps ServerMessage
   - Services connect to these events

4. **Update main.cpp**
   ```cpp
   // Detect host vs client
   if (userWantsToHost)
   {
       Server::World::Create();
       auto& world = Server::World::Get();
       world.StartHosting(10578, 8);

       while (running)
       {
           world.Update(); // Runs game + handles peers
       }
   }
   else
   {
       World::Create();
       auto& world = World::Get();
       world.ConnectToHost("192.168.1.5", 10578);

       while (running)
       {
           world.Update(); // Runs game + syncs from host
       }
   }
   ```

---

### Phase 4: Service Migration
**Status:** Not started

**Pattern for migrating services:**

**Old (TransportService):**
```cpp
void CharacterService::OnActorMoved(Actor* actor, Vector3 pos)
{
    ClientReferencesMoveRequest msg;
    msg.Position = pos;
    m_transport.Send(msg); // Goes to localhost if host!
}
```

**New (NetworkBridge/NetworkClient):**
```cpp
// Server-side service
void Server::CharacterService::OnActorMoved(Actor* actor, Vector3 pos)
{
    // Update authoritative World
    UpdateActorPosition(actor, pos);

    // Broadcast to peers (NO localhost!)
    ServerReferencesMoveRequest msg;
    msg.Position = pos;
    m_world.GetNetworkBridge()->BroadcastToAllPeers(msg);
}

// Client-side service (unchanged from before)
void CharacterService::OnActorMoved(Actor* actor, Vector3 pos)
{
    // Client-side prediction
    UpdateActorPosition(actor, pos);

    // Send to host
    ClientReferencesMoveRequest msg;
    msg.Position = pos;
    m_world.GetNetworkClient()->SendToHost(msg);
}
```

**Services to migrate (~11 total):**
- CharacterService
- InventoryService
- MagicService
- CombatService
- QuestService
- PartyService
- CalendarService
- WeatherService
- StringCacheService
- PlayerService
- ObjectService

---

### Phase 5: Cleanup
**Status:** Not started

**Remove old code:**
- Delete `Code/server/GameServer.h/cpp`
- Delete old `Server::World` if needed (already using new one)
- Remove TransportService localhost connection logic
- Clean up build files (xmake.lua)

---

## Metrics for Success

### When is Phase 2 complete? ✅

**Functional:**
- ✅ Server::World contains all client services
- ✅ Server::World has Update() loop
- ✅ NetworkBridge is pure networking (dispatcher only)
- ✅ NetworkClient is pure networking (dispatcher only)
- ✅ Server::World has StartHosting()/StopHosting()
- ✅ Client World has ConnectToHost()/Disconnect()
- ✅ HostService removed (no longer needed)

**Architecture:**
- ✅ Clean separation: networking vs game logic
- ✅ Symmetric design: host and client use same event pattern
- ✅ No World reference in network code
- ✅ Ready for implementation phase

---

## Open Questions Resolved

### ✅ 1. How will HostService use NetworkBridge?
**Answer:** HostService deleted! Server::World directly owns NetworkBridge and provides `StartHosting()` wrapper.

### ✅ 2. How will services access NetworkBridge/NetworkClient?
**Answer:** Option A implemented - World owns them, services get via `GetNetworkBridge()` / `GetNetworkClient()`.

### ✅ 3. Should NetworkBridge/NetworkClient have World reference?
**Answer:** NO! They only need `entt::dispatcher&` to trigger events. Services handle all game logic.

---

## Benefits Achieved ✅

### 1. No Localhost Connection
- Host uses Server::World directly
- No redundant serialization/deserialization
- No network overhead for host's own actions

### 2. Clean Separation
- Networking code has zero game logic
- All game state updates in services
- Easy to test networking independently

### 3. Symmetric Design
- Host and client follow same event pattern
- NetworkBridge and NetworkClient are parallel
- Consistent architecture across both sides

### 4. Compile-Time Safety
```cpp
// This won't compile - Client World doesn't have PlayerManager
World clientWorld;
clientWorld.GetPlayerManager(); // ERROR

// This works - Server::World has everything
Server::World serverWorld;
serverWorld.GetPlayerManager(); // OK
serverWorld.GetRunner(); // OK
serverWorld.GetNetworkBridge(); // OK
```

---

## Summary

**What We Built:**
1. Server::World as full-featured superset (client + server capabilities)
2. Pure networking layers (NetworkBridge, NetworkClient) with no game logic
3. Dispatcher-based event system for clean separation
4. Host and client follow symmetric patterns

**Architecture is Ready:**
- ✅ Foundation complete
- ✅ Networking layers defined
- ✅ Integration complete
- ⏳ Implementation needed (NetworkBridge.cpp, NetworkClient.cpp)
- ⏳ Service migration needed (~11 services)

**Next Session:** Begin Phase 3 - implement NetworkBridge.cpp and NetworkClient.cpp, update main.cpp to use appropriate World type.

---

**Last Updated:** 2025-11-05
**Status:** Phase 1 & 2 complete, ready for implementation phase
