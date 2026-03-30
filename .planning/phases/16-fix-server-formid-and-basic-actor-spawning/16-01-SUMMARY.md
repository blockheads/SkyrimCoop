---
phase: 16-fix-server-formid-and-basic-actor-spawning
plan: 01
subsystem: encoding, server
tags: [networking, worldstate, reconciliation, unreliable-channel]
dependency_graph:
  requires: []
  provides: [WorldStateSync-message, unreliable-movement-channel]
  affects: [server-CharacterService, encoding-opcodes, server-GameServer, server-Player]
tech_stack:
  added: []
  patterns: [periodic-broadcast-timer, unreliable-sequenced-channel]
key_files:
  created:
    - Code/encoding/Messages/WorldStateSync.h
    - Code/encoding/Messages/WorldStateSync.cpp
  modified:
    - Code/encoding/Opcodes.h
    - Code/encoding/Messages/ServerMessageFactory.h
    - Code/server/Services/CharacterService.h
    - Code/server/Services/CharacterService.cpp
    - Code/server/GameServer.h
    - Code/server/GameServer.cpp
    - Code/server/Game/Player.h
    - Code/server/Game/Player.cpp
decisions:
  - WorldStateSync broadcasts every 1 second as reconciliation safety net (D-08/D-10)
  - Movement updates sent via kUnreliable channel to avoid retransmitting stale positions (D-14b)
  - Send() overload with EPacketFlags added to both GameServer and Player for flag propagation
metrics:
  duration: 3min
  completed: "2026-03-30"
  tasks: 2
  files: 10
---

# Phase 16 Plan 01: WorldStateSync Message & Unreliable Movement Channel Summary

WorldStateSync message with 1-second periodic broadcast for client reconciliation safety net, plus unreliable sequenced channel for position updates to eliminate stale retransmission overhead.

## Tasks Completed

### Task 1: Define WorldStateSync message and register opcode
- **Commit:** 23d6d2f5
- Added `kWorldStateSync` opcode to ServerOpcode enum before kServerOpcodeMax
- Created WorldStateSync message struct with EntityEntry sub-struct (ServerId, Position as Vector3_NetQuantize, Rotation as Rotator2_NetQuantize)
- Implemented SerializeRaw/DeserializeRaw using VarInt + NetQuantize serialization patterns
- Registered WorldStateSync in ServerMessageFactory Visit template
- SkyrimEncoding target builds clean

### Task 2: Add 1-second WorldStateSync broadcast + unreliable channel
- **Commit:** 9d01492a
- Added `BroadcastWorldStateSync()` to server CharacterService with 1-second chrono timer
- Iterates all entities with MovementComponent + CharacterComponent, packs positions into WorldStateSync
- Broadcasts via `GameServer::SendToPlayers()` to all connected players
- Added `Send(ConnectionId_t, ServerMessage&, EPacketFlags)` overload to GameServer (delegates from existing Send)
- Added `Send(ServerMessage&, EPacketFlags)` overload to Player
- Changed ProcessMovementChanges to send via `kUnreliable` flag -- stale positions are worthless, retransmission wastes bandwidth
- SkyrimTogetherServer target builds clean

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added ENetInterface.hpp include to Player.h**
- **Found during:** Task 2
- **Issue:** Player.h forward-declares ServerMessage but had no access to TiltedPhoques::EPacketFlags enum needed for the new Send overload
- **Fix:** Added `#include <ENetInterface.hpp>` to Player.h
- **Files modified:** Code/server/Game/Player.h
- **Commit:** 9d01492a

## Verification Results

- `xmake build SkyrimEncoding` -- PASSED
- `xmake build SkyrimTogetherServer` -- PASSED
- `grep kWorldStateSync Opcodes.h` -- found at line 111
- `grep WorldStateSync ServerMessageFactory.h` -- found in include and Visit template
- `grep kUnreliable CharacterService.cpp` -- found at line 656

## Known Stubs

None -- all data sources are wired to live ECS components.
