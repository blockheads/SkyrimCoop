# Phase 5: Networking Verification & Server Cleanup - Research

**Researched:** 2026-03-28
**Domain:** Server code removal, networking test infrastructure, in-game verification
**Confidence:** HIGH

## Summary

Phase 5 has three distinct workstreams: (1) removing standalone dedicated server code, (2) building layered networking tests from unit through integration, and (3) a stretch goal of in-game verification via a fake second player with /proc/pid/mem validation. The codebase is well-structured for all three -- the server_runner is cleanly separated from the embedded server path, the existing Catch2 test infrastructure has 23 test cases to extend, and the Phase 7 relay architecture already provides ProcMemReader and GameReader APIs for memory verification.

The key architectural insight is that `TiltedPhoques::Server` (enet6-based) provides `Host()`, `Update()`, `OnConnection()`, `OnConsume()`, and `Send()` methods that can be driven entirely without Skyrim. A test harness can instantiate `Server::GameServer` directly (like HostService already does), create a `TiltedPhoques::Client` that connects to localhost, and exercise the full authentication + message dispatch pipeline headlessly on Linux.

**Primary recommendation:** Build a separate `TPNetworkTests` binary that links both SkyrimEncoding and SkyrimTogetherServer, instantiates the GameServer in-process, and connects a mock client via enet6 loopback. Test the relay protocol, message round-trips, and server handshake in that order.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: Remove all standalone/dedicated server logic. Only embedded server path (P2P host mode) remains. Remove server_runner, server list, authentication via server password, and connect-to-remote-server flow.
- D-02: Mock client / test harness tests both TCP relay protocol (DLL-to-native) AND game server networking (native-to-embedded server).
- D-03: Automated in-game verification (stretch goal) -- launch real Skyrim, connect fake second player, verify remote actor position via /proc/pid/mem.
- D-04: Test layering bottom-up: (1) message round-trip tests, (2) mock client handshake, (3) relay integration, (4) in-game integration.
- D-05: All server-side tests target embedded server only. No standalone server testing.

### Claude's Discretion
- Test framework choices beyond existing Catch2 (for integration tests, may need a different harness)
- Whether the fake player test harness is a separate binary or a mode of skyrim-coop
- How to structure the /proc/pid/mem verification (inline in test vs separate validation pass)
- Priority ordering of plans within the phase

### Deferred Ideas (OUT OF SCOPE)
- CI pipeline (GitHub Actions) -- explicitly deferred
- Fuzz testing (TEST-05) -- deferred to a future phase
- Two real Skyrim instances -- not feasible for automated testing
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TEST-01 | Existing Catch2 unit tests compile and pass natively on Linux (no Wine required) | Tests already build natively via `xmake build TPTests` on linux platform. 23 test cases, 905 assertions exist. Server removal must not break encoding/relay tests. |
| TEST-02 | Headless mock client connects to GameServer on Linux without Skyrim or Wine | TiltedPhoques::Client + TiltedPhoques::Server use enet6 UDP -- can loopback on localhost. GameServer instantiation pattern exists in HostService.cpp. ConsoleRegistry + GameServer constructor are the only setup needed. |
| TEST-03 | Message round-trip tests verify serialization/deserialization across mock client and server | Existing encoding.cpp tests cover serialize/deserialize in isolation. New tests must verify the full path: client serialize -> network -> server OnConsume -> factory Extract -> handler dispatch. |
</phase_requirements>

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Catch2 | 2.13.9 | Unit + integration test framework | Already used for 23 existing test cases; header-only, simple setup |
| enet6 | (xmake managed) | UDP networking for mock client/server | Already the transport layer -- test harness uses same stack as production |
| SkyrimEncoding | N/A (internal) | Message serialization | All 58 client + 61 server message types available natively |
| SkyrimTogetherServer | N/A (internal) | Embedded GameServer for integration tests | Links as static library, instantiable without Skyrim |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| spdlog | v1.13.0 | Test logging and server logging | Required by GameServer, already in test deps |
| glm | 0.9.9+8 | Vector math for position verification | Used by GameReader::ReadActorPosition |
| hopscotch-map | v2.3.1 | Fast hash maps in server components | Transitive dependency of server |
| rpmalloc | (xmake managed) | Allocator | Required in test main.cpp init |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Catch2 for integration | GoogleTest | GTest already in deps but Catch2 is established pattern; switching adds no value |
| Separate test binary | Mode flag in skyrim-coop | Separate binary cleaner -- no production code changes needed |
| In-process server test | Subprocess server | In-process avoids IPC complexity, matches HostService pattern |

