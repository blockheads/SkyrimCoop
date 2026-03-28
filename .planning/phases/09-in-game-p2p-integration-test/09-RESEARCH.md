# Phase 9: In-Game P2P Integration Test - Research

**Researched:** 2026-03-28
**Domain:** Integration testing, bash scripting, Proton/Wine process orchestration, enet6 networking
**Confidence:** HIGH

## Summary

Phase 9 is an **integration debugging** phase, not a greenfield build. All components exist: the test script (`test_ingame_sync.sh`), the fake_player binary, the relay DLL, the native client with embedded GameServer, and the deploy script. The work is making them work together end-to-end and extending coverage to include fake_player (now that Phase 8 provides the embedded GameServer) and two-Skyrim instance testing.

The existing test_ingame_sync.sh has a sound 5-stage structure but currently treats fake_player as opt-in (`--with-fake-player`). Per D-02, fake_player becomes the default test path. The script also needs a two-instance mode (D-06/D-07/D-08) where a second Skyrim joins the first as a client. The `/proc/pid/mem` verification in fake_player (D-05) needs implementation since fake_player currently only sends movements but does not read back from Skyrim's memory.

**Primary recommendation:** Fix the test script iteratively -- run each stage, fix what breaks, extend to include fake_player as default and two-instance mode. Do not rewrite the script from scratch.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Fix-and-validate approach. Run the existing `test_ingame_sync.sh` script, identify what breaks at each stage (DLL load, relay handshake, native spawn, GameServer start, fake_player connect), fix issues iteratively. The script structure is sound -- the bugs are in the components it orchestrates.
- **D-02:** The existing `--with-fake-player` flag becomes the default test path (not opt-in). The Phase 8 embedded GameServer makes this possible now.
- **D-03:** Script launches Skyrim directly via `proton run skse64_loader.exe` (bypassing MO2). This is correct for testing -- no mods needed.
- **D-04:** All 5 stages must pass for Phase 9 PASS: (1) Relay DLL loads, (2) TCP relay established, (3) GameServer listening, (4) fake_player auth + character assignment, (5) Movement broadcast verification.
- **D-05:** fake_player verifies host player position via `/proc/pid/mem` -- the ultimate proof the full loop works.
- **D-06:** Scope includes two real Skyrim instances: one as host, one as join client.
- **D-07:** Both instances use the same DLL and native binary. Instance role configured per-instance. Claude has discretion on mechanism.
- **D-08:** The test script should automate launching both instances for quick iteration.
- **D-09:** Log collection to temp directory with cleanup on pass.
- **D-10:** Each stage timeout prints which log file to check.

### Claude's Discretion
- Total test timeout (~5min with buffer is reasonable)
- How to differentiate host vs join instance (env var, config file, or separate Proton prefixes)
- Exact /proc/pid/mem verification assertions in fake_player
- Whether to add a deploy step to the script or assume pre-deployed
- Log format and directory naming convention
- Whether two-Skyrim test is a separate script or a mode flag

### Deferred Ideas (OUT OF SCOPE)
None
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| RELAY-10 | End-to-end integration validated: DLL loads in Skyrim, native process connects, hook events flow | Directly addressed by test_ingame_sync.sh stages 1-5 |
| P2P-01 | Server embedded in host player's client process | Verified via fake_player connecting to embedded GameServer |
| P2P-02 | Host-authoritative world state | Verified via movement broadcast from host to fake_player |
| P2P-03 | Simplified actor ownership | Host owns all actors, verified via server-side CharacterService |
| P2P-04 | Cell management uses host's loaded cells | Validated by sStartingCell=Riverwood injection forcing known cell |
</phase_requirements>

## Architecture Patterns

### Test Pipeline Structure (Existing)

The test follows a 5-stage pipeline with per-stage timeouts. Phase 9 extends this to include fake_player as default and adds two-instance support.

```
Stage 1: Prerequisites check (DLL deployed, SKSE installed, Skyrim exists)
Stage 2: Launch Skyrim via Proton + SKSE (120s timeout)
Stage 3: Wait for relay info + native process (90s timeout)
Stage 4: Verify relay hooks flowing (10s observation)
Stage 5: fake_player connects to GameServer (30s + duration)
Stage 6: [NEW] Two-instance test (second Skyrim joins)
```

### Recommended Script Structure

```
tests/
  test_ingame_sync.sh          # Main integration test (fixed + extended)
  test_two_player.sh           # Separate script for two-Skyrim test (D-06)
```

