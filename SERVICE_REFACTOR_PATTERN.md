# Service Refactor Pattern - Option A

## Goal
Make Server services handle BOTH game events (from Skyrim) AND network events (from peers), while keeping client services unchanged.

---

## Architecture

### Before (Dedicated Server Model):
```
Client World:
├─ Client Services (listen to Skyrim + network)
│  └─ Send to server via TransportService
└─ TransportService → connects to server

Dedicated Server (separate process):
├─ Server Services (listen to network ONLY)
│  └─ No game logic, just network proxying
└─ GameServer → broadcasts to clients
```

### After (P2P Host Model):
```
Client World:
├─ Client Services (unchanged)
│  └─ Send to host via NetworkClient
└─ NetworkClient → connects to host

Host (Server::World):
├─ Server Services (listen to Skyrim + network)
│  ├─ Game events → update world → broadcast
│  └─ Network events → update world → broadcast
└─ NetworkBridge → broadcasts to peers
```

---

## Refactor Pattern

### Step 1: Identify Responsibilities

For each service, identify:
1. **Game Event Handlers** - React to Skyrim (OnActorAdded, OnInventoryChanged, etc.)
2. **Network Event Handlers** - React to peer messages (OnAssignCharacterRequest, etc.)
3. **State Management** - Update ECS components, game state
4. **Broadcasting** - Send updates to peers

### Step 2: Port Game Logic to Server Service

**Example: CharacterService**

#### Client Service (existing):
```cpp
// Code/client/Services/CharacterService.cpp
void CharacterService::OnActorAdded(const ActorAddedEvent& acEvent) noexcept
{
    // Game spawned an actor
    auto* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));
    if (!pActor)
        return;

    // Create entity in client world
    auto entity = m_world.create();
    m_world.emplace<FormIdComponent>(entity, acEvent.FormId);
    m_world.emplace<LocalComponent>(entity);

    // Request server to assign this actor
    AssignCharacterRequest request;
    request.FormId = acEvent.FormId;
    request.ActorData = BuildActorData(pActor);
    m_transport.Send(request);  // → Send to server
}
```

#### Server Service (current - network only):
```cpp
// Code/server/Services/CharacterService.cpp
void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept
{
    // Peer wants to spawn an actor
    auto entity = m_world.create();
    m_world.emplace<FormIdComponent>(entity, acMessage.Packet.FormId);

    // Broadcast to all peers
    AssignCharacterResponse response;
    response.ServerId = ToInteger(entity);
    response.FormId = acMessage.Packet.FormId;
    GameServer::Get()->SendToPlayers(response, acMessage.GetSender());
}
```

#### Server Service (AFTER refactor - handles BOTH):
```cpp
// Code/server/Services/CharacterService.cpp
CharacterService::CharacterService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    // Existing: Listen to peer network messages
    m_characterAssignRequestConnection =
        aDispatcher.sink<PacketEvent<AssignCharacterRequest>>()
            .connect<&CharacterService::OnAssignCharacterRequest>(this);

    // NEW: Listen to Skyrim game events (ported from client service)
    m_actorAddedConnection =
        aDispatcher.sink<ActorAddedEvent>()
            .connect<&CharacterService::OnActorAdded>(this);
}

// NEW: Handle game events (ported from client service)
void CharacterService::OnActorAdded(const ActorAddedEvent& acEvent) noexcept
{
    // Host's game spawned an actor
    auto* pActor = Cast<Actor>(TESForm::GetById(acEvent.FormId));
    if (!pActor)
        return;

    // Create entity in Server::World
    auto entity = m_world.create();
    m_world.emplace<FormIdComponent>(entity, acEvent.FormId);
    m_world.emplace<LocalComponent>(entity); // Host owns it

    // Broadcast to peers (NO localhost sending!)
    AssignCharacterResponse response;
    response.ServerId = ToInteger(entity);
    response.FormId = acEvent.FormId;
    response.ActorData = BuildActorData(pActor);

    // Replace GameServer::Get()->SendToPlayers()
    if (auto* bridge = m_world.GetNetworkBridge())
    {
        bridge->BroadcastToAllPeers(response);
    }
}

// UPDATED: Network handler (replace GameServer calls)
void CharacterService::OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) const noexcept
{
    // Peer wants to spawn an actor
    auto entity = m_world.create();
    m_world.emplace<FormIdComponent>(entity, acMessage.Packet.FormId);
    m_world.emplace<RemoteComponent>(entity, acMessage.GetSender()->GetId());

    // Broadcast to all peers (including sender)
    AssignCharacterResponse response;
    response.ServerId = ToInteger(entity);
    response.FormId = acMessage.Packet.FormId;
    response.ActorData = acMessage.Packet.ActorData;

    // Replace GameServer::Get()->SendToPlayers()
    if (auto* bridge = m_world.GetNetworkBridge())
    {
        bridge->BroadcastToAllPeers(response);
    }
}
```

---

## Common Code Pattern (For Duplicated Logic)

When client and server services share logic, create a base class:

### Example: CharacterServiceBase

