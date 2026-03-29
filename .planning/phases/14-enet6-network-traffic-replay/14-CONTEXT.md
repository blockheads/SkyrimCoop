# Phase 14: enet6 Network Traffic Replay - Context

**Gathered:** 2026-03-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Extend the replay infrastructure to capture and replay full enet6 network traffic (ClientMessage/ServerMessage) as NDJSON alongside existing DLL hook events. Per-peer recording with both directions (c2s + s2c) in each file. A combined dual harness replays both NDJSON hook events and enet6 messages independently from recorded files. Comprehensive opcode coverage with hardcoded Catch2 assertions. Enables offline regression testing of the full multiplayer data path without launching Skyrim.

</domain>

<decisions>
## Implementation Decisions

### Recording Format
- **D-01:** NDJSON format for enet6 traffic, consistent with Phase 12/13 hook-event recordings. Each line is a JSON object with message type name, opcode (metadata), direction, timestamp, and serialized payload fields.
- **D-02:** Message type names (e.g. "AuthenticationRequest", "NotifySpawnData") as the primary identifier per record. Numeric opcode included as metadata but type name is the primary key. Human-readable, self-documenting fixtures.

### Capture Points & Dual Recording
- **D-03:** Recording taps at the application layer — GameServer::Send() for outgoing ServerMessages and GameServer::OnConsume() for incoming ClientMessages. NOT at the enet6 transport layer (avoids handshake noise, keepalives, fragmentation, retransmissions, ACKs).
- **D-04:** Separate NDJSON file per connected peer. Each file contains both directions (c2s + s2c) for that peer, tagged with a "dir" field. Self-contained session transcript per player.

### Replay Harness Design
- **D-05:** Combined dual harness that wraps both the existing ReplayHarness (native_client hook events) and a new server-side component (GameServer + server World + server services with stubs). One harness class can replay both NDJSON hook events and enet6 messages.
- **D-06:** Independent replay — each side replays from its own recorded file. Server gets recorded ClientMessages, client gets recorded ServerMessages. No live wiring between sides. Deterministic, debuggable, failures are isolatable to one side.

### Test Scope & Assertions
- **D-07:** Comprehensive opcode coverage — record and test whatever the live session produces. Auth + AssignCharacter, CharacterSpawn + movement, inventory + equipment, and all other opcodes that appear in the recording.
- **D-08:** Hardcoded Catch2 assertions with explicit expected values, same pattern as Phase 12/13. REQUIRE/CHECK with expected values. A regression = test failure. Protocol changes break fixtures = re-record signal.

### Claude's Discretion
- How to serialize ClientMessage/ServerMessage fields to JSON (manual per-message or reflection-based approach using existing Serialize/Deserialize methods)
- ServerReplayHarness internal architecture — how to stub enet6 connections for the server World while keeping services functional
- Whether the combined harness is one class or a thin coordinator over two independent harness classes
- NDJSON field schema for enet6 messages (exact field names, how to represent binary payload fields in JSON)
- How to trigger recording (--record-enet flag, config option, or automatic when --record is active)
- Fixture organization (subdirectory per session, naming convention for per-peer files)
- Which server services need stub/mock dependencies for replay mode
- Test file organization (one test file per message category vs grouped)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Server Message Processing (recording tap points)
- `Code/server/GameServer.cpp` — `OnConsume()` (line ~617) processes incoming ClientMessages; `Send()` (line ~719) serializes outgoing ServerMessages. These are the recording insertion points.
- `Code/server/GameServer.h` — Server interface including `OnConsume`, `Send`, `InjectMessage`, `SetLoopbackRoute`, `m_messageHandlers` array

### Message Encoding (serialization schema)
- `Code/encoding/Messages/ClientMessageFactory.h` — Factory for deserializing client messages by opcode
- `Code/encoding/Messages/ServerMessageFactory.h` — Factory for deserializing server messages by opcode
- `Code/encoding/Opcodes.h` — All opcode enums (58 client, 61 server)
- `Code/encoding/Messages/` — All message structs with Serialize/Deserialize methods

