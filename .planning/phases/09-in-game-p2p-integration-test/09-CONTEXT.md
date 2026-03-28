# Phase 9: In-Game P2P Integration Test - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Full end-to-end validation of the P2P co-op architecture: a real Skyrim SE instance running under Proton loads the relay DLL, which spawns the native process with embedded GameServer. fake_player connects via enet6 and verifies auth + movement sync. Additionally, a second real Skyrim instance connects in join mode (relay DLL + native process with --connect) to prove the real two-player co-op flow. The existing test_ingame_sync.sh script is the foundation — Phase 9 fixes whatever breaks and extends it to cover the full P2P pipeline including two-instance testing.

</domain>

<decisions>
## Implementation Decisions

### Test Orchestration
- **D-01:** Fix-and-validate approach. Run the existing `test_ingame_sync.sh` script, identify what breaks at each stage (DLL load, relay handshake, native spawn, GameServer start, fake_player connect), fix issues iteratively. The script structure is sound — the bugs are in the components it orchestrates.
- **D-02:** The existing `--with-fake-player` flag becomes the default test path (not opt-in). The Phase 8 embedded GameServer makes this possible now.
- **D-03:** Script launches Skyrim directly via `proton run skse64_loader.exe` (bypassing MO2). This is correct for testing — no mods needed.

### Success Criteria
- **D-04:** All 5 stages must pass for Phase 9 PASS:
  1. Relay DLL loads into Skyrim via SKSE
  2. Relay TCP connection established (DLL writes relay_info.json)
  3. Native process starts and GameServer begins listening on port 10578
  4. fake_player authenticates and receives character assignment
  5. Movement broadcast: fake_player sends positions, receives host position updates
- **D-05:** fake_player verifies host player position via `/proc/pid/mem` (reads Skyrim's game memory to confirm the actor actually exists/moved in-game). This is the ultimate proof that the full loop works: game state → DLL hooks → native process → GameServer → fake_player.

### Two-Player Testing
- **D-06:** Scope includes two real Skyrim instances: one as host (relay DLL + native process with GameServer), one as join client (relay DLL + native process with `--connect host_ip:10578`). This proves the real co-op experience.
- **D-07:** Both instances use the same DLL and native binary. Instance role (host vs join) is configured per-instance. Claude has discretion on the mechanism (environment variable, separate Proton prefixes, or config file) — whatever works cleanly with Wine/Proton.
- **D-08:** The test script should automate launching both instances for quick iteration during development.

### Failure Diagnostics
- **D-09:** Log collection to temp directory. Script creates a test run directory, redirects all process stderr/stdout to log files, prints relevant stage logs on failure. On PASS, logs are cleaned up (or kept with `--keep-logs`).
- **D-10:** Each stage timeout prints which log file to check for diagnosis.

### Claude's Discretion
- Total test timeout (existing per-stage timeouts: 120s Skyrim boot, 90s server ready, 30s fake_player — likely ~5min total with buffer)
- How to differentiate host vs join instance configuration (env var, config file, or separate Proton prefixes)
- Exact /proc/pid/mem verification assertions in fake_player (which actor fields to check)
- Whether to add a deploy step to the script (copy DLL + native binary to SKSE plugins dir) or assume pre-deployed
- Log format and directory naming convention for test runs
- Whether the two-Skyrim test is a separate script or a mode flag on test_ingame_sync.sh

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Test Infrastructure (primary — this is what we're fixing)
- `tests/test_ingame_sync.sh` — Existing integration test script with 5-stage pipeline, INI injection, Proton launch, relay wait, fake_player stub
- `Code/tests/integration/fake_player.cpp` — fake_player binary with --self-test, --port, --verify-pid, --duration modes
- `Code/tests/integration/xmake.lua` — Build config for fake_player
- `Code/tests/mock_client.h` / `Code/tests/mock_client.cpp` — MockClient used by fake_player for enet6 connection

### Relay DLL (component under test)
- `Code/relay_dll/relay_main.cpp` — DLL entry point, TCP server, process launcher
- `Code/relay_dll/hook_trampolines.cpp` — Hook installation and event forwarding
- `Code/relay_dll/tcp_server.cpp` — TCP server for DLL-to-native IPC
- `Code/relay_dll/process_launcher.cpp` — Native process spawning
- `Code/relay_dll/protocol.h` — IPC protocol definition (shared with native client)

### Native Client (component under test)
- `Code/native_client/main.cpp` — Native process entry point with --pid, --port, --connect, --server-port, --host-only args
- `Code/native_client/transport/LoopbackTransport.h` — In-process loopback for host player
- `Code/native_client/transport/ServerMessageRouter.h` — Routes server messages to host
- `Code/native_client/game_bridge/` — ProcMemReader, PointerTable, TcpClient, GameReader

### Embedded GameServer (component under test)
- `Code/server/GameServer.h` / `Code/server/GameServer.cpp` — Server class with enet6
- `Code/server/Services/CharacterService.cpp` — Character sync and movement broadcast

### Deploy Script
- `deploy_and_test.sh` — Existing deploy script (copies DLL + native binary to SKSE plugins dir)

### Prior Phase Context
- `.planning/phases/07-native-linux-build-with-skse-tcp-relay/07-CONTEXT.md` — Relay architecture decisions
- `.planning/phases/08-p2p-embedded-gameserver/08-CONTEXT.md` — Embedded GameServer decisions, loopback transport, host/join mode

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **test_ingame_sync.sh**: Full 5-stage pipeline with Proton launch, INI injection (sStartingCell=Riverwood), relay wait, cleanup traps, per-stage timeouts. Needs fixing, not rewriting.
- **fake_player**: Connects to GameServer, sends scripted movements, has --verify-pid for /proc/pid/mem verification. Reuses MockClient from Phase 5.
- **deploy_and_test.sh**: Copies built DLL + native binary to SKSE plugins directory. May be useful as a pre-step.

### Established Patterns
- Script uses `pgrep -f "SkyrimSE.exe"` to detect Skyrim process
- DLL writes `skyrim_coop_relay.json` with port and PID for native process discovery
- Per-stage timeouts with `seq 1 $TIMEOUT` polling loops
- Cleanup trap restores Skyrim.ini and kills child processes on exit

### Integration Points
- DLL must be in `$SKYRIM_DIR/Data/SKSE/Plugins/skyrim_coop_hooks.dll`
- Native binary must be in `$SKYRIM_DIR/Data/SKSE/Plugins/skyrim-coop`
- GameServer listens on UDP port 10578 (enet6)
- Relay DLL TCP server port is dynamic (read from relay_info.json)
- fake_player connects to GameServer on localhost:10578

</code_context>

<specifics>
## Specific Ideas

- The character of this phase is **integration debugging** — the script and components exist, the job is making them work together end-to-end.
- Two-Skyrim automated testing is a major productivity win for ongoing development — quick iteration on co-op behavior.
- /proc/pid/mem verification is the ultimate proof: game state → DLL hooks → native → GameServer → fake_player reads Skyrim memory and confirms the actor exists.
- The script currently bypasses MO2 and launches Skyrim directly via Proton — this is correct for testing since no mods are needed.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 09-in-game-p2p-integration-test*
*Context gathered: 2026-03-28*