**Recommendation:** Two-Skyrim test should be a **separate script** because:
1. It requires two Proton prefixes (or env var differentiation)
2. Failure modes are different (single-instance failure vs inter-instance failure)
3. Single-instance test is the prerequisite for two-instance test
4. Keeps test_ingame_sync.sh focused and debuggable

### Process Tree

```
test_ingame_sync.sh
  |-- proton run skse64_loader.exe (Host)
  |     |-- SkyrimSE.exe (Wine)
  |     |-- skyrim_coop_hooks.dll (loaded by SKSE)
  |     |     |-- TCP server on dynamic port
  |     |     |-- writes skyrim_coop_relay.json
  |     |-- skyrim-coop --pid <PID> --port <PORT> (spawned by DLL)
  |           |-- GameReader (reads /proc/PID/mem)
  |           |-- TcpClient (connects to DLL TCP server)
  |           |-- GameServer (enet6, UDP 10578)
  |           |-- LoopbackTransport (host self-auth)
  |-- fake_player --port 10578 --verify-pid <PID> --duration 15
```

### Two-Instance Process Tree

```
test_two_player.sh
  |-- proton run skse64_loader.exe (Host - prefix A)
  |     |-- SkyrimSE.exe (Wine)
  |     |-- skyrim-coop [host mode, GameServer on 10578]
  |-- proton run skse64_loader.exe (Client - prefix B or env var)
  |     |-- SkyrimSE.exe (Wine)
  |     |-- skyrim-coop --connect 127.0.0.1:10578 [join mode]
```

### Two-Instance Differentiation (Recommendation)

**Use separate Proton prefixes.** This is the cleanest approach because:
1. Each Skyrim instance gets its own Skyrim.ini (sStartingCell can differ)
2. Each instance gets its own registry, documents folder, save files
3. No conflicts between DLL log files or relay_info.json paths
4. Each instance's skyrim-coop process is independently identifiable

The second prefix needs to be created by copying the existing compatdata/489830 to a new location (e.g., compatdata/489830_client). Both point to the same Skyrim install but have separate Wine prefixes.

**Alternative (simpler but fragile):** Use `SKYRIM_COOP_ROLE=join` environment variable passed through Proton. The DLL reads it and passes `--connect` to the native process. This avoids prefix duplication but requires DLL modification.

**Recommendation for Phase 9:** Start with the separate Proton prefix approach. It is purely script-level (no code changes to DLL).

### Anti-Patterns to Avoid
- **Rewriting test_ingame_sync.sh from scratch:** The existing structure is sound. Fix what breaks, extend incrementally.
- **Running two Skyrim instances from the same Proton prefix:** They share Skyrim.ini, save files, DLL logs, and relay_info.json -- race conditions guaranteed.
- **Polling with aggressive sleep loops:** The existing `seq 1 $TIMEOUT` pattern with 1s sleeps is reasonable for process startup. Do not use tighter loops.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| JSON parsing in bash | Custom awk/sed JSON parser | grep -o pattern matching (existing) | relay_info.json is simple key:value, no nested objects |
| Process monitoring | Custom PID file watchers | pgrep -f with timeout loops (existing) | Standard Linux pattern, already implemented |
| Log aggregation | Custom log collector | Redirect stdout/stderr to files in temp dir | Shell native, D-09 requires this |
| enet6 client for testing | New test client binary | fake_player (existing) | Already does auth + movement + /proc/pid/mem verification stub |

## Common Pitfalls

### Pitfall 1: Wine PID vs Linux PID
**What goes wrong:** The relay DLL reports its PID via GetCurrentProcessId() which returns a Wine-internal PID. The native process needs the real Linux PID for /proc/pid/mem access.
**Why it happens:** Wine translates Windows API calls but the PID numbering is internal.
**How to avoid:** The existing deploy_and_test.sh already handles this: use `pgrep -f "SkyrimSE.exe"` to get the real Linux PID, ignore the PID from relay_info.json for /proc/pid/mem purposes. The DLL already uses `getpid()` (resolves to real Linux PID under Wine) when spawning the native process. test_ingame_sync.sh must also use the pgrep-discovered PID, not the relay_info.json PID.
**Warning signs:** fake_player --verify-pid fails with "permission denied" or "no such process"

### Pitfall 2: DLL Auto-Spawns Native Process
**What goes wrong:** The script tries to launch `skyrim-coop` manually, but the relay DLL already spawns it via ProcessLauncher::Launch(). Two native processes compete for the same TCP port and enet6 port.
**Why it happens:** The test script (written before the DLL auto-spawn was implemented) has a manual `"$PLUGINS/skyrim-coop" --pid ... --port ...` launch step.
**How to avoid:** **Remove the manual native process launch from test_ingame_sync.sh.** The DLL spawns it automatically. Wait for the native process to appear (look for GameServer port 10578 becoming active via `ss -ln | grep 10578`).
**Warning signs:** "Address already in use" errors on port 10578, two skyrim-coop processes visible in ps output.

