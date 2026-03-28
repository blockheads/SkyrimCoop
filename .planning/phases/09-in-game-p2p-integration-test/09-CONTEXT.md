# Phase 9: In-Game P2P Integration Test - Context

**Gathered:** 2026-03-28
**Status:** Ready for planning (revised)

<domain>
## Phase Boundary

Build a comprehensive automated E2E testing framework and developer tooling for the SkyrimCoop P2P pipeline. The testing pyramid has four tiers:

1. **Headless multi-bot testing** (CI): Extend fake_player with `--bots N` and `--verify-broadcast` against `skyrim-coop --host-only`. No Skyrim needed.
2. **Mock relay pipeline** (CI): A lightweight MinGW binary (mock_skyrim_loader.exe) that loads the relay DLL under Wine without Skyrim, sends scripted HookEventPackets, proving the full DLL→TCP→native→GameServer→clients pipeline headlessly.
3. **Single Skyrim golden test** (pre-release): Fix test_ingame_sync.sh — one real Skyrim + N fake_players as the definitive integration test.
4. **Two-instance developer tool** (manual): A convenience script that spawns two Skyrim instances in 1280x720 windowed mode side-by-side for quick manual co-op iteration.

Additionally: hook event recording (`--record`) in the native process for future CI replay capability.

</domain>

<decisions>
## Implementation Decisions

### Testing Strategy (revised from original plan)
- **D-01:** Testing pyramid approach. Automated headless tests are the primary CI gate. Real Skyrim is for manual verification only.
- **D-02:** fake_player becomes the multi-bot stress test tool. `--bots N` spawns N concurrent MockClient threads. `--verify-broadcast` confirms each bot receives movement updates from others. `--metrics` outputs latency/throughput CSV.
- **D-03:** mock_skyrim_loader.exe is a ~100-line MinGW binary. Loads relay DLL via LoadLibrary under Wine, calls SKSEPlugin_Load(nullptr), sends scripted HookEventPackets over TCP. DLL already handles hook failure gracefully (relay_main.cpp line 308).
- **D-04:** test_ingame_sync.sh fixes: make fake_player default path (invert --with-fake-player to --no-fake-player), add GameServer port readiness check (ss -ulnp polling), add log collection to /tmp with --keep-logs.

### Success Criteria
- **D-05:** Headless CI pipeline passes: build → unit tests → `skyrim-coop --host-only` + `fake_player --bots 8 --verify-broadcast --duration 15`
- **D-06:** mock_skyrim_loader.exe loads DLL under Wine, relay_info.json appears, native process connects, fake_player authenticates through the full pipeline
- **D-07:** test_ingame_sync.sh works with single Skyrim + fake_player (5-stage pass)
- **D-08:** Developer two-instance tool spawns two 1280x720 windowed Skyrim instances side-by-side with separate Proton prefixes

### Two-Instance Developer Tool
- **D-09:** Separate script (tests/launch_two_skyrim.sh) — NOT an automated test, a developer convenience tool for quick manual co-op iteration.
- **D-10:** Both instances run windowed at 1280x720 via Skyrim.ini injection (bFull Screen=0, iSize H=720, iSize W=1280). Script positions them side-by-side via xdotool/wmctrl.
- **D-11:** Separate Proton prefixes (clone compatdata for join instance). Sequential launch with relay_info.json capture.
- **D-12:** Mouse unlock — inject bAlwaysActive=1 and set mouse cursor visible via INI tweaks so both windows are usable without fighting for mouse capture. Claude has discretion on the exact INI settings and whether an SSE Engine Fixes style borderless approach works better.
- **D-13:** Script handles launching both native processes (host with GameServer, join with --connect). On exit, cleans up both instances and cloned prefix.

### Hook Event Recording
- **D-14:** Add `--record <file>` flag to native process. Writes all received HookEventPackets to a binary log with timestamps. Future phases can build a replay tool for CI.

### Failure Diagnostics
- **D-15:** Log collection to temp directory for test_ingame_sync.sh. Script creates test run dir, redirects all process stderr/stdout. On failure, prints which log to check. --keep-logs preserves on pass.

