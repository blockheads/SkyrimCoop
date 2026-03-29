# Phase 10: Two-Player Co-op Session - Context

**Gathered:** 2026-03-28 (updated 2026-03-28)
**Status:** Ready for planning

<domain>
## Phase Boundary

Two Skyrim SE instances running on one Linux machine under Proton. One hosts (embedded GameServer), the other connects as client via localhost. Both players see each other's character model and sync movement in real-time. No UI, no Steam invites, no inventory/combat sync — just character presence and movement.

</domain>

<decisions>
## Implementation Decisions

### JoinClient Routing
- **D-01:** Reuse existing `ServerMessageRouter` for JoinClient message routing — same code path as host loopback, already handles auth + move messages
- **D-02:** JoinClient sends movement data via direct enet6 send (serialize `ClientReferencesMoveRequest` and send to host's GameServer like any other client packet)
- **D-03:** JoinClient::OnConsume() must be wired into ServerMessageRouter instead of just logging opcodes

### Character Assignment Pipeline
- **D-04:** ServerMessageRouter must be extended to handle character assignment messages (currently only handles `AuthenticationResponse` + `ServerReferencesMoveRequest`)
- **D-05:** When a player connects, the server must send character assignment to the remote client so it knows which formId represents the other player
- **D-06:** `CharacterService::OnAssignCharacter()` already creates ECS entities with RemoteComponent + InterpolationComponent — this just needs to be triggered by the network path

### DLL Command Execution
- **D-07:** Implement CMD_SET_POSITION in the DLL's DrainCommandQueue (currently a stub that only logs)
- **D-08:** Hook DrainCommandQueue into the game thread via a once-per-frame function — research CommonLibSSE (specifically `TESObjectREFR.cpp` and related) to find the right tick point instead of using HookActorProcess (which fires per-actor)
- **D-09:** Only implement CMD_SET_POSITION for this phase — other CMD_* opcodes deferred

### Two-Instance Setup
- **D-10:** Two separate Proton prefixes (STEAM_COMPAT_DATA_PATH) for each Skyrim instance — completely separate Wine environments, no file locking conflicts
- **D-11:** CLI port override for relay DLL TCP ports — host uses 9667, client uses 9668 (relay DLL already configurable via --port)
- **D-12:** Create a launch script that starts both instances (host Skyrim + native_client host mode, then client Skyrim + native_client join mode)

### Connection Model
- **D-13:** Direct IP connection to localhost — no Steam invites, no lobby system, no discovery
- **D-14:** Host starts embedded GameServer (already implemented), client connects via `--connect 127.0.0.1:10578`
- **D-15:** Two separate Skyrim instances on same machine, each with its own Proton prefix/SKSE/relay DLL/native process

### Sync Scope
- **D-16:** Movement sync only — both players see each other's character model and position updates
- **D-17:** No inventory sync, no combat sync, no quest sync, no spell sync for this phase
- **D-18:** Host's loaded cell is authoritative — client must be in the same cell to see the host character

### Success Validation
- **D-19:** Success = visual movement sync — both players see each other's character model moving in real-time in Riverwood
- **D-20:** Manual visual verification by user — no formal documentation artifacts required
- **D-21:** Test in Riverwood (sStartingCell=Riverwood) for both instances

### Claude's Discretion
- Character model selection for remote player (can use a default NPC appearance)
- Network tick rate and interpolation smoothing approach
- Specific CommonLibSSE function to hook for game thread tick (research required)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Architecture (Native Client)
- `Code/native_client/main.cpp` — Native client entry point, JoinClient class, host/client mode, event loop
- `Code/native_client/Services/CharacterService.cpp` — Actor spawning, movement sync, OnAssignCharacter, SyncMovement, ApplyRemotePositions
- `Code/native_client/Services/CharacterService.h` — TrackedActor, RemoteComponent, InterpolationComponent, AnimationComponent structs
- `Code/native_client/transport/ServerMessageRouter.cpp` — Routes server messages to CharacterService (auth + move only — needs AssignCharacter)
- `Code/native_client/transport/ServerMessageRouter.h` — Router interface with auth/move callbacks
- `Code/native_client/transport/LoopbackTransport.cpp` — Host-side loopback transport to embedded GameServer

### Relay DLL (Command Execution)
- `Code/relay_dll/relay_main.cpp` — DrainCommandQueue stub (lines 113-124), needs game thread hook
- `Code/relay_dll/hook_trampolines.cpp` — All hook entries including SetPosition (19790), HookActorProcess
- `Code/relay_dll/command_queue.h` — SPSC command queue, CommandSlot struct
- `Code/relay_dll/protocol.h` — CMD_SET_POSITION (0x8001) and other command opcodes

### Networking / Server
- `Code/server/GameServer.cpp` — Embedded server, player connection handling
- `Code/server/Services/CharacterService.cpp` — Server-side actor state, broadcast to players
- `Code/server/Services/PlayerService.cpp` — Player authentication, connection management
- `Code/encoding/Messages/` — Network message definitions (58 client, 61 server opcodes)

### External Reference (Research Required)
- `https://github.com/Ryan-rsm-McKenzie/CommonLibSSE/blob/master/src/RE/T/TESObjectREFR.cpp` — Find appropriate once-per-frame game thread hook for DrainCommandQueue

### Prior Phase Context
- `.planning/phases/09.1-dll-side-formid-resolution/09.1-03-SUMMARY.md` — FormId resolution verification results
- `.planning/phases/09.1-dll-side-formid-resolution/09.1-RESEARCH.md` — DLL-side formId research

### Test Infrastructure
- `tests/test_ingame_sync.sh` — Existing in-game test script (single instance)
- `Code/tests/integration/fake_player.cpp` — Fake player for headless testing

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **GameServer**: Embedded server already works — host mode with enet6 on UDP 10578
- **JoinClient**: enet6 client class exists in main.cpp, handles connect/disconnect/auth send — just needs message routing
- **ServerMessageRouter**: Handles auth + move messages, needs AssignCharacter extension
- **CharacterService**: Full remote character lifecycle (assign, move, remove, animate) already implemented
- **LoopbackTransport**: Host-side transport to embedded GameServer
- **CommandQueue**: SPSC ring buffer for native→DLL commands, just needs DLL-side execution
- **fake_player**: Connects to GameServer, sends mock data — proves networking works

### Established Patterns
- Hook trampolines → TCP → native process → DispatchHookEvent → services
- enet6 UDP for GameServer ↔ client communication
- EnTT ECS for entity/component management on both client and server
- Differential serialization for movement updates (ClientReferencesMoveRequest / ServerReferencesMoveRequest)

### Key Gaps Identified
1. **JoinClient::OnConsume()** just logs — needs ServerMessageRouter wiring
2. **ServerMessageRouter** missing AssignCharacter handler — can't spawn remote characters
3. **DrainCommandQueue()** is a stub — CMD_SET_POSITION doesn't execute
4. **DrainCommandQueue** not hooked into game thread — `[[maybe_unused]]`
5. **JoinClient** can't send movement data — needs CharacterService integration for client mode

### Integration Points
- JoinClient needs ServerMessageRouter (same as host's loopback path)
- CharacterService needs an ITransport-like send mechanism for client mode (currently only has loopback)
- DLL needs game thread hook to drain command queue
- Launch script needs to orchestrate two Proton prefixes + two native_client instances

</code_context>

<specifics>
## Specific Ideas

- Two Skyrim instances on the same machine with separate Proton prefixes
- Start both in Riverwood for testing (sStartingCell injection already implemented)
- Research CommonLibSSE TESObjectREFR.cpp for the right game thread tick function to hook DrainCommandQueue
- User wants to verify visually (walk around Riverwood, see other player follow)

</specifics>

<deferred>
## Deferred Ideas

- **Network stress testing**: Packet drop simulation, latency injection, desync detection
- **Steam invite system**: Replace direct IP with Steam friend invite flow
- **Combat/inventory/quest sync**: Beyond movement — full gameplay sync
- **UI overlay**: In-game connection UI, player list, health bars
- **Full CMD_* implementation**: Only CMD_SET_POSITION for now, rest deferred
- **Transport abstraction**: ITransport interface for CharacterService (loopback vs enet) — do direct enet send for now

</deferred>

---

*Phase: 10-two-player-co-op-session*
*Context gathered: 2026-03-28*