## Architecture Patterns

### Recommended Project Structure
```
Code/
├── tests/
│   ├── main.cpp                    # Existing Catch2 runner (rpmalloc init)
│   ├── encoding.cpp                # Existing encoding round-trip tests
│   ├── relay_protocol_tests.cpp    # Existing relay protocol tests
│   ├── command_queue_tests.cpp     # Existing SPSC queue tests
│   ├── proc_memory_tests.cpp       # Existing /proc/pid/mem tests
│   ├── pointer_table_tests.cpp     # Existing pointer table tests
│   ├── server_roundtrip_tests.cpp  # NEW: message round-trip through actual server
│   ├── mock_client_tests.cpp       # NEW: headless client handshake + sync
│   └── xmake.lua                   # Updated to add server deps for new tests
├── tests/integration/
│   ├── fake_player.cpp             # NEW: standalone binary for in-game testing
│   └── xmake.lua                   # NEW: separate target linking full stack
tests/
├── smoke_tcp_relay.sh              # Existing smoke test
├── smoke_ptrace.sh                 # Existing ptrace smoke test
└── test_ingame_sync.sh             # NEW: stretch goal integration test script
```

### Pattern 1: In-Process Server + Client Testing
**What:** Instantiate GameServer and TiltedPhoques::Client in the same test process, connected via localhost loopback.
**When to use:** For TEST-02 (headless mock client) and TEST-03 (message round-trip).
**Example:**
```cpp
// Pattern from HostService.cpp -- instantiate server without Skyrim
#include <catch2/catch.hpp>
#include <GameServer.h>
#include <Client.hpp>
#include <console/ConsoleRegistry.h>

// Server runs in-process, client connects via enet6 loopback
TEST_CASE("MockClient_AuthHandshake", "[NetworkIntegration]") {
    ServerConsole::ConsoleRegistry console("TestServer");
    console.BindStaticItems();

    // GameServer constructor calls Host() on a port
    Server::GameServer server(console);
    server.Initialize();

    // Create mock client that connects to localhost
    MockClient client;  // extends TiltedPhoques::Client
    REQUIRE(client.Connect("127.0.0.1:" + std::to_string(server.GetPort())));

    // Pump both sides
    for (int i = 0; i < 100; i++) {
        server.Update();
        client.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(client.IsConnected());
    // Verify auth handshake completed
    REQUIRE(client.GetAuthState() == MockClient::kAuthenticated);
}
```

### Pattern 2: Message Factory Round-Trip Through Wire
**What:** Serialize a message, send over enet6, receive on server side, verify dispatch.
**When to use:** TEST-03 verification that serialization works end-to-end through the network stack.
**Example:**
```cpp
// Build on existing encoding.cpp pattern but send through actual network
TEST_CASE("RoundTrip_ClientReferencesMoveRequest", "[NetworkRoundTrip]") {
    // Setup server + client (helper)
    auto [server, client] = CreateTestPair();

    // Build message
    ClientReferencesMoveRequest moveReq;
    auto& update = moveReq.Updates[1];
    update.UpdatedMovement.Position.x = 100.0f;
    update.UpdatedMovement.Position.y = 200.0f;
    update.UpdatedMovement.Position.z = 300.0f;

    // Send through network
    client.SendGameMessage(moveReq);

    // Pump
    PumpUntilReceived(server, client);

    // Verify server received and dispatched correctly
    REQUIRE(server.GetLastReceivedOpcode() == ClientReferencesMoveRequest::Opcode);
}
```