### Claude's Discretion
- Exact mock_skyrim_loader.exe implementation (how it sends HookEventPackets, which events to script)
- fake_player --bots threading model (std::thread per bot vs async)
- Stress test default parameters (bot count, tick rate, duration)
- --record binary format (timestamp + raw packet bytes is fine)
- INI settings for mouse unlock and windowed mode
- Whether to use xdotool, wmctrl, or another tool for window positioning
- Whether mock_skyrim_loader needs a timeout or runs indefinitely until signaled
- Latency measurement approach in fake_player (embed sequence numbers in movement, measure round-trip)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Test Infrastructure (primary)
- `tests/test_ingame_sync.sh` — Existing integration test script with 5-stage pipeline
- `Code/tests/integration/fake_player.cpp` — fake_player binary with --self-test, --port, --verify-pid, --duration modes
- `Code/tests/integration/xmake.lua` — Build config for fake_player
- `Code/tests/mock_client.h` / `Code/tests/mock_client.cpp` — MockClient used by fake_player for enet6 connection

### Relay DLL (component under test)
- `Code/relay_dll/relay_main.cpp` — DLL entry point, TCP server, hook failure is non-fatal (line 308-310)
- `Code/relay_dll/hook_trampolines.cpp` — Hook installation and event forwarding
- `Code/relay_dll/tcp_server.cpp` — TCP server for DLL-to-native IPC
- `Code/relay_dll/protocol.h` — IPC protocol definition (HookEventPacket types, CommandPacket types)

### Native Client (component under test)
- `Code/native_client/main.cpp` �� Native process with --pid, --port, --connect, --server-port, --host-only args
- `Code/native_client/transport/LoopbackTransport.h` — In-process loopback for host player
- `Code/native_client/transport/ServerMessageRouter.h` — Routes server messages to host
- `Code/native_client/game_bridge/` — ProcMemReader, PointerTable, TcpClient, GameReader

### Embedded GameServer
- `Code/server/GameServer.h` / `Code/server/GameServer.cpp` — Server class with enet6
- `Code/server/Services/CharacterService.cpp` — Character sync and movement broadcast

### Deploy Script
- `deploy_and_test.sh` — Copies DLL + native binary to SKSE plugins dir

### Prior Phase Context
- `.planning/phases/07-native-linux-build-with-skse-tcp-relay/07-CONTEXT.md` — Relay architecture decisions
- `.planning/phases/08-p2p-embedded-gameserver/08-CONTEXT.md` — Embedded GameServer decisions, loopback transport, host/join mode

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **test_ingame_sync.sh**: Full 5-stage pipeline with Proton launch, INI injection. Needs fixing, not rewriting.
- **fake_player**: Connects to GameServer, sends scripted movements. MockClient has OnConsume() that parses server messages — extend for broadcast verification.
- **skyrim-coop --host-only**: Already creates a fully functional embedded GameServer without Skyrim. Perfect for headless testing.
- **MockClient**: Thread-safe enet6 client wrapper. Can be instantiated multiple times for multi-bot.
- **relay_main.cpp hook failure**: Lines 308-310 show hooks fail gracefully — DLL continues without them. This is what makes mock_skyrim_loader possible.

### Key Insight: DLL Doesn't Need SKSE
relay_main.cpp D-11: SKSEPlugin_Load ignores the SKSEInterface pointer. The DLL only needs LoadLibrary + SKSEPlugin_Load(nullptr) to start its TCP server and write relay_info.json. Hook installation fails but that's non-fatal.

### Integration Points
- DLL TCP server port is dynamic (read from relay_info.json)
- GameServer listens on UDP port 10578 (enet6)
- protocol.h defines HookEventPacket and CommandPacket types for TCP IPC
- fake_player connects to GameServer on localhost:10578

</code_context>

<specifics>
## Specific Ideas

- The mock_skyrim_loader is the most creative piece — it proves the relay pipeline without Skyrim by exploiting the DLL's graceful hook failure.
- Multi-bot fake_player is highest leverage — `--host-only` + `--bots 20 --verify-broadcast` gives you a full headless integration test in CI.
- The two-instance developer tool is about ergonomics, not automation — 1280x720 windowed, side-by-side, mouse unlocked, one script to launch/teardown.
- Hook event recording is a low-effort investment that pays off later when you build the replay tool.
- Stress testing becomes trivial: `--bots 50 --tick-rate 60` = 3000 msgs/sec. Find your ceiling.

</specifics>

<deferred>
## Deferred Ideas

- Hook event replay tool (build the replayer in a future phase, Phase 9 just adds --record)
- Console command injection via relay protocol (useful for game state automation)
- Visual regression via screenshot comparison (for mod compatibility)
- Papyrus script automation for in-game test scenarios

</deferred>

---

*Phase: 09-in-game-p2p-integration-test*
*Context gathered: 2026-03-28 (revised with E2E testing strategy)*
