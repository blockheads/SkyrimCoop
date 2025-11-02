# Listen Server Architecture - Design Document

## Overview

This document outlines the architectural changes needed to convert SkyrimCoop from a **dedicated server model** to a **listen server (P2P co-op) model** where the host player runs both client and server in the same Skyrim process.

## Motivation

### Problem with Current Architecture
The original Tilted Online uses a **dedicated server** model:
- Server runs as standalone executable (`SkyrimTogetherServer.exe`)
- Server has NO access to Skyrim game engine
- Server cannot run physics, AI, pathfinding, or game logic
- Physics/AI were disabled server-side, causing sync issues
- Server can only relay state, not authoritatively simulate

### Goal: Listen Server with Engine Access
We want a **P2P co-op model** where:
- One player acts as "host" and runs server in their Skyrim process
- Host's server has FULL access to Skyrim engine (physics, AI, quests, etc.)
- Host runs authoritative simulation of the game world
- Clients are "thin" - they send inputs and receive state updates
- Host does NOT send network messages to themselves (direct simulation)

## Architecture Comparison

### Current (Dedicated Server)
```
┌─────────────────────┐         ┌──────────────────────┐
│ Standalone Server   │         │   Skyrim Client      │
│ (Separate EXE)      │◄────────┤   (SKSE Plugin)      │
│                     │         │                      │
│ - No engine access  │         │ - Full engine access │
│ - Pure networking   │         │ - Physics enabled    │
│ - State relay only  │         │ - Rendering, AI      │
└─────────────────────┘         └──────────────────────┘
```

### Target (Listen Server)
```
┌──────────────────────────────────────────────┐
│     Host Player (SkyrimTogetherClient.dll)   │
│                                              │
│  ┌────────────────┐  ┌──────────────────┐   │
│  │ Client Mode    │  │ Server Mode      │   │
│  │                │  │ (Embedded)       │   │
│  │ - Local player │◄─┤                  │   │
│  │ - Rendering    │  │ - Physics runs   │   │
│  │ - Input        │  │ - AI runs        │   │
│  └────────────────┘  │ - Authoritative  │   │
│                      └──────────────────┘   │
│         ▲                     │              │
│         │ Direct access       │ Network      │
│         │ (no loopback)       ▼              │
│  ┌──────────────────────────────────────┐   │
│  │   Skyrim Engine (Shared Access)      │   │
│  │   - Physics (Havok)                  │   │
│  │   - AI / Pathfinding                 │   │
│  │   - Quest System                     │   │
│  │   - Animation System                 │   │
│  └──────────────────────────────────────┘   │
└──────────────────────────────────────────────┘
                      │
                      │ Network messages
                      ▼
            ┌──────────────────┐
            │ Remote Clients   │
            │ (Receive updates)│
            └──────────────────┘
```

## Key Design Principles

### 1. **No Host Loopback**
- Host server updates game state by reading DIRECTLY from Skyrim engine
- Host client renders using LOCAL actors, not network-replicated ones
- Host NEVER sends network messages to itself
- Only remote clients receive network updates

### 2. **Authoritative Host Simulation**
- Host runs full Skyrim physics simulation (Havok)
- Host runs NPC AI with real pathfinding (navmesh)
- Host's game state is ground truth
- Clients trust host's updates unconditionally

### 3. **Shared Engine Access**
- Both client and server code run in same process
- Both can call Skyrim engine functions via SKSE
- Server services can call `Actor::Update()`, `bhkWorld::StepSimulation()`, etc.
- No need for "dumb" state relay

### 4. **Thin Clients**
- Remote clients send inputs/actions to host
- Remote clients receive authoritative state from host
- Remote clients interpolate/extrapolate for smooth rendering
- Remote clients have physics disabled (host is authoritative)

## Implementation Plan

### Phase 1: Build System Changes
**Goal:** Compile server code into SKSE plugin instead of standalone EXE

**Tasks:**
1. Modify `xmake.lua` to link server sources into `SkyrimTogetherClient.dll`
2. Make server code conditional on `IS_HOST` flag
3. Ensure server and client code can coexist in same binary
4. Keep `SkyrimTogetherServer.exe` for now (backwards compatibility)

**Files to modify:**
- `xmake.lua` - Add server sources to client target
- Build configuration

### Phase 2: Host/Client Mode Detection
**Goal:** Determine if player is hosting or joining

**Tasks:**
1. Add `IsHost()` method to `TransportService`
2. Add UI option: "Host Game" vs "Join Game"
3. Store host flag in `World` or `TransportService`
4. Use flag to conditionally instantiate server

**Files to modify:**
- `Code/client/Services/TransportService.h/cpp`
- UI components for host/join selection

### Phase 3: HostService Creation
**Goal:** Manage embedded GameServer lifecycle

**Tasks:**
1. Create `Code/client/Services/HostService.h/cpp`
2. Instantiate `GameServer` when in host mode
3. Connect local client to embedded server (loopback on localhost)
4. Handle server startup/shutdown with client

**New files:**
- `Code/client/Services/HostService.h`
- `Code/client/Services/HostService.cpp`

**Responsibilities:**
```cpp
struct HostService {
    void StartHosting();  // Create GameServer instance
    void StopHosting();   // Shutdown server
    bool IsHosting();     // Check if hosting
    GameServer* GetServer(); // Access embedded server
};
```

### Phase 4: Engine Access Layer
**Goal:** Allow server services to call Skyrim engine functions

**Tasks:**
1. Create `Code/server/EngineInterface.h` - Abstract interface for engine calls
2. Implement interface in client code (has SKSE access)
3. Pass interface to server services on construction
4. Server calls engine functions through interface

