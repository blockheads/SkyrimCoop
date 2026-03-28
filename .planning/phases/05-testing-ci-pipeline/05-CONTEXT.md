# Phase 5: Testing & CI Pipeline - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Automated networking verification for the relay architecture. Prove that the DLL↔native TCP relay and native↔embedded server networking actually work end-to-end, with a stretch goal of in-game verification using a fake second player.

Also: remove dead standalone server code — SkyrimCoop is P2P host-only, the old dedicated server path should be cleaned up.

CI pipeline is explicitly deferred — not part of this phase.
Fuzz testing is explicitly deferred — not part of this phase.

</domain>

<decisions>
## Implementation Decisions

### D-01: Standalone server removal
Remove all standalone/dedicated server logic. SkyrimCoop only supports embedded server (P2P host mode). The old server_runner, server list, authentication via server password, and any connect-to-remote-server flow should be removed or gutted. Only the embedded server path (host player runs GameServer internally) remains.

### D-02: Mock client scope — test both relay AND game server
The mock client / test harness should test:
- **TCP relay protocol**: DLL↔native communication (hook events, commands, control messages)
- **Game server networking**: native↔embedded server (player connection, character sync, movement)
Both paths need validation since they're both part of the live pipeline.

### D-03: Automated in-game verification (stretch goal)
The ultimate test: launch real Skyrim with the DLL, connect a fake second player via test harness, then verify:
- Remote actor spawns in the real Skyrim game world
- Test harness sends scripted position updates
- Verify via /proc/pid/mem that the remote actor's position matches expected coordinates
- Compare expected vs actual within tolerance

The test harness acts as a "robot player" — connects to the embedded server, sends movement/sync messages, and the real Skyrim should react. Verification reads game memory to confirm.

### D-04: Test layering — lightweight first, then integration
Build tests bottom-up:
1. **Message round-trip tests** — serialize/deserialize all message types (expand existing Catch2 tests)
2. **Mock client handshake** — headless process connects to embedded GameServer, completes auth + character assign
3. **Relay integration** — mock DLL sends hook events over TCP to native process, verify service dispatch
4. **In-game integration** — real Skyrim + fake second player + memory verification (stretch goal)

### D-05: Test target — embedded server only
All server-side tests target the embedded server (host mode). No standalone server testing — that code path is being removed in D-01.

### Claude's Discretion
- Test framework choices beyond existing Catch2 (for integration tests, may need a different harness)
- Whether the fake player test harness is a separate binary or a mode of skyrim-coop
- How to structure the /proc/pid/mem verification (inline in test vs separate validation pass)
- Priority ordering of plans within the phase

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Relay Architecture (Phase 7)
- `.planning/phases/07-native-linux-build-with-skse-tcp-relay/07-CONTEXT.md` — All relay architecture decisions
- `.planning/phases/07-native-linux-build-with-skse-tcp-relay/07-RESEARCH.md` — TCP protocol, proc_memory, service rewrite patterns
- `Code/relay_dll/protocol.h` — Wire format for hook events, commands, control messages
- `Code/native_client/game_bridge/` — GameReader, ProcMemReader, PointerTable, TcpClient APIs

### Existing Tests
- `Code/tests/relay_protocol_tests.cpp` — Existing relay protocol unit tests
- `Code/tests/command_queue_tests.cpp` — SPSC queue tests
- `Code/tests/proc_memory_tests.cpp` — /proc/pid/mem self-tests
- `Code/tests/pointer_table_tests.cpp` — FormId-to-pointer mapping tests
- `Code/tests/encoding.cpp` — Existing encoding round-trip tests
- `tests/smoke_tcp_relay.sh` — Build + export verification smoke test
- `tests/smoke_ptrace.sh` — ptrace environment validation

### Server Architecture
- `Code/server/GameServer.h` — Embedded server class
- `Code/server/Services/PlayerService.cpp` — Authentication + player connection
- `Code/server/Services/CharacterService.cpp` — Actor assignment + movement sync
- `Code/encoding/Messages/` — All network message definitions (58 client, 61 server opcodes)
- `Code/encoding/Opcodes.h` — Opcode enums

### Deploy/Test Infrastructure
- `deploy_and_test.sh` — Automated Skyrim launch via Proton + SKSE with native process connection

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **GameReader API** (`Code/native_client/game_bridge/game_reader.h`): Can be reused in test harness for memory verification
- **ProcMemReader** (`Code/native_client/game_bridge/proc_memory.h`): Self-test pattern already exists, can verify remote actor positions
- **TcpClient** (`Code/native_client/game_bridge/tcp_client.h`): Can connect test harness to DLL's TCP server
- **Existing Catch2 setup** (`Code/tests/main.cpp`): 23 test cases, 905 assertions — extend this
- **SkyrimEncoding messages**: All message types (ClientReferencesMoveRequest, ServerAssignCharacterRequest, etc.) compile natively and can be used in test harness
- **deploy_and_test.sh**: Already launches Skyrim via Proton, connects native process, can be extended for test scenarios

### Established Patterns
- Catch2 for unit tests with tag-based filtering (`[RelayProtocol]`, `[CommandQueue]`, etc.)
- Shell scripts for integration/smoke tests
- /proc/pid/mem for reading game state without game APIs

### Integration Points
- Test harness connects to embedded GameServer as a second "player"
- Test harness sends/receives the same message types as a real client
- Memory verification reads from the same /proc/pid/mem path as the native client

</code_context>

<specifics>
## Specific Ideas

- The fake second player should be "controllable" — send scripted movements, not just connect
- Verification should be automated: compare expected positions with actual game memory reads
- The end goal is confidence that networking logic works without manual testing

</specifics>

<deferred>
## Deferred Ideas

- **CI pipeline** (GitHub Actions) — explicitly deferred, can be its own phase later
- **Fuzz testing** (TEST-05) — deferred to a future phase
- **Two real Skyrim instances** — not feasible for automated testing, fake player approach is the alternative

</deferred>

---

*Phase: 05-testing-ci-pipeline*
*Context gathered: 2026-03-28*