### Pattern 3: Fake Player Binary for In-Game Testing (Stretch)
**What:** Standalone binary that connects to embedded GameServer as a second player, sends scripted movements, then reads game memory to verify.
**When to use:** D-03 stretch goal, validating full end-to-end sync.
**Example architecture:**
```
Skyrim (via Proton) + relay DLL + native process (host)
    |
    | enet6 UDP (localhost:10578)
    |
FakePlayer binary (test harness)
    |
    | /proc/pid/mem reads
    |
    v
Verify remote actor position in Skyrim's memory matches sent coordinates
```

### Anti-Patterns to Avoid
- **Testing serialization only in isolation:** The existing encoding.cpp tests verify serialize/deserialize but never go through the actual network. Both are needed.
- **Sleeping for fixed durations in tests:** Use polling loops with timeouts (pump both server + client Update() calls) instead of fixed sleep durations.
- **Depending on server_runner for tests:** All tests must use embedded GameServer directly, not the DediRunner/server_runner path (which is being removed).
- **Modifying production code for test hooks:** Use the public API (GameServer constructor, TiltedPhoques::Client) rather than adding test-only code paths to production.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| UDP loopback testing | Custom socket layer | TiltedPhoques::Client + Server over localhost | Already handles enet6 setup, compression, reliability |
| Message serialization | Manual byte packing | Buffer::Writer + Message::Serialize() | Existing infrastructure, tested, handles all opcodes |
| Process memory reads | Raw pread() calls | ProcMemReader + GameReader | Already handles /proc/pid/mem open/close, typed reads, pointer chains |
| Server lifecycle | Manual enet init/cleanup | GameServer constructor/destructor | ConsoleRegistry + GameServer is the supported setup path |
| Client message factory | Manual opcode switch | ClientMessageFactory::Extract() | Already maps all 58 opcodes to correct types |

## Standalone Server Removal Inventory

### Files to Remove Entirely
| File/Directory | Purpose | Safe to Delete |
|----------------|---------|----------------|
| `Code/server_runner/` (entire directory) | DediRunner standalone executable | YES -- 6 files: DediRunner.h, DediRunner.cpp, main.cpp, xmake.lua, resources/, server_runner.rc |
| `Code/common/GameServerInstance.h` | IGameServerInstance interface for DediRunner | YES -- only used by DediRunner and server/main.cpp |

### Files to Modify
| File | What to Change | Risk |
|------|----------------|------|
| `Code/xmake.lua` | Remove `includes("server_runner")` line | LOW -- one line removal |
| `Code/server/main.cpp` | Remove or gut -- provides `CreateGameServer()`, `SetDefaultLogger()`, `RegisterLogger()` exports used only by DediRunner | MEDIUM -- HostService uses GameServer directly, not through these exports. Check if any other consumer uses these exports. |
| `Code/server/GameServer.cpp` | Remove `sPassword`, `sAdminPassword` settings and password-checking in `HandleAuthenticationRequest()`. Remove `sServerName` and server-list-oriented `UpdateInfo()`. | MEDIUM -- must keep auth flow working for player join, just remove password gate |
| `Code/server/GameServer.h` | Remove `IsPasswordProtected()`, `m_isPasswordProtected` | LOW |
| `Code/server/xmake.lua` | Keep as-is -- builds server as library, not standalone | NONE |

### Settings to Remove from GameServer.cpp
These are dedicated-server settings that have no meaning in P2P embedded host mode:
- `sServerName` -- "Name that shows up in the server list"
- `sAdminPassword` -- dedicated admin console password
- `sPassword` -- server password for connecting
- `uMaxPlayerCount` -- could keep for P2P (limit to 4), but the setting-based config is DediRunner specific