```cpp
// Code/common/Services/CharacterServiceBase.h
struct CharacterServiceBase
{
protected:
    // Shared helper methods
    ActorData BuildActorData(Actor* apActor) const noexcept;
    void ApplyActorData(Actor* apActor, const ActorData& acData) const noexcept;
    Actor* CreateActorInGame(const FormIdComponent& acFormId) const noexcept;

    // Shared state management
    void CreateEntityForActor(entt::registry& aRegistry, uint32_t aFormId) const noexcept;
};

// Code/client/Services/CharacterService.h
struct CharacterService : CharacterServiceBase
{
    // Client-specific logic
    void OnActorAdded(const ActorAddedEvent& acEvent) noexcept;
    void OnAssignCharacter(const AssignCharacterResponse& acMessage) noexcept;

private:
    TransportService& m_transport; // Client sends to server
};

// Code/server/Services/CharacterService.h
namespace Server {
    struct CharacterService : CharacterServiceBase
    {
        // Server-specific logic (handles BOTH game and network)
        void OnActorAdded(const ActorAddedEvent& acEvent) noexcept;
        void OnAssignCharacterRequest(const PacketEvent<AssignCharacterRequest>& acMessage) noexcept;

    private:
        World& m_world; // Server broadcasts to peers
    };
}
```

---

## Migration Checklist (Per Service)

### Phase 1: Analyze
- [ ] List all game event handlers in client service
- [ ] List all network event handlers in server service
- [ ] Identify shared helper methods
- [ ] Identify `GameServer::Get()` call sites

### Phase 2: Create Base (If Needed)
- [ ] Create `Code/common/Services/<ServiceName>Base.h`
- [ ] Move shared helper methods to base class
- [ ] Update client service to inherit from base
- [ ] Update server service to inherit from base

### Phase 3: Port Game Handlers
- [ ] Copy game event handlers from client → server service
- [ ] Update to use Server::World instead of client World
- [ ] Connect to game events in server service constructor
- [ ] Test event handlers fire correctly

### Phase 4: Replace GameServer Calls
- [ ] Find all `GameServer::Get()->SendToPlayers()`
- [ ] Replace with `m_world.GetNetworkBridge()->BroadcastToAllPeers()`
- [ ] Find all `GameServer::Get()->SendToParty()`
- [ ] Implement party filtering in broadcast (or use NetworkBridge::SendToPeer loop)
- [ ] Remove `GameServer::Get()` includes

### Phase 5: Test
- [ ] Host starts with F9
- [ ] Game events trigger server handlers
- [ ] Peers receive broadcasts
- [ ] Client joins with F6
- [ ] Client receives updates from host

---

## Services to Migrate (Priority Order)

### 1. **CharacterService** (FIRST - most critical)
- Handles actor spawning, movement, animation
- Most complex, but most important

### 2. **PlayerService**
- Handles player state, connection/disconnection
- Simpler than CharacterService

### 3. **InventoryService**
- Item equipping, trading
- Medium complexity

### 4. **MagicService**
- Spell casting, effects
- Medium complexity

### 5. **CombatService**
- Damage, projectiles, hits
- Medium complexity

### 6. **QuestService**
- Quest progress sync
- Low complexity (mostly just broadcasts)

### 7. **PartyService**
- Party management
- Low complexity

### 8. **CalendarService**
- Time sync
- Low complexity

### 9. **WeatherService**
- Weather sync
- Low complexity

### 10. **StringCacheService**
- String ID sync
- Low complexity (good test case!)

### 11. **ObjectService**
- World object sync
- Medium complexity

---

## Code Replacement Patterns

### Pattern 1: GameServer Broadcast
```cpp
// BEFORE:
GameServer::Get()->SendToPlayers(message, excludeSender);

// AFTER:
if (auto* bridge = m_world.GetNetworkBridge())
{
    // TODO: Handle excludeSender if needed
    bridge->BroadcastToAllPeers(message);
}
```

### Pattern 2: GameServer Party Broadcast
```cpp
// BEFORE:
GameServer::Get()->SendToParty(message, partyComponent, excludeSender);

// AFTER:
if (auto* bridge = m_world.GetNetworkBridge())
{
    // Get party members
    for (auto memberId : partyComponent.MemberIds)
    {
        if (auto* pPlayer = m_world.GetPlayerManager().GetById(memberId))
        {
            if (pPlayer != excludeSender)
            {
                bridge->SendToPeer(pPlayer->GetConnectionId(), message);
            }
        }
    }
}
```

### Pattern 3: Client Send
```cpp
// Client services (unchanged):
m_transport.Send(message);

// OR eventually:
if (auto* client = m_world.GetNetworkClient())
{
    client->SendToHost(message);
}
```

---

## Testing Strategy

### Unit Test Each Service:
1. Create Server::World
2. Trigger game event (e.g., spawn actor)
3. Verify entity created in world
4. Verify broadcast message queued
5. Verify peers would receive it

### Integration Test:
1. Start host with F9
2. Join with F6 from second client
3. Trigger game event on host
4. Verify client receives update
5. Trigger event on client
6. Verify host receives and broadcasts

---

## Next Steps

1. **Start with StringCacheService** (simplest - good warm-up)
2. **Then CharacterService** (most important)
3. **Continue through priority list**
4. **Delete GameServer** after all services migrated
5. **Clean up old code** (HostService, localhost logic)

---

## Notes

- Server services now need access to Skyrim APIs (Actor, TESForm, etc.)
- Server services need to be built as part of client DLL (not standalone server exe)
- NetworkBridge message handlers still need to be implemented (currently stubs)
- PacketEvent<T> pattern might need adjustment for P2P (currently expects Player*)

---

**Status:** Ready to begin migration
**Estimated Effort:** 3-5 days per service (11 services ≈ 4-6 weeks)