### Pitfall 3: Proton Environment Variables
**What goes wrong:** Launching Skyrim via `proton run` without proper Steam environment variables causes Proton to fail to find the runtime or prefix.
**Why it happens:** Proton expects SteamAppId, STEAM_COMPAT_DATA_PATH, STEAM_COMPAT_CLIENT_INSTALL_PATH, WINEPREFIX.
**How to avoid:** The existing script sets all required variables. Keep them.
**Warning signs:** Proton exits immediately, Wine prefix not found errors.

### Pitfall 4: Skyrim INI Backup Left Behind
**What goes wrong:** Script crashes before cleanup trap runs, leaving sStartingCell=Riverwood permanently in Skyrim.ini.
**Why it happens:** `set -e` causes exit before trap if certain commands fail in unexpected ways.
**How to avoid:** The existing cleanup trap is good. Ensure it runs on all exit paths (SIGINT, SIGTERM, ERR). Add `trap cleanup EXIT INT TERM` (existing uses just EXIT, which should cover most cases).
**Warning signs:** Skyrim always boots to Riverwood even outside testing.

### Pitfall 5: GameServer Port Not Ready When fake_player Connects
**What goes wrong:** fake_player tries to connect before the embedded GameServer has finished initializing.
**Why it happens:** The native process starts, initializes services, then starts GameServer. There is a startup delay.
**How to avoid:** Wait for port 10578 to be listening before launching fake_player: `ss -ln | grep :10578`. The script currently waits for the native process to exist, not for the port to be ready.
**Warning signs:** fake_player exits with "connection timed out" despite native process running.

### Pitfall 6: Two-Instance Relay Info Collision
**What goes wrong:** Both Skyrim instances write to the same `skyrim_coop_relay.json` path, second instance overwrites first.
**Why it happens:** Both instances use the same SKSE/Plugins directory since they share the Skyrim installation.
**How to avoid:** For two-instance testing, the second instance needs a different relay_info.json path. Options: (a) use a per-instance env var the DLL checks, (b) use a different SKSE plugins path via symlink, or (c) accept that the DLL will need a small code change to support this. The DLL currently hardcodes the path relative to its own location.
**Warning signs:** Native process connects to wrong DLL instance.

### Pitfall 7: TCP Receive Blocking in Native Process
**What goes wrong:** The native process main loop uses blocking TCP receive. If the DLL stops sending events (e.g., no game activity), the native process blocks and GameServer::Update() stops being called.
**Why it happens:** TcpClient::Receive is blocking (noted in main.cpp comments: "non-blocking via poll() deferred").
**How to avoid:** This is a known limitation from Phase 7. The DLL sends heartbeats every 5s, so the native process unblocks at worst every 5s. For testing purposes this is acceptable -- fake_player will see delayed but functional responses. If this causes test timeouts, increase fake_player's connection wait time.
**Warning signs:** fake_player connects but receives no auth response for 5+ seconds.

## Code Examples

### Current Script Issues to Fix

**Issue 1: Manual native process launch (line 265)**
```bash
# CURRENT (WRONG) -- DLL auto-spawns this
"$PLUGINS/skyrim-coop" --pid "$SKYRIM_PID" --port "$RELAY_PORT" &
NATIVE_PID=$!

# FIX: Wait for DLL to spawn native process automatically
info "Waiting for native skyrim-coop process (DLL auto-spawn)..."
NATIVE_PID=""
for i in $(seq 1 30); do
    NATIVE_PID=$(pgrep -f "skyrim-coop.*--pid" 2>/dev/null | head -1 || true)
    if [ -n "$NATIVE_PID" ]; then
        pass "skyrim-coop running (PID: $NATIVE_PID, auto-spawned by DLL)"
        break
    fi
    sleep 1
done
```

**Issue 2: fake_player is opt-in (line 339)**
```bash
# CURRENT -- opt-in
local with_fake_player=false
# ...
--with-fake-player) with_fake_player=true ;;

# FIX (per D-02): fake_player is default, add --no-fake-player to skip
local skip_fake_player=false
# ...
--no-fake-player) skip_fake_player=true ;;
```