### Settings to Keep
These are gameplay settings that apply equally in embedded host mode:
- `uServerPort` -- host still needs a port
- `bPremiumTickrate` -- tick rate is relevant
- `uDifficulty`, `bEnableGreetings`, `bEnablePvp`, `bSyncPlayerHomes`, `bEnableDeathSystem`, `uTimeScale`, `bSyncPlayerCalendar`, `bAutoPartyJoin` -- all gameplay
- `bEnableModCheck`, `bAllowSKSE`, `bAllowMO2` -- mod policy

### References That Need Cleanup
- `Code/skyrim_ui/` server-list component (`server-list.component.html`) -- UI removal is Phase 6, but note this exists
- `P2P_CONVERSION_PLAN.md` -- documentation reference, not code

## Common Pitfalls

### Pitfall 1: enet6 Initialization
**What goes wrong:** enet6 requires global initialization (`enet_initialize()`) before creating hosts. If tests call `enet_initialize()` multiple times without matching `enet_deinitialize()`, memory leaks or crashes occur.
**Why it happens:** TiltedPhoques::Server and TiltedPhoques::Client likely handle this internally, but instantiating multiple server/client pairs in separate test cases may expose init/deinit ordering issues.
**How to avoid:** Use Catch2 test fixtures or global setup/teardown. Verify that TiltedPhoques handles enet init reference counting. If not, add enet_initialize() in test main.cpp alongside rpmalloc_initialize().
**Warning signs:** Segfault or "enet_host_create failed" on second test case.

### Pitfall 2: Port Conflicts Between Test Cases
**What goes wrong:** GameServer constructor calls `Host(port, tickRate)` and will auto-increment port if busy. But if two test cases run quickly, the first server may not have released its port.
**Why it happens:** UDP port release is not instant -- TIME_WAIT state.
**How to avoid:** Each test case should use a unique port range (e.g., 20000 + testIndex). Or use a single server instance for related tests via Catch2 fixtures.
**Warning signs:** "Port X is already in use, trying X+1" log spam.

### Pitfall 3: GameServer Requires ConsoleRegistry with BindStaticItems
**What goes wrong:** GameServer constructor reads ServerConsole::Setting values. If settings are not bound, it reads uninitialized values.
**Why it happens:** The static `ServerConsole::Setting` objects register themselves but BindStaticItems() must be called to activate them.
**How to avoid:** Always call `console.BindStaticItems()` before constructing GameServer. See `CreateGameServer()` in server/main.cpp for the pattern.
**Warning signs:** Random port numbers, assertion failures in settings.

### Pitfall 4: Server Removal Breaking Existing Tests
**What goes wrong:** Removing server_runner files or modifying server/main.cpp could break the build of TPTests if there are transitive dependencies.
**Why it happens:** TPTests links SkyrimEncoding but does NOT link SkyrimTogetherServer currently. Removing server_runner should not affect TPTests, but verify.
**How to avoid:** Build and run existing tests after each removal step.
**Warning signs:** Link errors mentioning GameServer symbols.

### Pitfall 5: MockClient Auth Token Must Match
**What goes wrong:** Client sends AuthenticationRequest with Token field. Server checks `acRequest->Token == sPassword.value()`. If password is empty string (default), token must also be empty.
**Why it happens:** Even after removing password protection, the auth flow remains -- it just accepts empty tokens.
**How to avoid:** After server cleanup, verify the auth path accepts connections without password. Set mock client token to empty string.
**Warning signs:** Authentication rejected with kWrongPassword.

## Code Examples

### Existing Test Pattern (encoding.cpp)
```cpp
// Source: Code/tests/encoding.cpp
TEST_CASE("Encoding factory", "[encoding.factory]")
{
    Buffer buff(1000);
    AuthenticationRequest request;
    request.Token = "TesSt";

    Buffer::Writer writer(&buff);
    request.Serialize(writer);

    Buffer::Reader reader(&buff);
    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);

    REQUIRE(pMessage);
    REQUIRE(pMessage->GetOpcode() == request.GetOpcode());
}
```