### Existing Replay Infrastructure (Phase 12/13 foundation)
- `Code/native_client/replay/replay_harness.h` — ReplayHarness class with all 8 native_client services, stub dependencies
- `Code/native_client/replay/replay_harness.cpp` — ReplayFile() implementation, NDJSON dispatch loop
- `Code/native_client/dispatch_hook_event.h` — DispatchHookEvent() shared dispatch function

### Existing Test Patterns
- `Code/tests/replay/replay_tests.cpp` — CharacterService replay tests (pattern to follow)
- `Code/tests/replay/` — All per-service replay tests (weather, calendar, quest, combat, inventory, actorvalue, magic, integration)
- `Code/tests/replay/xmake.lua` — ReplayTests build target

### Transport Infrastructure
- `Code/native_client/transport/LoopbackTransport.h` — In-process host message routing (reference for how messages flow without enet6)
- `Code/libraries/networking/Client.hpp` — TiltedPhoques::Client base class
- `Code/libraries/networking/Server.cpp` — TiltedPhoques::Server base class with enet6 integration

### Server Services (test targets)
- `Code/server/Services/` — All server-side services (PlayerService, CharacterService, PartyService, etc.)
- `Code/server/Components/` — Server-side ECS components

### Fixtures
- `tests/fixtures/replay/` — Existing NDJSON fixtures from Phase 12/13 (pattern for file organization)

### Prior Phase Context
- `.planning/phases/12-ndjson-replay-capture-and-integration-tests/12-CONTEXT.md` — Phase 12 decisions (D-01 through D-10)
- `.planning/phases/13-network-replay-recording-and-regression-testing/13-CONTEXT.md` — Phase 13 decisions (D-01 through D-10), deferred item that became this phase

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ReplayHarness` class: Instantiates all 8 native_client services with stub dependencies. The combined harness wraps this for the client side.
- `GameServer::InjectMessage()`: Already exists — injects raw binary data into `OnConsume()`. Useful for feeding recorded messages into the server during replay.
- `GameServer::SetLoopbackRoute()`: Routes server output to a callback instead of enet6. Can capture ServerMessages during replay.
- `LoopbackTransport`: Demonstrates in-process message routing pattern between client and server without network.
- `ClientMessageFactory` / `ServerMessageFactory`: Deserialize binary buffers into typed message objects. Needed for NDJSON serialization of recorded traffic.
- Existing `--record` flag in native_client `main.cpp`: Pattern for recording NDJSON hook events. enet6 recording follows same pattern.

### Established Patterns
- NDJSON: One JSON object per line, `op` field for dispatch, fields match the message struct. Phase 11/12/13 established this.
- ReplayHarness: Stub dependencies (fd=-1, socket=-1), fast-forward, fixed deltaTime (1/60s), Catch2 assertions.
- Test structure: `TEST_CASE("description", "[Replay][ServiceName]")`, ReplayHarness instantiation, ReplayFile(), REQUIRE/CHECK.
- Fixtures: Curated golden files in `tests/fixtures/replay/`, from live recordings, never hand-edited.

### Integration Points
- GameServer::Send() — Insert recording tap here to capture outgoing ServerMessages as NDJSON
- GameServer::OnConsume() — Insert recording tap here to capture incoming ClientMessages as NDJSON
- New `ServerReplayHarness` or combined harness links against server libraries + encoding libraries + Catch2
- New test files in `Code/tests/replay/` or `Code/tests/enet6_replay/` following existing xmake patterns

</code_context>

<specifics>
## Specific Ideas

- This phase is the "server-side ENet message recording and replay" explicitly deferred from Phase 13.
- Per-peer files mean each player's session is a self-contained NDJSON transcript — useful for debugging specific player issues.
- The combined harness enables future tests that validate both sides simultaneously, even though Phase 14 replays each side independently from recorded files.
- Recording at the application layer (not transport) means fixtures are meaningful game data without enet6 noise.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 14-enet6-network-traffic-replay*
*Context gathered: 2026-03-29*