**Issue 3: No GameServer port readiness check**
```bash
# ADD: Wait for GameServer port before fake_player
wait_for_gameserver_port() {
    info "Waiting for GameServer on port ${GAME_SERVER_PORT} (timeout: 30s)..."
    for i in $(seq 1 30); do
        if ss -ln 2>/dev/null | grep -q ":${GAME_SERVER_PORT} "; then
            pass "GameServer listening on port $GAME_SERVER_PORT"
            return 0
        fi
        sleep 1
    done
    fail "GameServer not listening on port $GAME_SERVER_PORT after 30s"
    return 1
}
```

### fake_player /proc/pid/mem Verification Pattern (D-05)

The existing fake_player has a `--verify-pid` flag and includes `proc_memory.h`. The verification code needs to be added to `RunExternal()`:

```cpp
// After movement loop completes, if --verify-pid was given:
if (aCfg.verifyPid > 0) {
    ProcMemReader reader;
    if (!reader.Open(aCfg.verifyPid)) {
        spdlog::error("Failed to open /proc/{}/mem for verification", aCfg.verifyPid);
        return 1;
    }

    // Read the player's TESObjectREFR from known pointer chain
    // The exact offsets depend on Skyrim SE version and Address Library
    // Minimum viable: verify we can read the process memory at all
    // Advanced: verify the host player's position is non-zero
    uint64_t testAddr = 0x0;  // placeholder -- needs real base address
    auto val = reader.ReadValue<uint32_t>(testAddr);
    spdlog::info("Verification read: addr={:#x} value={:#x}", testAddr, val);
    reader.Close();
}
```

### Log Collection Pattern (D-09)

