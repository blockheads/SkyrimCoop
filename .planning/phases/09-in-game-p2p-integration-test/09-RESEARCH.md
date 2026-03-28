# Phase 9: In-Game P2P Integration Test - Research

**Researched:** 2026-03-28
**Domain:** Integration testing, Proton/Wine process orchestration, enet6 networking verification, /proc/pid/mem game state validation
**Confidence:** HIGH

## Summary

Phase 9 is an integration debugging phase -- all components exist from Phases 5, 7, and 8. The test script (`test_ingame_sync.sh`), fake_player binary, relay DLL, native client with embedded GameServer, and deploy script are all built and deployed. The work is making them work together end-to-end and extending coverage to include: (1) fake_player as the default test path per D-02, (2) /proc/pid/mem verification per D-05, and (3) two real Skyrim instances testing per D-06.

Three critical issues need resolution: First, the test script manually launches the native process (line 265), but the DLL also auto-spawns it via ProcessLauncher -- this will cause two competing native processes. **However, re-reading the DLL code more carefully, the DLL's MonitorThread waits for an AcceptClient() connection, and auto-restart only triggers after a disconnect.** The DLL does NOT do initial launch via ProcessLauncher::Launch() in SKSEPlugin_Load(). The initial launch IS external (by the script or deploy_and_test.sh). So the manual launch is correct. Second, two Skyrim instances require separate Proton prefixes to avoid file-lock conflicts. Third, fake_player's `--verify-pid` flag is parsed but the actual /proc/pid/mem verification logic in RunExternal() is not implemented.

**Primary recommendation:** Fix and extend the existing test_ingame_sync.sh iteratively -- make fake_player default, add GameServer port readiness wait, add log collection, then create a separate two-instance test script.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Fix-and-validate approach. Run the existing test_ingame_sync.sh script, identify what breaks at each stage, fix issues iteratively. The script structure is sound -- the bugs are in the components it orchestrates.
- **D-02:** The existing `--with-fake-player` flag becomes the default test path (not opt-in). The Phase 8 embedded GameServer makes this possible now.
- **D-03:** Script launches Skyrim directly via `proton run skse64_loader.exe` (bypassing MO2). Correct for testing -- no mods needed.
- **D-04:** All 5 stages must pass for Phase 9 PASS:
  1. Relay DLL loads into Skyrim via SKSE
  2. Relay TCP connection established (DLL writes relay_info.json)
  3. Native process starts and GameServer begins listening on port 10578
  4. fake_player authenticates and receives character assignment
  5. Movement broadcast: fake_player sends positions, receives host position updates
- **D-05:** fake_player verifies host player position via `/proc/pid/mem` -- the ultimate proof the full loop works.
- **D-06:** Scope includes two real Skyrim instances: one as host, one as join client.
- **D-07:** Both instances use the same DLL and native binary. Instance role configured per-instance. Claude has discretion on mechanism.
- **D-08:** The test script should automate launching both instances for quick iteration.
- **D-09:** Log collection to temp directory with cleanup on pass, keep with `--keep-logs`.
- **D-10:** Each stage timeout prints which log file to check.

### Claude's Discretion
- Total test timeout (~5min with buffer is reasonable)
- How to differentiate host vs join instance (env var, config file, or separate Proton prefixes)
- Exact /proc/pid/mem verification assertions in fake_player
- Whether to add a deploy step to the script or assume pre-deployed
- Log format and directory naming convention
- Whether two-Skyrim test is a separate script or a mode flag

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| RELAY-10 | End-to-end integration validated: DLL loads in Skyrim, native process connects, hook events flow | Directly addressed by test_ingame_sync.sh stages 1-4 |
| P2P-01 | Server embedded in host player's client process | Verified via fake_player connecting to embedded GameServer (stage 5) |
| P2P-02 | Host-authoritative world state | Verified via movement broadcast from host to fake_player |
| P2P-03 | Simplified actor ownership | Host owns all actors, verified via server-side CharacterService |
| P2P-04 | Cell management uses host's loaded cells | Validated by sStartingCell=Riverwood injection forcing known cell |
| D-05 | /proc/pid/mem verification | Requires implementation in fake_player RunExternal() |
| D-06 | Two real Skyrim instances | Requires new test script with separate Proton prefixes |
</phase_requirements>

## Architecture Patterns

