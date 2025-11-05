# P2P Refactor - Final Approach

## The Clean Solution: Server::World is the Superset

**Key Insight:** Don't try to make one World that does both. Instead, make `Server::World` capable of everything, and use the right World type from the start.

---

## Architecture

```
Host Process:
└─ Server::World (the full-featured World)
   ├─ All client capabilities (SKSE hooks, rendering, UI)
   ├─ All server capabilities (PlayerManager, NetworkBridge)
   └─ NO TransportService (doesn't connect to itself)

Client Process:
└─ World (client World, unchanged)
   ├─ Client capabilities (SKSE hooks, rendering, UI)
   ├─ TransportService (connects to host)
   └─ NO server capabilities
```

**No if-statements needed!** Just use the appropriate World type.

---

## What Needs to Happen

### Step 1: Add Client Services to Server::World

**Currently Server::World has:**
- ✅ CharacterService (server version)
- ✅ PartyService
- ✅ CalendarService
- ✅ QuestService
- ✅ PlayerService
- ✅ PlayerManager
- ✅ AdminService
- ✅ ScriptService

**Missing from Server::World (client has these):**
- ❌ RunnerService (game loop)
- ❌ OverlayService (UI)
- ❌ DebugService (debugging)
- ❌ MagicService (spells)
- ❌ InventoryService
- ❌ CombatService
- ❌ WeatherService
- ❌ ObjectService
- ❌ ActorValueService
- ❌ CommandService
- ❌ ModSystem

**Action:** Add all missing services to `Code/server/World.h` and `Code/server/World.cpp`

---

### Step 2: Update Server::World to Handle Rendering

**Currently:** Server::World just manages state, doesn't interact with Skyrim engine.

**Needed:** Server::World needs to:
- Process SKSE hooks (OnActorMove, OnActorSpawn, etc.)
- Inject into Skyrim (ForcePosition, SpawnActor, etc.)
- Render UI overlay
- Handle input

**Action:** Make sure Server::World services connect to SKSE hooks when running as host.

---

### Step 3: Replace GameServer with Server::World in HostService

**Current HostService:**
```cpp
class HostService {
    std::unique_ptr<Server::GameServer> m_pGameServer; // Embedded server
};
```

**Target HostService:**
```cpp
class HostService {
    // No embedded GameServer!
    // Host just switches to using Server::World directly
};
```

**In main.cpp:**
```cpp
int main()
{
    if (userWantsToHost)
    {
        // Create Server::World instead of client World
        Server::World::Create();
        auto& world = Server::World::Get();

        // Start listening for peers (GameServer functionality merged into World)
        world.StartHosting(port, maxPlayers);

        while (running)
        {
            world.Update(); // Runs game + handles peers
        }
    }
    else
    {
        // Create client World as usual
        World::Create();
        auto& world = World::Get();

        // Connect to host
        world.GetTransport().Connect(hostIP, port);

        while (running)
        {
            world.Update(); // Runs game + syncs from host
        }
    }
}
```

---

### Step 4: Merge GameServer Functionality into Server::World

**Currently:** `GameServer` handles:
- Listening for connections
- Processing messages
- Broadcasting updates
- Managing players

**Target:** `Server::World` does all of this directly:

```cpp
namespace Server
{
    struct World : entt::registry
    {
        // NEW: Host/server functionality (from GameServer)
        void StartHosting(uint16_t aPort, uint8_t aMaxPlayers);
        void StopHosting();
        void BroadcastToAllPeers(const ServerMessage& aMessage);
        void SendToPeer(ConnectionId_t aPeerId, const ServerMessage& aMessage);

        // NEW: Client services (for rendering/SKSE)
        OverlayService& GetOverlayService();
        MagicService& GetMagicService();
        // ... all client services

        // Existing server stuff
        PlayerManager& GetPlayerManager();
        CharacterService& GetCharacterService();
        // ... all server services

    private:
        // NEW: Network bridge (replaces GameServer)
        std::unique_ptr<NetworkBridge> m_pNetworkBridge;

        // Existing
        PlayerManager m_playerManager;
        // ... all services
    };
}
```

---

## The Clean Result

### Host Code (No If-Checks!)

```cpp
// Host uses Server::World
Server::World& world = Server::World::Get();

// Service code knows it's the server version
void Server::CharacterService::OnActorMoved(Actor* apActor, Vector3 aPos)
{
    // Update World
    UpdateActorPosition(apActor, aPos);

    // Broadcast to peers - no if-check!
    ServerReferencesMoveRequest msg;
    msg.Position = aPos;

    m_world.BroadcastToAllPeers(msg);
}
```

### Client Code (Unchanged!)

```cpp
// Client uses regular World
World& world = World::Get();

// Service code knows it's the client version
void CharacterService::OnHostUpdate(const ServerReferencesMoveRequest& aMsg)
{
    // Apply host's update
    UpdateActorPosition(entity, aMsg.Position);
}
```

---

## File Structure

### Keep Separate:

```
Code/
├─ client/
│   ├─ World.h/cpp              (Client World - unchanged)
│   ├─ Services/
│   │   ├─ CharacterService.*   (Client version)
│   │   ├─ MagicService.*       (Client version)
│   │   └─ ...                  (All client services)
│   └─ main.cpp                 (Entry point)
│
├─ server/
│   ├─ World.h/cpp              (Server::World - becomes superset)
│   ├─ Services/
│   │   ├─ CharacterService.*   (Server version)
│   │   ├─ PlayerService.*      (Server only)
│   │   └─ ...                  (All server services)
│   └─ GameServer.cpp           (DELETE - merge into World)
│
└─ network_bridge/
    └─ NetworkBridge.h/cpp      (Used by Server::World)
```

---

## Services Comparison

### Client CharacterService
```cpp
// Code/client/Services/CharacterService.cpp
void CharacterService::OnLocalPlayerMoved(Vector3 aPos)
{
    // Send to host
    ClientReferencesMoveRequest msg;
    msg.Position = aPos;
    m_world.GetTransport().Send(msg);
}

void CharacterService::OnHostUpdate(const ServerReferencesMoveRequest& aMsg)
{
    // Apply host's authoritative update
    UpdateActorPosition(entity, aMsg.Position);
}
```

### Server CharacterService
```cpp
// Code/server/Services/CharacterService.cpp
void CharacterService::OnActorMoved(Actor* apActor, Vector3 aPos)
{
    // Host's actor moved (authoritative)
    UpdateActorPosition(apActor, aPos);

    // Broadcast to all peers
    ServerReferencesMoveRequest msg;
    msg.Position = aPos;
    m_world.BroadcastToAllPeers(msg);
}

void CharacterService::OnPeerMovementInput(ConnectionId_t aPeerId, const ClientReferencesMoveRequest& aMsg)
{
    // Peer's character moved
    auto peerActor = GetPeerActor(aPeerId);
    UpdateActorPosition(peerActor, aMsg.Position);

    // Broadcast to all peers (including sender)
    ServerReferencesMoveRequest broadcast;
    broadcast.Position = aMsg.Position;
    m_world.BroadcastToAllPeers(broadcast);
}
```

**No if-statements! Different files, different implementations.**

---

## Implementation Steps

### Phase 1: Expand Server::World (1 week)

1. Add all client services to `Code/server/World.h`
2. Add all client service includes
3. Initialize all services in `Code/server/World.cpp` constructor
4. Add getters for all services

**Files to modify:**
- `Code/server/World.h`
- `Code/server/World.cpp`

### Phase 2: Merge GameServer into Server::World (3 days)

1. Add `StartHosting()` / `StopHosting()` to `Server::World`
2. Add `m_pNetworkBridge` member to `Server::World`
3. Copy message handling from `GameServer` to `Server::World`
4. Add `BroadcastToAllPeers()` method
5. Delete `GameServer.h/cpp`

**Files to modify:**
- `Code/server/World.h/cpp`
- `Code/server/GameServer.h/cpp` (delete after merge)

### Phase 3: Update main.cpp to Use Server::World for Host (2 days)

1. Detect if hosting or joining
2. If hosting: Create `Server::World`
3. If joining: Create client `World`
4. Both have same `Update()` loop

**Files to modify:**
- `Code/client/main.cpp`

### Phase 4: Test (1 week)

1. Test hosting with `Server::World`
2. Test client connection
3. Test all services work
4. Fix bugs

---

## Benefits

### ✅ No If-Statements Everywhere
```cpp
// OLD (bad)
if (m_world.IsHost()) {
    m_world.GetNetworkBridge()->Broadcast(msg);
} else {
    m_world.GetTransport().Send(msg);
}

// NEW (good) - Service knows which World it's in
void Server::CharacterService::OnActorMoved(...)
{
    m_world.BroadcastToAllPeers(msg); // Always valid
}
```

### ✅ Clear Separation
- **Server::World** = Full-featured host World
- **client World** = Lightweight peer World
- Different types, different purposes

### ✅ Compile-Time Safety
```cpp
Server::World hostWorld;
hostWorld.GetPlayerManager(); // OK

World clientWorld;
clientWorld.GetPlayerManager(); // Compile error - no such method
```

### ✅ Leverage Existing Code
- Don't rewrite everything
- Server services already exist
- Client services already exist
- Just add missing pieces to each

### ✅ Natural Flow
```cpp
// Host process
Server::World world;
world.StartHosting(10578);
while (running) {
    world.Update(); // Runs game + broadcasts
}

// Client process
World world;
world.GetTransport().Connect("192.168.1.5", 10578);
while (running) {
    world.Update(); // Runs game + syncs
}
```

---

## What Gets Deleted

- ✅ `Code/server/GameServer.h/cpp` (functionality merged into Server::World)
- ✅ `Code/client/Services/HostService.h/cpp` (no longer needed)
- ✅ Localhost connection code (host doesn't connect to itself)

---

## What Stays

- ✅ `Code/client/World.*` (unchanged)
- ✅ `Code/server/World.*` (expanded with client services)
- ✅ All client services (unchanged)
- ✅ All server services (unchanged)
- ✅ NetworkBridge (used by Server::World)

---

## Summary

**Old:**
```
Host: client World + embedded GameServer (localhost connection)
Client: client World → connects to server
```

**New:**
```
Host: Server::World (superset) → broadcasts to peers
Client: client World → connects to host
```

**Result:**
- No if-checks
- No localhost connection
- No redundant World
- Clean separation
- Leverage existing code

---

**Next Step:** Start with Phase 1 - Add client services to `Code/server/World.h`