```bash
# Create test run directory
TEST_RUN_DIR=$(mktemp -d "/tmp/skyrimcoop-test-XXXXXX")
info "Test run logs: $TEST_RUN_DIR"

# Redirect process outputs
"$PROTON_DIR/proton" run "$SKYRIM_DIR/skse64_loader.exe" \
    >"$TEST_RUN_DIR/proton.stdout" 2>"$TEST_RUN_DIR/proton.stderr" &

# On failure, print relevant log
fail_with_logs() {
    local stage="$1"
    fail "$stage"
    info "=== Logs for stage: $stage ==="
    info "Check: $TEST_RUN_DIR/"
    ls -la "$TEST_RUN_DIR/"
    [ -f "$DLL_LOG" ] && info "DLL log:" && tail -20 "$DLL_LOG"
    [ -f "$TEST_RUN_DIR/proton.stderr" ] && info "Proton stderr:" && tail -20 "$TEST_RUN_DIR/proton.stderr"
}

# On success, cleanup (unless --keep-logs)
cleanup_logs() {
    if [ "${KEEP_LOGS:-false}" = "false" ]; then
        rm -rf "$TEST_RUN_DIR"
        info "Cleaned up test logs"
    else
        info "Logs kept at: $TEST_RUN_DIR"
    fi
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Manual native process launch in script | DLL auto-spawns native process | Phase 7 (relay DLL) | test_ingame_sync.sh must remove manual launch |
| fake_player opt-in | fake_player default (Phase 8 GameServer) | Phase 8 complete | Script flag inversion |
| No embedded GameServer | GameServer embedded in native process | Phase 8 | fake_player can now connect to real server |
| Dedicated server mode | P2P host-only mode | Phase 5 cleanup | Only embedded server remains |

## Open Questions

1. **DLL auto-spawn vs manual launch in test script**
   - What we know: The DLL's ProcessLauncher::Launch() spawns the native binary automatically with `--pid` and `--port` args. The test script currently launches it manually too.
   - What's unclear: Does the DLL reliably auto-spawn in every case, or are there timing issues where the DLL hasn't finished initializing before the script checks for the native process?
   - Recommendation: Remove manual launch, wait for DLL-spawned process. If DLL auto-spawn is unreliable, the script should detect "no native process after 30s" and fall back to manual launch.

2. **Two-Instance relay_info.json Collision**
   - What we know: Both instances write to the same `$PLUGINS/skyrim_coop_relay.json` path.
   - What's unclear: Whether the DLL can be configured to use a different path without code changes.
   - Recommendation: For the two-instance test, either modify the DLL to respect an env var for the JSON path, or accept a small relay_dll code change. Alternatively, use a symlink or bind mount to redirect the path per-instance.

3. **/proc/pid/mem Verification Depth**
   - What we know: ProcMemReader works (tested in Phase 7). fake_player has the --verify-pid stub.
   - What's unclear: Which specific memory addresses to read to verify "host player actor exists and moved." This requires knowledge of Skyrim's in-memory TESObjectREFR layout and the Address Library's resolved offsets.
   - Recommendation: Minimum viable verification is: (1) open /proc/pid/mem successfully, (2) read the player's base address from a known pointer chain, (3) verify it is non-zero. Advanced verification (reading position floats and comparing to sent values) is a stretch goal.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Skyrim SE | Test target | Yes | SkyrimSE.exe present | -- |
| Proton Experimental | Wine runtime | Yes | Experimental (2026-03-25) | -- |
| SKSE | Plugin loading | Yes | skse64_loader.exe present | -- |
| xmake | Build system | Yes | v3.0.8 | -- |
| skyrim_coop_hooks.dll | Relay DLL | Yes (deployed) | 71KB, 2026-03-28 | Rebuild + deploy |
| skyrim-coop | Native client | Yes (deployed) | 703KB, 2026-03-28 | Rebuild + deploy |
| fake_player | Test binary | Yes (built) | In build/linux/x86_64 | Rebuild |
| ptrace_scope | /proc/pid/mem | ptrace_scope=1 | OK with ptrace SEIZE | -- |
| ss (iproute2) | Port checking | Yes (standard) | -- | netstat fallback |
| Skyrim.ini | INI injection | Yes | Proton prefix exists | -- |

**Missing dependencies with no fallback:** None -- all prerequisites are met.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Bash scripts + fake_player C++ binary + Catch2 (unit tests) |
| Config file | None (script-driven) |
| Quick run command | `./tests/test_ingame_sync.sh --check-only` |
| Full suite command | `./tests/test_ingame_sync.sh` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| RELAY-10 | DLL loads, native connects, hooks flow | integration | `./tests/test_ingame_sync.sh` | Yes (needs fixes) |
| P2P-01 | Embedded server in host process | integration | `xmake run fake_player --self-test` | Yes |
| P2P-02 | Host-authoritative world state | integration | fake_player movement verification | Partial (needs /proc verify) |
| P2P-03 | Simplified actor ownership | integration | Verified by server-side CharacterService | Unit tests exist |
| P2P-04 | Cell management | integration | sStartingCell injection + relay hooks | Yes (script handles) |

### Sampling Rate
- **Per task commit:** `xmake run fake_player --self-test && xmake run TPTests`
- **Per wave merge:** `./tests/test_ingame_sync.sh` (requires Skyrim)
- **Phase gate:** All 5 stages pass + two-instance test completes

### Wave 0 Gaps
- [ ] test_ingame_sync.sh needs fixes (manual launch removal, fake_player default, port readiness check)
- [ ] fake_player needs /proc/pid/mem verification implementation
- [ ] Two-instance test script does not exist yet
- [ ] Log collection infrastructure (D-09) not yet in script

## Sources

### Primary (HIGH confidence)
- `tests/test_ingame_sync.sh` -- read in full, analyzed all 5 stages
- `Code/tests/integration/fake_player.cpp` -- read in full, analyzed self-test and external modes
- `Code/native_client/main.cpp` -- read in full, analyzed host/join mode and GameServer lifecycle
- `Code/relay_dll/process_launcher.cpp` -- read in full, confirmed DLL auto-spawns native process
- `Code/relay_dll/protocol.h` -- read in full, confirmed TCP protocol structure
- `Code/native_client/game_bridge/proc_memory.h` -- read in full, confirmed ProcMemReader API
- `deploy_and_test.sh` -- read in full, analyzed deploy pipeline
- `.planning/phases/08-p2p-embedded-gameserver/08-03-SUMMARY.md` -- confirmed Phase 8 complete

### Secondary (MEDIUM confidence)
- Environment probes: verified Skyrim, Proton, SKSE, DLL, native binary, fake_player all present on this machine
- ptrace_scope=1 confirmed compatible with ProcMemReader's SEIZE approach

### Tertiary (LOW confidence)
- /proc/pid/mem verification depth: exact Skyrim memory offsets for TESObjectREFR position fields not verified. The offsets are defined in the Address Library but the exact values depend on Skyrim SE version installed.
- Two-instance Proton prefix approach: not tested yet, based on general Proton knowledge that each prefix is an independent Wine environment.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - all tools are already built and deployed, verified on this machine
- Architecture: HIGH - existing scripts analyzed in detail, pipeline structure is clear
- Pitfalls: HIGH - all pitfalls identified from actual code analysis (e.g., DLL auto-spawn vs manual launch conflict is a real bug in the current script)
- /proc/pid/mem verification: MEDIUM - ProcMemReader works but exact assertion targets unclear

**Research date:** 2026-03-28
**Valid until:** 2026-04-28 (stable -- no external dependency changes expected)