### GameServer Instantiation Pattern (HostService.cpp)
```cpp
// Source: Code/client/Services/HostService.cpp
// This is the production pattern for creating an embedded server
ServerConsole::ConsoleRegistry console("EmbeddedServer");
auto pGameServer = std::make_unique<Server::GameServer>(console);
pGameServer->Initialize();
// Server is now listening, call pGameServer->Update() in a loop
```

### TiltedPhoques::Client Connect Pattern
```cpp
// Source: Code/libraries/networking/Client.hpp
// Client connects to server by endpoint string
TiltedPhoques::Client client;
client.Connect("127.0.0.1:10578");
// Pump with client.Update() until client.IsConnected()
```

### ProcMemReader Self-Test Pattern (proc_memory_tests.cpp)
```cpp
// Source: Code/tests/proc_memory_tests.cpp
ProcMemReader reader;
REQUIRE(reader.Open(getpid()));
volatile uint64_t canary = 0xDEADBEEFCAFEBABEULL;
uint64_t readBack = 0;
ssize_t bytesRead = reader.Read(&readBack, sizeof(readBack),
    reinterpret_cast<uint64_t>(&canary));
REQUIRE(bytesRead == sizeof(readBack));
REQUIRE(readBack == 0xDEADBEEFCAFEBABEULL);
```