**Example:**
```cpp
// Server side
struct IEngineInterface {
    virtual Actor* GetActor(entt::entity) = 0;
    virtual void UpdatePhysics(Actor*, float deltaTime) = 0;
    virtual NiPoint3 GetActorPosition(Actor*) = 0;
    // ... more engine calls
};

// Client side (SKSE plugin)
struct EngineInterface : IEngineInterface {
    Actor* GetActor(entt::entity entity) override {
        // Lookup actor in Skyrim's form list
        return LookupFormByID<Actor>(entityToFormId[entity]);
    }

    void UpdatePhysics(Actor* actor, float dt) override {
        // Call Skyrim's physics update
        actor->GetMiddleProcess()->UpdatePosition(dt);
    }
};
```

### Phase 5: Host Bypass Logic
**Goal:** Host reads from engine directly, doesn't send to self

**Tasks:**
1. Add `Player* GetHostPlayer()` to `PlayerManager` (already done!)
2. In server services, check if owner is host
3. If host: read from engine, broadcast to others only
4. If client: use network data, broadcast to all

**Example Pattern:**
```cpp
void ServerCharacterService::OnUpdate(const UpdateEvent& acEvent) {
    Player* pHost = GetHostPlayer();

    for (auto entity : view<MovementComponent, OwnerComponent>()) {
        Player* pOwner = GetOwner(entity);

        if (pOwner == pHost) {
            // HOST-OWNED ENTITY: Read from local engine
            Actor* pActor = m_engineInterface->GetActor(entity);
            auto& movement = GetMovement(entity);

            // Update from authoritative simulation
            movement.Position = pActor->pos;
            movement.Rotation = pActor->rot;

            // Broadcast to REMOTE clients only (exclude host)
            NotifyMovement msg;
            msg.ServerId = entity;
            msg.Position = movement.Position;
            GameServer::Get()->SendToPlayers(msg, pHost); // Excludes host!
        }
        else {
            // REMOTE-OWNED ENTITY: Already have network data
            // Just validate and broadcast to others
            BroadcastMovement(entity, pOwner);
        }
    }
}
```

### Phase 6: Physics Integration
**Goal:** Host runs real Skyrim physics for NPCs

**Tasks:**
1. Remove physics disable code in server
2. Add physics update calls for host-owned NPCs
3. Read physics results from engine
4. Broadcast physics state to clients

**Example:**
```cpp
void ServerCharacterService::UpdateNPCPhysics(entt::entity npc, float dt) {
    if (!IsHost()) return; // Only host runs physics

    Actor* pActor = m_engineInterface->GetActor(npc);

    // Enable and update physics (was disabled before!)
    pActor->SetPhysicsEnabled(true);
    m_engineInterface->UpdatePhysics(pActor, dt);

    // Read results from physics engine
    auto& movement = GetMovement(npc);
    movement.Position = pActor->pos;
    movement.Velocity = pActor->velocity;

    // Send to clients
    BroadcastPhysicsState(npc, movement);
}
```

### Phase 7: Testing
**Tasks:**
1. Test host mode with 1 client
2. Test host mode with 4 clients
3. Verify physics works correctly on host
4. Verify clients receive updates smoothly
5. Test proximity enforcement works
6. Test host disconnect handling

## File Structure

### New Files to Create
```
Code/client/Services/HostService.h       - Host mode management
Code/client/Services/HostService.cpp
Code/server/EngineInterface.h            - Abstract engine access
Code/client/EngineInterfaceImpl.h        - SKSE implementation
Code/client/EngineInterfaceImpl.cpp
```

### Files to Modify
```
xmake.lua                                 - Build configuration
Code/client/Services/TransportService.h   - Add IsHost() flag
Code/client/Services/TransportService.cpp
Code/client/main.cpp                      - Initialize HostService
Code/server/Services/CharacterService.cpp - Add engine access
Code/server/Services/CharacterService.h
Code/server/GameServer.h                  - Add engine interface
Code/server/GameServer.cpp
```

## Benefits

### What This Fixes
1. ✅ **Physics Sync Issues** - Host runs real Havok physics
2. ✅ **AI Sync Issues** - Host runs real NPC AI with pathfinding
3. ✅ **Quest Sync Issues** - Host has authoritative quest state
4. ✅ **Performance** - No need for separate server executable
5. ✅ **Simplicity** - One installation, one process
6. ✅ **Combat** - Host can resolve damage/hits authoritatively

### Challenges
1. ⚠️ **Build Complexity** - Need to compile server into client DLL
2. ⚠️ **Testing** - Harder to debug (can't attach to separate server)
3. ⚠️ **Migration** - Existing dedicated servers won't work
4. ⚠️ **Host Responsibility** - Host leaving = session ends

## References

Similar implementations:
- **Left 4 Dead** - Source Engine listen server
- **Minecraft** - Integrated server mode
- **Dark Souls** - Host-based P2P
- **Borderlands** - Host runs simulation
- **Valheim** - Host-based co-op

## Next Steps

1. Create this document ✅ (you are here)
2. Start with Phase 1: Build system changes
3. Implement Phase 2-3: HostService
4. Add Phase 4: Engine interface
5. Complete Phase 5-6: Host bypass and physics
6. Test Phase 7

## Notes

- Keep dedicated server support for now (optional backwards compatibility)
- Host can be any player (first to start hosting)
- Consider host migration in future (advanced feature)
- UI needs "Host Game" button to start embedded server
- Direct IP connect or Steam lobby for discovery