### Current Test Pipeline (test_ingame_sync.sh as-is)

```
Stage 1: Prerequisites (binaries, Skyrim, SKSE, DLL deployed, INI exists)
Stage 2: Launch Skyrim via `proton run skse64_loader.exe` (120s timeout)
         pgrep -f SkyrimSE.exe to detect process
Stage 3: Wait for relay_info.json (90s timeout)
         Read TCP port + Wine PID from JSON
         MANUALLY launch skyrim-coop --pid $PID --port $PORT
Stage 4: Verify relay stable (10s, check native process alive)
Stage 5: [OPT-IN] Run fake_player --port 10578 --duration 15 --verify-pid $PID
```

### Corrected Understanding: DLL Does NOT Auto-Launch Native Process

Re-reading relay_main.cpp carefully:
- `SKSEPlugin_Load()` starts TCP server, installs hooks, writes relay_info.json, starts MonitorThread
- `MonitorThread()` calls `g_tcpServer.AcceptClient()` (blocking) -- waits for native process to connect
- `ProcessLauncher::Launch()` is only called on **auto-restart after disconnect** (relay_main.cpp line 254)
- There is NO initial `g_launcher.Launch()` call in `SKSEPlugin_Load()`

Therefore: **the script's manual launch of `skyrim-coop` is correct and required.** The DLL writes relay_info.json for the script to read, then waits for the native process to connect on its own.

### Required Script Changes

1. **Make fake_player default (D-02):** Invert the flag -- `--no-fake-player` to skip instead of `--with-fake-player` to enable.

2. **Add GameServer port readiness check:** Between native process launch and fake_player launch, wait for UDP port 10578 to be listening. Use `ss -ulnp | grep :10578` polling loop.

3. **Add log collection (D-09):** Create `/tmp/skyrimcoop-test-YYYYMMDD-HHMMSS/` directory. Redirect all process stdout/stderr to log files. On failure, print relevant log path per D-10.

4. **Wait for GameServer before fake_player:** The native process takes 1-2s to initialize GameServer after startup. Without waiting, fake_player may connect before the server is ready.

### Recommended Project Structure for Two-Instance Test

```
tests/
  test_ingame_sync.sh          # Primary: single-instance (host + fake_player)
  test_two_player.sh           # Extension: two real Skyrim instances
```

**Separate script recommended** because:
1. Two-instance test requires two Proton prefixes (additional setup)
2. Different failure modes (inter-instance vs single-instance)
3. Single-instance test is the prerequisite
4. Keeps scripts focused and debuggable

### Two-Instance Architecture (D-06, D-07)

```
Instance 1 (HOST):
  WINEPREFIX = /compatdata/489830/pfx (default Skyrim prefix)
  Skyrim.ini -> sStartingCell=Riverwood
  proton run skse64_loader.exe
  -> DLL loads, writes relay_info.json, MonitorThread waits
  -> Script reads relay_info.json, launches: skyrim-coop --pid $PID1 --port $RELAY_PORT1
  -> GameServer starts on UDP 10578

Instance 2 (JOIN):
  WINEPREFIX = /tmp/skyrimcoop_test_join_PREFIX/pfx (cloned from 489830)
  Skyrim.ini -> sStartingCell=Riverwood
  proton run skse64_loader.exe
  -> DLL loads, OVERWRITES relay_info.json (same path), MonitorThread waits
  -> Script reads new relay_info.json, launches: skyrim-coop --pid $PID2 --port $RELAY_PORT2 --connect 127.0.0.1:10578
```

**Separate Proton prefixes are REQUIRED:**
- Skyrim.ini and save files are in `$WINEPREFIX/pfx/drive_c/users/steamuser/Documents/`
- Two Skyrim processes writing to the same INI/saves causes corruption
- Wine uses per-prefix wineserver; two prefixes = two independent Wine servers
- The DLL and game binary are from the same Skyrim install dir (read-only, OK to share)

**Prefix cloning approach:**
```bash
COMPAT_DATA_JOIN="/tmp/skyrimcoop_test_join_$$"
cp -a "$COMPAT_DATA" "$COMPAT_DATA_JOIN"  # ~200MB, one-time per test run
```

### relay_info.json Collision Resolution

Both DLL instances write to `$SKYRIM_DIR/Data/SKSE/Plugins/skyrim_coop_relay.json` since the path is derived from the DLL location (same install dir).