### GameServer Message Handler Registration
```cpp
// Source: Code/server/GameServer.cpp:249-294
// Server auto-registers handlers for all message types via factory visitor
void GameServer::BindMessageHandlers() {
    auto handlerGenerator = [this](auto& x) {
        using T = typename std::remove_reference_t<decltype(x)>::Type;
        m_messageHandlers[T::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId) {
            auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);
            if (!pPlayer) { Kick(aConnectionId); return; }
            const auto pRealMessage = CastUnique<T>(std::move(apMessage));
            m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));
        };
        return false;
    };
    ClientMessageFactory::Visit(handlerGenerator);
    // Auth handler overridden separately
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Dedicated server + client (STR) | Embedded server in host player (P2P) | Phase 5 | server_runner deleted, only HostService path remains |
| GameNetworkingSockets (Valve) | enet6 | Pre-fork | All networking uses enet6 UDP transport |
| Test encoding only in isolation | Test through actual network stack | Phase 5 | Catches serialization issues that buffer-only tests miss |

## Open Questions

1. **TiltedPhoques::Client as base for mock client**
   - What we know: Client.hpp provides Connect(), Update(), OnConsume(), Send() virtual methods. MockClient would inherit from it.
   - What's unclear: Whether Client requires a libuv event loop or works with simple Update() polling. Need to verify during implementation.
   - Recommendation: Try simple inheritance first; if blocked, use raw enet6 client instead.

2. **enet6 initialization lifecycle**
   - What we know: Server/Client constructors likely call enet_initialize().
   - What's unclear: Whether multiple server instances in sequential test cases cause init/deinit issues.
   - Recommendation: Test with a simple two-test-case smoke test early. Add global init to main.cpp if needed.

3. **Server dependencies for test binary**
   - What we know: Server links Console, Resources, ESLoader, CrashHandler, BaseLib, AdminProtocol, SkyrimCoopNetworking, plus packages enet6, spdlog, sqlite3, lua, sol2, glm, entt.
   - What's unclear: Whether all these are needed for a headless test or if some can be stubbed.
   - Recommendation: Link everything (same as server target) for simplicity. Stubbing saves little and risks missing integration issues.

4. **Stretch goal feasibility: Timing of actor spawn**
   - What we know: When a second player connects, server assigns a character, client spawns a RemoteComponent entity.
   - What's unclear: How long after auth the remote actor appears in Skyrim memory at a readable address. The pointer table is populated by HOOK_ACTOR_ADDED events from the DLL.
   - Recommendation: Add generous timeouts (30+ seconds) in the in-game test. Use retry loop reading /proc/pid/mem until actor pointer appears.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| XMake | Build system | YES | v3.0.8 | -- |
| MinGW cross-compiler | DLL build (smoke test) | YES | custom toolchain in .toolchain/ | -- |
| enet6 | Networking tests | YES (xmake pkg) | xmake managed | -- |
| Catch2 | Test framework | YES (xmake pkg) | 2.13.9 | -- |
| spdlog | Server logging | YES (xmake pkg) | v1.13.0 | -- |
| Skyrim SE + Proton | Stretch goal in-game tests | YES (per deploy_and_test.sh) | Steam install | Skip stretch goal if unavailable |
| ptrace | /proc/pid/mem access | YES (per smoke_ptrace.sh) | Linux kernel | -- |

**Missing dependencies with no fallback:**
- None -- all dependencies available.

**Missing dependencies with fallback:**
- Skyrim SE runtime: Only needed for stretch goal (D-03). Fallback: skip in-game integration test.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 (header-only) |
| Config file | Code/tests/xmake.lua |
| Quick run command | `xmake build TPTests && xmake run TPTests` |
| Full suite command | `xmake build TPTests && xmake run TPTests` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TEST-01 | Existing tests compile and pass natively | unit | `xmake build TPTests && xmake run TPTests` | YES -- 6 existing test files |
| TEST-02 | Headless mock client connects to GameServer | integration | `xmake run TPTests "[NetworkIntegration]"` | NO -- Wave 0 |
| TEST-03 | Message round-trip through server | integration | `xmake run TPTests "[NetworkRoundTrip]"` | NO -- Wave 0 |

### Sampling Rate
- **Per task commit:** `xmake build TPTests && xmake run TPTests`
- **Per wave merge:** `xmake build TPTests && xmake run TPTests` (full suite)
- **Phase gate:** Full suite green + smoke tests pass

### Wave 0 Gaps
- [ ] `Code/tests/server_roundtrip_tests.cpp` -- covers TEST-03 (message round-trip through real server)
- [ ] `Code/tests/mock_client_tests.cpp` -- covers TEST-02 (headless client handshake)
- [ ] Update `Code/tests/xmake.lua` -- add server library dependencies for new integration tests

## Sources

### Primary (HIGH confidence)
- `Code/server/GameServer.h` and `GameServer.cpp` -- server instantiation, auth flow, message handling
- `Code/client/Services/HostService.cpp` -- embedded server pattern (production code)
- `Code/libraries/networking/Server.hpp` and `Client.hpp` -- enet6 transport API
- `Code/tests/encoding.cpp` -- existing test patterns
- `Code/tests/xmake.lua` -- existing test build configuration
- `Code/server_runner/` -- files to be removed (DediRunner, xmake.lua, main.cpp)
- `Code/encoding/Messages/ClientMessageFactory.h` -- all 46 client message types
- `Code/encoding/Messages/ServerMessageFactory.h` -- all 55 server message types
- `Code/relay_dll/protocol.h` -- TCP relay protocol (30 hook + 15 command opcodes)
- `Code/native_client/game_bridge/game_reader.h` -- memory read API for stretch goal

### Secondary (MEDIUM confidence)
- enet6 documentation -- enet_initialize/deinitialize lifecycle (based on enet API knowledge)
- Catch2 v2 documentation -- fixture patterns, tag-based filtering

### Tertiary (LOW confidence)
- None

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all libraries already in use, versions verified from xmake.lua
- Architecture: HIGH -- patterns derived directly from existing production code (HostService.cpp, encoding.cpp)
- Pitfalls: HIGH -- derived from code analysis of auth flow, port handling, init requirements
- Server removal: HIGH -- all files identified by direct inspection, dependency graph traced

**Research date:** 2026-03-28
**Valid until:** 2026-04-28 (stable codebase, no external dependency changes expected)