**Solution: Sequential launch with capture.**
1. Launch host Skyrim
2. Wait for relay_info.json, read host's TCP port and PID
3. Launch host's skyrim-coop (host's native process connects, DLL MonitorThread accepts)
4. Wait for GameServer ready on 10578
5. THEN launch join Skyrim (DLL overwrites relay_info.json -- we already captured host's info)
6. Wait for new relay_info.json (different port/PID), read join's TCP port and PID
7. Launch join's skyrim-coop with `--connect 127.0.0.1:10578`

This is clean, requires no DLL code changes, and works because:
- The host's native process is already connected to the host's DLL before step 5
- The join's relay_info.json will have a different TCP port (the DLL uses ephemeral ports)

### Join Mode Native Client: Current State

main.cpp line 406-411:
```cpp
} else {
    spdlog::info("Starting in JOIN mode (connecting to {})", connectAddr);
    // TODO(Phase 8+): Implement enet6 client connection to remote host
    // For now, just log. The native process still operates as relay bridge.
}
```

**Join mode is NOT implemented.** The native process enters join mode but does not connect to the remote GameServer. Phase 9 must implement this for D-06 to pass. The implementation needs:
1. Instantiate a TiltedPhoques::Client (or MockClient-like class)
2. Connect to the remote host address (connectAddr)
3. Authenticate with the remote GameServer
4. Wire incoming ServerMessages to services (similar to ServerMessageRouter)

This is new code, not a script fix. It uses the same patterns as fake_player's RunExternal().

### Anti-Patterns to Avoid

- **Rewriting test_ingame_sync.sh from scratch:** The existing structure is sound. Fix incrementally.
- **Running two Skyrim instances from the same Proton prefix:** File lock conflicts and wineserver confusion.
- **Launching fake_player before GameServer is ready:** Race condition causes connection timeout.
- **Using Wine PID from relay_info.json for /proc/pid/mem:** Use pgrep's Linux PID instead.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing in bash | Custom awk/sed parser | `grep -o '"key":[0-9]*' \| grep -o '[0-9]*'` | Already used in existing scripts, no deps needed |
| Process monitoring | Custom PID file watcher | `pgrep -f` + `kill -0` + timeout loops | Standard Linux, already in script |
| Port readiness | Custom socket probe | `ss -ulnp \| grep :$PORT` | Standard iproute2 tool |
| enet6 client for testing | New binary | fake_player (existing) | Already does auth + movement |
| Proton prefix clone | Custom Wine setup | `cp -a $SRC $DST` | Preserves all permissions and symlinks |

## Common Pitfalls

### Pitfall 1: Wine PID vs Linux PID
**What goes wrong:** Native process gets wrong PID, /proc/pid/mem fails to open.
**Why it happens:** relay_info.json contains Wine's GetCurrentProcessId() value. The real Linux PID comes from pgrep.
**How to avoid:** Use `pgrep -f SkyrimSE.exe` for the real PID. The current script already does this correctly at line 222-225. The native process receives the correct PID via `--pid` from the script.
**Warning signs:** "Failed to open /proc/{}/mem" in native process logs.

### Pitfall 2: GameServer Port Not Ready When fake_player Connects
**What goes wrong:** fake_player tries to connect before GameServer has initialized.
**Why it happens:** Native process takes 1-2s to create ConsoleRegistry, GameServer, LoopbackTransport, and call Initialize(). Script currently doesn't wait for port readiness.
**How to avoid:** After confirming native process alive, poll `ss -ulnp | grep :10578` before launching fake_player.
**Warning signs:** fake_player logs "connection timed out" despite native process running.

### Pitfall 3: Two-Instance Proton Prefix Conflicts
**What goes wrong:** Second Skyrim instance crashes or corrupts saves/INI.
**Why it happens:** Both use same WINEPREFIX, same Documents dir, same wineserver.
**How to avoid:** Clone compatdata directory for join instance, set STEAM_COMPAT_DATA_PATH and WINEPREFIX to clone.
**Warning signs:** Skyrim hangs on loading screen, "wine: could not lock" errors.

### Pitfall 4: relay_info.json Overwritten by Second Instance
**What goes wrong:** Script reads wrong port/PID, native process connects to wrong DLL instance.
**Why it happens:** Both DLLs write to same file (same SKSE/Plugins dir).
**How to avoid:** Sequential launch: capture host's relay info BEFORE launching join instance. See architecture section for full flow.
**Warning signs:** Native process fails TCP connect ("Connection refused").

### Pitfall 5: fake_player --verify-pid Not Implemented
**What goes wrong:** fake_player accepts the flag but doesn't actually verify anything via /proc/pid/mem.
**Why it happens:** RunExternal() (line 266-358) never references cfg.verifyPid. Only ParseArgs handles the flag.
**How to avoid:** Phase 9 must implement the verification logic. At minimum: open /proc/pid/mem, read a known address, verify non-zero.
**Warning signs:** fake_player reports PASS without any "verification" log lines.

### Pitfall 6: TCP Receive Blocking in Native Process
**What goes wrong:** GameServer::Update() called infrequently because main loop blocks on TCP receive.
**Why it happens:** TcpClient::Receive is blocking. The DLL sends heartbeats every 5s, so worst-case 5s delay.
**How to avoid:** Known Phase 7 limitation. For testing, increase fake_player connection wait timeout beyond 5s. DLL heartbeats ensure the loop unblocks periodically.
**Warning signs:** fake_player auth response takes 5+ seconds.

### Pitfall 7: Join Mode Not Implemented in Native Client
**What goes wrong:** Join instance's skyrim-coop starts but never connects to host's GameServer.
**Why it happens:** main.cpp join branch is a TODO stub (line 409).
**How to avoid:** Phase 9 must implement basic enet6 client connect in the join branch.
**Warning signs:** Join instance logs "Starting in JOIN mode" but nothing else network-related.

## Code Examples

### GameServer Port Readiness Check (new, for test script)

```bash
# Source: standard ss-based port polling
wait_for_gameserver_port() {
    local port="${1:-10578}"
    local timeout="${2:-30}"
    info "Waiting for GameServer on UDP port $port (timeout: ${timeout}s)..."
    for i in $(seq 1 "$timeout"); do
        if ss -ulnp 2>/dev/null | grep -q ":${port} "; then
            pass "GameServer listening on port $port"
            return 0
        fi
        sleep 1
    done
    fail "GameServer not listening on port $port after ${timeout}s"
    return 1
}
```

### Log Collection Setup (D-09)

```bash
# Source: derived from existing script patterns
setup_test_logs() {
    TEST_RUN_DIR=$(mktemp -d "/tmp/skyrimcoop-test-XXXXXX")
    info "Test run logs: $TEST_RUN_DIR"
}

# Redirect process outputs
# proton run ... >"$TEST_RUN_DIR/skyrim_stdout.log" 2>"$TEST_RUN_DIR/skyrim_stderr.log" &
# skyrim-coop ... >"$TEST_RUN_DIR/native_stdout.log" 2>"$TEST_RUN_DIR/native_stderr.log" &

fail_with_logs() {
    local stage="$1"
    fail "$stage -- check logs in $TEST_RUN_DIR/"
    [ -f "$DLL_LOG" ] && info "DLL log (last 20 lines):" && tail -20 "$DLL_LOG"
    [ -f "$TEST_RUN_DIR/native_stderr.log" ] && info "Native stderr (last 20 lines):" && tail -20 "$TEST_RUN_DIR/native_stderr.log"
}
```

### fake_player /proc/pid/mem Verification (D-05)

```cpp
// Source: ProcMemReader API from Code/native_client/game_bridge/proc_memory.h
// Add to RunExternal() after movement loop
#ifdef __linux__
static bool VerifyHostMemory(pid_t aPid)
{
    ProcMemReader reader;
    if (!reader.Open(aPid)) {
        spdlog::error("verify: failed to open /proc/{}/mem", aPid);
        return false;
    }
    spdlog::info("verify: /proc/{}/mem opened successfully", aPid);

    // MVP verification: read from a known valid address range
    // The Skyrim executable's PE header starts at the base address.
    // Reading the first 4 bytes should give us 'MZ' (0x5A4D) --
    // the DOS header signature, proving memory access works.
    //
    // Getting the base address: read /proc/pid/maps and find SkyrimSE.exe
    // This is more robust than hardcoding an address.

    reader.Close();
    return true;
}
#endif
```

### Join Mode enet6 Client (required new code for D-06)

```cpp
// Conceptual pattern for native_client/main.cpp join branch
// Uses TiltedPhoques::Client (same base as MockClient)
// This is the minimum viable join implementation

class JoinClient : public TiltedPhoques::Client {
public:
    void OnConsume(const void* apData, uint32_t aSize) override {
        // Deserialize ServerMessages, dispatch to services via ServerMessageRouter
    }
    void OnConnected() override { m_connected = true; }
    void OnDisconnected(EDisconnectReason) override { m_connected = false; }
    void OnUpdate() override {}
    bool m_connected = false;
};

// In main.cpp join branch:
// JoinClient client;
// client.Connect(connectAddr);
// In main loop: client.Update(); + GameServer::Update() NOT called
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| fake_player opt-in (--with-fake-player) | fake_player always runs (D-02) | Phase 9 | Default test path includes GameServer verification |
| No GameServer port check | ss-based port readiness | Phase 9 | Eliminates race condition |
| Single Skyrim instance | Two-instance test (D-06) | Phase 9 | Proves real co-op flow |
| ServerMessageRouter missing move dispatch | Fixed: commit d9b039cb | Phase 8 fix | Remote player movement flows through loopback |
| Join mode TODO stub | enet6 client connect | Phase 9 | Real two-player testing possible |

## Open Questions

1. **fake_player /proc/pid/mem verification depth**
   - What we know: ProcMemReader works (tested in Phase 7). fake_player parses --verify-pid. RunExternal() ignores it.
   - What's unclear: Which specific memory addresses prove "host player actor exists and moved." The Address Library resolves addresses at DLL load time within the Skyrim process; fake_player runs as a separate process and cannot use those resolved addresses directly.
   - Recommendation: MVP approach: parse /proc/pid/maps to find SkyrimSE.exe base address, read PE header signature to prove memory access works. Stretch: read the PlayerCharacter singleton pointer chain to verify position. Flag as manual verification if deep memory validation proves too complex for Phase 9.

2. **Join mode implementation scope**
   - What we know: main.cpp join branch is a TODO. TiltedPhoques::Client exists. MockClient demonstrates the connect pattern.
   - What's unclear: Whether Phase 9 needs full bidirectional sync or just connection + auth verification.
   - Recommendation: Minimum for D-06: connect to host, authenticate, log "join player connected." Bidirectional movement sync is stretch but validates the full co-op flow.

3. **Two-instance Proton prefix disk cost**
   - What we know: Cloning compatdata/489830 creates a ~200MB copy.
   - What's unclear: Whether the clone can be made smaller (sparse copy, shared files).
   - Recommendation: Use full `cp -a` clone for simplicity. Clean up in test script cleanup() function. 200MB on /tmp is acceptable for testing.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Skyrim SE | Test target | Yes | SkyrimSE.exe (1.6.1170) | -- |
| Proton Experimental | Wine runtime | Yes | 2026-03-25 | -- |
| SKSE | Plugin loading | Yes | 2.2.6 | -- |
| XMake | Build system | Yes | v3.0.8 | -- |
| skyrim_coop_hooks.dll | Relay DLL (deployed) | Yes | 71KB, built 2026-03-28 | Rebuild via deploy_and_test.sh |
| skyrim-coop | Native client (deployed) | Yes | 703KB, built 2026-03-28 | Rebuild via deploy_and_test.sh |
| fake_player | Test binary (built) | Yes | 5MB, built 2026-03-28 | `xmake build fake_player` |
| ptrace_scope | /proc/pid/mem access | Yes | scope=1 (OK with SEIZE) | -- |
| ss (iproute2) | Port readiness check | Yes | Standard Linux | netstat fallback |
| MinGW GCC | DLL rebuild | Yes | GCC 10 | -- |
| Skyrim.ini | INI injection | Yes | In Proton prefix | -- |

**Missing dependencies with no fallback:** None

**Missing dependencies with fallback:** None

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Bash scripts + fake_player C++ binary + Catch2 unit tests |
| Config file | tests/test_ingame_sync.sh (primary), Code/tests/integration/xmake.lua (fake_player) |
| Quick run command | `./tests/test_ingame_sync.sh --check-only` |
| Full suite command | `./tests/test_ingame_sync.sh` (requires Skyrim launch, ~3-5min) |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| RELAY-10 | DLL loads, native connects, hooks flow | integration | `./tests/test_ingame_sync.sh` (stages 1-4) | Yes (needs fixes) |
| D-04-1 | Relay DLL loads into Skyrim via SKSE | integration | Stage 2 in test script | Yes |
| D-04-2 | Relay TCP established (relay_info.json) | integration | Stage 3 in test script | Yes |
| D-04-3 | GameServer listening on 10578 | integration | Stage 3 + new port check | Partial (needs port check) |
| D-04-4 | fake_player auth + assignment | integration | Stage 5 (default) | Yes (needs flag inversion) |
| D-04-5 | Movement broadcast verified | integration | Stage 5 + fake_player output | Yes (basic) |
| D-05 | /proc/pid/mem host verification | integration | fake_player --verify-pid | No (stub in RunExternal) |
| D-06 | Two real Skyrim instances | integration | test_two_player.sh | No (new script) |

### Sampling Rate
- **Per task commit:** `xmake run fake_player --self-test && xmake run TPTests`
- **Per wave merge:** `./tests/test_ingame_sync.sh` (requires Skyrim)
- **Phase gate:** All 5 stages PASS + two-instance human verification

### Wave 0 Gaps
- [ ] test_ingame_sync.sh: Make fake_player default path (D-02)
- [ ] test_ingame_sync.sh: Add GameServer port readiness check before fake_player
- [ ] test_ingame_sync.sh: Add log collection to temp directory (D-09, D-10)
- [ ] fake_player: Implement /proc/pid/mem verification in RunExternal() (D-05)
- [ ] native_client/main.cpp: Implement join mode enet6 client connect (D-06 dependency)
- [ ] tests/test_two_player.sh: New script for two-instance testing (D-06, D-07, D-08)

## Sources

### Primary (HIGH confidence)
- `tests/test_ingame_sync.sh` -- read in full, all 404 lines analyzed
- `Code/tests/integration/fake_player.cpp` -- read in full, 375 lines
- `Code/native_client/main.cpp` -- read in full, 531 lines, host/join/host-only modes analyzed
- `Code/relay_dll/relay_main.cpp` -- read in full, 380 lines, confirmed DLL does NOT auto-launch
- `Code/relay_dll/process_launcher.cpp` -- read in full, 95 lines, auto-restart only
- `Code/native_client/transport/ServerMessageRouter.cpp` -- read in full, Phase 8 gap fix confirmed present
- `Code/native_client/game_bridge/proc_memory.h` -- read in full, ProcMemReader API verified
- `Code/tests/mock_client.h` -- read in full, MockClient API for enet6 client pattern
- `deploy_and_test.sh` -- read in full, 241 lines, deploy pipeline analyzed
- `.planning/phases/08-p2p-embedded-gameserver/08-VERIFICATION.md` -- Phase 8 verification results
- `.planning/phases/08-p2p-embedded-gameserver/08-03-SUMMARY.md` -- Phase 8 completion confirmed
- `.planning/phases/09-in-game-p2p-integration-test/09-CONTEXT.md` -- all decisions

### Secondary (MEDIUM confidence)
- Environment probes: verified all dependencies present on this machine via command-line checks
- ptrace_scope=1 confirmed compatible with ProcMemReader's SEIZE approach
- Proton separate-prefix pattern: standard practice for multi-instance Wine/Proton

### Tertiary (LOW confidence)
- /proc/pid/mem verification depth: exact Skyrim memory addresses for TESObjectREFR position fields not determined. Address Library IDs are known (in hook_trampolines.cpp) but resolved at DLL load time within the Skyrim process -- fake_player cannot reuse them directly.
- Two-instance Proton prefix cloning: not yet tested on this specific system.

## Metadata

**Confidence breakdown:**
- Test script fixes: HIGH -- all source code read, issues clearly identified, fixes are mechanical
- Two-instance architecture: HIGH -- standard Proton separate-prefix pattern, sequential launch resolves relay_info.json collision
- /proc/pid/mem verification: MEDIUM -- ProcMemReader works, but specific assertion targets (memory addresses) are unclear
- Join mode implementation: MEDIUM -- MockClient/TiltedPhoques::Client patterns exist, but integration into main.cpp is new code

**Research date:** 2026-03-28
**Valid until:** 2026-04-28 (stable infrastructure work, not library-dependent)
