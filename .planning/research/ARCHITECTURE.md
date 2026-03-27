# Architecture Patterns

**Domain:** Linux-first cross-compiled Skyrim co-op mod
**Researched:** 2026-03-27
**Confidence:** HIGH (based on existing codebase analysis + LethalInjection reference implementation)

## Recommended Architecture

### Overview: Adapted LethalInjection Pattern

LethalInjection uses a three-layer architecture: **Frontend (Linux GUI)** <-> **Middleware (Linux native daemon)** <-> **Backend (MinGW cross-compiled DLL injected into Wine process)**. SkyrimCoop adapts this but with crucial differences:

1. **SkyrimCoop's DLL is an SKSE plugin**, not a generic injectable -- it loads via SKSE's plugin mechanism, not DLL injection
2. **SkyrimCoop already has an embedded server** -- `HostService` already instantiates `Server::GameServer` inside the client process
3. **SkyrimCoop's middleware equivalent is the embedded server itself** -- there is no separate Linux daemon needed for game logic

The mapping is:

| LethalInjection Layer | SkyrimCoop Equivalent | Build Target |
|-|-|-|
| Frontend (Linux GUI) | Not needed (CEF overlay runs inside Skyrim/Wine) | N/A |
| Middleware (Linux native) | **Test harness + server standalone** (native Linux) | `SkyrimServerRunner`, `TPTests` |
| Backend (MinGW DLL) | **SkyrimTogetherClient.dll** (SKSE plugin via MinGW) | `SkyrimTogetherClient` |

### Component Diagram

```
Linux Host Machine
+------------------------------------------------------------------+
|                                                                  |
|  [Native Linux Builds]              [Wine/Proton Process]        |
|  +------------------------+         +------------------------+   |
|  | SkyrimServerRunner     |         | SkyrimSE.exe           |   |
|  | (standalone server     |         |   +------------------+ |   |
|  |  for testing only)     |         |   | SKSE             | |   |
|  +------------------------+         |   |   +-----------+  | |   |
|  | SkyrimTogetherServer   |         |   |   | Client DLL|  | |   |
|  | (static lib, native)   |         |   |   | (MinGW)   |  | |   |
|  +------------------------+         |   |   +-----------+  | |   |
|                                     |   |   | HostService| | |   |
|  [Test Harness]                     |   |   | (embedded  | | |   |
|  +------------------------+         |   |   |  server)   | | |   |
|  | TPTests                |         |   |   +-----------+  | |   |
|  | HeadlessTestRunner     |         |   +------------------+ |   |
|  | (native Linux,         |         |                        |   |
|  |  no Skyrim needed)     |         |   [Network: localhost]  |  |
|  +------------------------+         |   Host connects to self |  |
|  | SkyrimEncoding         |         |   Clients connect to    |  |
|  | CommonLib              |         |   host's IP             |  |
|  | (shared, native)       |         +------------------------+   |
|  +------------------------+                                      |
+------------------------------------------------------------------+
```

## Component Boundaries

### Tier 1: Platform-Independent Core (builds everywhere)

| Component | Responsibility | Dependencies | Build Output |
|-|-|-|-|
| `SkyrimEncoding` | Message serialization, opcodes, shared structs | GLM, TiltedCore | Static library |
| `CommonLib` | GameId, Cell, DateTime, Map utilities | None significant | Static library |
| `BaseLib` | Allocators, containers, threading primitives | mimalloc | Static library |
| `SkyrimCoopNetworking` | ENet6 UDP transport, Client/Server base classes | enet6, TiltedCore, snappy, libuv | Static library |
| `TiltedCore` | Buffer, String, Map, Set, UniquePtr wrappers | hopscotch-map | Static library |

These components have **zero Windows API dependencies**. They compile natively on Linux with GCC/Clang and cross-compile to Windows with MinGW. This is the foundation layer.

### Tier 2: Server Logic (builds native Linux + MinGW)

| Component | Responsibility | Dependencies | Build Output |
|-|-|-|-|
| `SkyrimTogetherServer` | GameServer, World, all server Services | Tier 1 + Sol2, Lua, sqlite3, sentry, spdlog | Static library |
| `Console` | ConsoleRegistry, command parsing | spdlog | Static library |
| `Resources` | Resource/asset collection | Tier 1 | Static library |
| `ESLoader` | Elder Scrolls plugin file parsing | Tier 1 | Static library |
| `CrashHandler` | Sentry + Crashpad integration | sentry-native | Static library |
| `AdminProtocol` | Admin RPC message definitions | Tier 1 | Static library |
| `SkyrimServerRunner` | Standalone server binary | Tier 2 libs | Binary executable |

The server already builds on Linux. The static library form of `SkyrimTogetherServer` is what gets linked into the client DLL for embedded hosting via `HostService`.

### Tier 3: Client DLL (MinGW cross-compile only)

| Component | Responsibility | Dependencies | Build Output |
|-|-|-|-|
| `SkyrimCoopReverse` | Function hooking, signature scanning, memory patching | minhook, mem, xbyak | Static library |
| `SkyrimCoopHooks` | D3D11, DInput, Windows API hooks | Tier 1 + Reverse, d3d11, dinput8 | Static library |
| `SkyrimCoopUI` | CEF browser integration, overlay rendering | cef, DirectXTK, d3d11 | Static library |
| `SkyrimCoopUIProcess` | CEF render subprocess, V8 bridge | cef | Static library |
| `SkyrimTogetherClient` | SKSE plugin: Services, Components, Systems, Game hooks | All tiers + SKSE | Shared library (.dll) |
| `TPProcess` | CEF worker process executable | UIProcess lib | Binary executable |

These components use Windows APIs (Win32, D3D11, DInput) and **must be cross-compiled via MinGW**.

### Tier 4: Test Infrastructure (native Linux only)

| Component | Responsibility | Dependencies | Build Output |
|-|-|-|-|
| `TPTests` | Unit tests for encoding, quantization, serialization | Catch2, Tier 1 | Binary executable |
| `HeadlessTestRunner` (new) | Integration tests: simulated host+client sessions | Tier 1 + Tier 2, Catch2 | Binary executable |

## Data Flow

### Host Player Session Flow

```
1. Player launches Skyrim via Proton
2. SKSE loads SkyrimTogetherClient.dll
3. Client World initializes all services
4. Player clicks "Host" in CEF overlay
5. HostService::StartHosting() creates Server::GameServer
6. GameServer::Host() binds UDP port via enet6
7. TransportService::Connect("127.0.0.1:port") -- host connects to self
8. Host authenticates as first player
9. Remote players connect to host's IP

Each frame:
  World::Update()
    -> RunnerService tick
    -> TransportService::OnUpdate() -- process incoming network packets
    -> CharacterService -- detect local actor changes, send to server
    -> HostService::OnUpdate() -- tick embedded GameServer
       -> Server::GameServer::OnUpdate()
          -> Process all client messages
          -> Broadcast state to connected clients
    -> InterpolationSystem -- smooth remote player positions
    -> AnimationSystem -- sync remote animations
```

### Host's Embedded Server Data Path (In-Process)

```
Host Client -> TransportService::Send()
            -> enet6 serializes to localhost UDP
            -> GameServer::OnConsume() receives packet
            -> Server CharacterService processes
            -> GameServer::SendToPlayers() broadcasts
            -> enet6 sends to all peers (including localhost)
            -> Host's TransportService::OnConsume() receives broadcast
            -> Client CharacterService applies remote state
```

The localhost round-trip through enet6 is intentional -- it keeps the code paths identical between host and client, avoiding special-case bugs. The UDP loopback overhead is negligible (sub-microsecond on Linux).

### MinGW Cross-Compile Data Flow (Build Time)

```
Developer's Linux Machine:
  xmake f -p mingw --mingw=$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content
  xmake -y

  Build order:
    1. Tier 1 (platform-independent) -- cross-compiled with x86_64-w64-mingw32-g++
    2. Tier 2 (server logic) -- cross-compiled, produces .a static libs
    3. Tier 3 (client) -- cross-compiled, links all into SkyrimTogetherClient.dll
    4. xmake install -o distrib -> produces DLL + assets

  Native builds (parallel):
    1. Tier 1 -- compiled with native gcc/clang
    2. Tier 2 -- compiled natively
    3. Tier 4 (tests) -- compiled natively, linked against native Tier 1+2
    4. Run tests natively
```

## Patterns to Follow

### Pattern 1: Dual-Build Static Libraries

**What:** Every component in Tier 1 and Tier 2 compiles as a static library under both native Linux (for testing) and MinGW (for the DLL). The XMake build system builds both toolchains from the same source.

**When:** Always. This is the core enabler for Linux-first development.

**Implementation:**
```lua
-- xmake.lua for a tier-1 library
target("SkyrimEncoding")
    set_kind("static")
    set_group("Encoding")
    add_files("**.cpp")
    add_includedirs(".", {public = true})
    add_deps("TiltedCore", "CommonLib")
    add_packages("glm", "entt")
    -- No platform-specific code. Compiles identically on both toolchains.
```

The existing XMake project already does this for the server and encoding targets. The key insight is that XMake's `mingw` platform uses `is_plat("mingw")` not `is_plat("windows")`, so platform gates need updating.

### Pattern 2: HostService as Server Lifecycle Manager

**What:** The `HostService` (already implemented) owns the `Server::GameServer` instance, ticking it on the client's update loop. The host client connects to itself via localhost.

**When:** Always for co-op mode. The standalone `SkyrimServerRunner` exists only for testing and debugging.

**Why this works:** The existing implementation in `HostService.cpp` already does the right thing -- it creates the server, the host player connects to `127.0.0.1`, and both code paths (host and guest) use identical network stacks. No special-casing needed.

### Pattern 3: XMake Multi-Toolchain with Platform Detection

**What:** A single `xmake.lua` tree supports three build modes:
1. `xmake f -p windows` -- MSVC on Windows (original)
2. `xmake f -p mingw --mingw=/path/to/xpack` -- MinGW cross-compile from Linux
3. `xmake f -p linux` -- Native Linux (server + tests only)

**Implementation approach:**
```lua
-- Root xmake.lua platform gating
-- Tier 1: Always build
includes("encoding")
includes("common")
includes("base")
includes("TiltedCore")
includes("libraries/networking")  -- networking is platform-independent (enet6)

-- Tier 2: Server logic -- builds on all platforms
includes("server")
includes("components")
includes("admin_protocol")

-- Tier 3: Client DLL -- only on Windows or MinGW
if is_plat("windows") or is_plat("mingw") then
    includes("client")
    includes("libraries/hooks")     -- Win32 hooks
    includes("libraries/reverse")   -- Win32 memory manipulation
    includes("libraries/ui")        -- CEF integration
    includes("libraries/ui_process")
    includes("tp_process")
    includes("immersive_launcher")
end

-- Tier 4: Tests -- native Linux only (or native Windows)
if not is_plat("mingw") then
    includes("tests")
end

-- Standalone server runner -- native Linux or Windows
if is_plat("linux") or (is_plat("windows") and not is_plat("mingw")) then
    includes("server_runner")
end
```

### Pattern 4: Headless Test Harness via Server-Only Simulation

**What:** Integration tests instantiate a `Server::GameServer` and multiple simulated `TiltedPhoques::Client` instances natively on Linux. No Skyrim, no Wine, no DLL injection. Tests verify that messages serialize correctly, server state updates as expected, and multi-client scenarios (join, move, disconnect) work.

**When:** CI pipeline, pre-commit testing, development iteration.

**Implementation:**
```cpp
// tests/integration/host_client_test.cpp
#include <catch2/catch.hpp>
#include <GameServer.h>
#include <Client.hpp>
#include <Messages/AuthenticationRequest.h>

struct MockClient : TiltedPhoques::Client {
    std::vector<std::vector<uint8_t>> received;
    bool connected = false;

    void OnConsume(const void* apData, uint32_t aSize) override {
        received.emplace_back(
            static_cast<const uint8_t*>(apData),
            static_cast<const uint8_t*>(apData) + aSize
        );
    }
    void OnConnected() override { connected = true; }
    void OnDisconnected(EDisconnectReason) override { connected = false; }
    void OnUpdate() override {}
};

TEST_CASE("Two clients connect and see each other") {
    ServerConsole::ConsoleRegistry console("test");
    Server::GameServer server(console);
    server.Initialize();

    MockClient host, guest;
    host.Connect("127.0.0.1:" + std::to_string(server.GetPort()));
    guest.Connect("127.0.0.1:" + std::to_string(server.GetPort()));

    // Pump network for a few ticks
    for (int i = 0; i < 10; i++) {
        server.Update();
        host.Update();
        guest.Update();
    }

    REQUIRE(host.connected);
    REQUIRE(guest.connected);
    REQUIRE(server.GetClientCount() == 2);
}
```

This works because `TiltedPhoques::Server` and `TiltedPhoques::Client` are thin wrappers around enet6, which is a pure C library with no Windows dependencies. The entire server + networking stack runs natively on Linux.

## Anti-Patterns to Avoid

### Anti-Pattern 1: Bypassing Network for Host

**What:** Making the host client call server methods directly (skipping serialization/deserialization) to "optimize" the host path.

**Why bad:** Creates divergent code paths between host and guest. Bugs will appear only for guests or only for hosts. Eliminates the ability to test the full pipeline without running Skyrim.

**Instead:** Always go through the network stack, even on localhost. The enet6 loopback is effectively zero-cost.

### Anti-Pattern 2: Wine MSVC as Long-Term Strategy

**What:** The current codebase has extensive `get_config("sdk") == "/opt/msvc"` checks for a Wine-hosted MSVC toolchain. This was a stepping stone.

**Why bad:** Wine MSVC is fragile (broken `lib.exe`, object library workarounds, missing UI features). MinGW is the proven path -- LethalInjection validates that MinGW cross-compilation works for production Windows DLLs running under Wine/Proton.

**Instead:** Transition to MinGW cross-compilation. Remove Wine MSVC workarounds once MinGW is stable. Keep native MSVC as a secondary CI target for Windows users.

### Anti-Pattern 3: Monolithic Test Binary

**What:** Putting all tests in one `TPTests` executable with a single `encoding.cpp`.

**Why bad:** Slow iteration, no parallelism, discourages writing tests for new features.

**Instead:** Split into test targets by layer:
- `TPTests_Encoding` -- message serialization
- `TPTests_Server` -- server logic integration
- `TPTests_Integration` -- multi-client simulation

### Anti-Pattern 4: Conditional Compilation for Host vs Guest

**What:** Using `#ifdef HOST_MODE` throughout client code to change behavior based on whether the player is hosting.

**Why bad:** Combinatorial explosion of code paths, untestable without running both modes.

**Instead:** The existing `HostService` pattern is correct -- the client always behaves as a client. The only difference is whether `HostService` has a running `GameServer`. All gameplay code is identical.

## Build Order (Dependency Graph)

Build dependencies flow strictly downward. No circular dependencies.

```
Layer 0: External packages (enet6, entt, glm, spdlog, Sol2, Lua, CEF, etc.)
    |
Layer 1: TiltedCore (Buffer, String, Map, Set, Stl wrappers)
    |
Layer 2: BaseLib, CommonLib, SkyrimEncoding, SkyrimCoopNetworking
    |         \___________________________
    |                                     |
Layer 3: SkyrimTogetherServer (static)    |
    |    Console, Resources, ESLoader     |
    |    CrashHandler, AdminProtocol      |
    |                                     |
    +--[mingw only]-----------------------+
    |                                     |
Layer 4: SkyrimCoopReverse, SkyrimCoopHooks (Win32 APIs)
    |    SkyrimCoopUI, SkyrimCoopUIProcess (CEF + D3D11)
    |
Layer 5: SkyrimTogetherClient.dll (links everything)
         SkyrimServerRunner (links Layer 0-3, native only)
         TPTests (links Layer 0-3, native only)
```

**Critical path for MinGW migration:**
1. Layer 0: Get all packages building under MinGW (enet6, entt, spdlog are portable; CEF, minhook, DirectXTK need MinGW validation)
2. Layer 1-2: Should compile without changes (already platform-independent)
3. Layer 3: Already compiles on Linux natively; MinGW should be trivial
4. Layer 4: Hooks/Reverse use Win32 APIs that exist in MinGW headers. CEF is pre-built -- need MinGW-compatible CEF binaries or build from source
5. Layer 5: Final linking with MinGW producing .dll instead of MSVC .dll

**Build order implications for phases:**
- Phase 1: Get Layers 0-3 compiling under `xmake f -p mingw` and tests passing natively
- Phase 2: Get Layer 4 compiling (Win32 hooks via MinGW, CEF integration)
- Phase 3: Full Layer 5 producing a loadable SKSE DLL
- Phase 4: Integration testing with Wine/Proton

## How LethalInjection Pattern Maps to This Project

### What Transfers Directly

| LethalInjection | SkyrimCoop | Notes |
|-|-|-|
| xPack MinGW-w64 GCC 14.3.0 toolchain | Same toolchain, same `$HOME/.local/xPacks/` path | Proven working |
| `-static -static-libgcc -static-libstdc++` link flags | Same flags for client DLL | Eliminates MinGW runtime dependency |
| `CMAKE_SYSTEM_NAME Windows` + cross-prefix detection | XMake `--mingw=` does this automatically | XMake is easier than CMake here |
| Separate build dirs per target (native vs cross) | XMake handles this natively with `-p mingw` vs `-p linux` | No manual separation needed |
| Wine detection via `ntdll.dll` export check | Already relevant for client DLL running under Proton | Copy approach |
| `BOOL APIENTRY DllMain()` entry point | SkyrimCoop uses `SKSEPluginLoad()` via SKSE | Different entry mechanism but same DLL structure |
| Static lib core + thin DLL wrapper | Already the pattern: `SkyrimTogetherServer` (static) linked into client DLL | Validated approach |

### What Does NOT Transfer

| LethalInjection | Why Not for SkyrimCoop |
|-|-|
| Three-layer architecture (frontend/middleware/backend) | SkyrimCoop has no separate frontend or middleware -- everything runs inside Skyrim or as a standalone server |
| MCP protocol between layers | No IPC needed -- server is embedded in client process |
| `/proc/pid/mem` reading for debugging | Not relevant -- use standard GDB/Wine debugging |
| LuaJIT cross-compilation for DLL | SkyrimCoop uses plain Lua 5.1 via Sol2, not LuaJIT |
| MinHook initialization in DllMain | SKSE handles hooking setup differently |

### What Adapts With Modification

| LethalInjection Pattern | SkyrimCoop Adaptation |
|-|-|
| CMake presets for toolchain selection | XMake `xmake f -p mingw --mingw=/path` (simpler, no preset files needed) |
| `--allow-multiple-definition` linker flag | May be needed if MinGW and MSVC link behavior differs for TiltedCore symbols |
| `GoogleTest` in native builds only | `Catch2` in native builds only (already using Catch2) |
| Sanitizer preset (ASan/UBSan) | Add `xmake f --policies=build.sanitizer.address` for native test builds |
| `cmake-format` / `clang-tidy` static analysis | Keep existing `clang-format`, add `clang-tidy` for native builds |

## MinGW-Specific Concerns for SkyrimCoop

### Package Compatibility Matrix

| Package | MinGW Compatible | Notes |
|-|-|-|
| enet6 | YES | Pure C, no platform issues |
| entt | YES | Header-only C++20 |
| spdlog | YES | Header-only with fmt |
| glm | YES | Header-only math |
| Sol2 | YES | Header-only Lua bindings |
| Lua 5.1 | YES | Pure C |
| mimalloc | YES | Has MinGW support |
| hopscotch-map | YES | Header-only |
| snappy | YES | Pure C++ |
| cryptopp | LIKELY | Has MinGW makefiles, verify |
| minhook | YES | LethalInjection uses it with MinGW |
| mem | VERIFY | Signature scanning lib, likely fine |
| xbyak | VERIFY | JIT assembler, should work |
| CEF | UNCERTAIN | Pre-built binaries are MSVC; may need to link against import libs or find MinGW-compatible build |
| DirectXTK | UNCERTAIN | Microsoft library, typically MSVC; may need MinGW port or replacement |
| discord SDK | LOW RISK | Pre-built, but has C API that should work |
| sentry-native | VERIFY | Has CMake build, should support MinGW |
| sqlite3 | YES | Pure C |
| catch2 | YES | Header-only |
| libuv | YES | Has MinGW support |
| zlib | YES | Pure C |

**Highest risk:** CEF and DirectXTK. CEF provides MSVC-compiled `.lib` files; linking these from MinGW requires import library conversion or building CEF from source with MinGW. DirectXTK similarly assumes MSVC. These are both Tier 3 (client-only) concerns and should be the last items addressed.

## Scalability Considerations

| Concern | 2 Players (Target) | 4 Players (Max) | Notes |
|-|-|-|-|
| Server tick rate | 60Hz embedded in client frame loop | 60Hz, fine | enet6 handles this easily |
| Network bandwidth | ~5KB/s per player | ~15KB/s total | Differential serialization keeps this low |
| Entity count | Host's loaded NPCs (~50-100) | Same | Host authoritative, no distributed ownership |
| Memory overhead | ~20MB for server World | ~25MB | Negligible vs Skyrim's 4-8GB |
| Build time | 2-3 min full rebuild | N/A | Unity build option exists for faster compilation |
| Test execution | < 10s for unit tests | N/A | Integration tests may take 30-60s with network simulation |

## Sources

- LethalInjection reference implementation: `/media/bighass/4d2c4781-34de-41a5-8939-e35551bc9a5d5/Projects/LethalInjection/` (HIGH confidence -- same developer, proven pattern)
- XMake cross-compilation docs: [xmake.io/guide/basic-commands/cross-compilation.html](https://xmake.io/guide/basic-commands/cross-compilation.html) (HIGH confidence)
- XMake MinGW platform: [xmake.io/guide/basic-commands/build-configuration.html](https://xmake.io/guide/basic-commands/build-configuration.html) (HIGH confidence)
- Existing SkyrimCoop codebase analysis: `Code/client/Services/HostService.cpp`, `Code/server/GameServer.h`, `Code/libraries/networking/` (HIGH confidence)
- EnTT ECS: [github.com/skypjack/entt](https://github.com/skypjack/entt) (HIGH confidence)
- enet6 networking: Pure C UDP library, platform-independent (HIGH confidence)

---

*Architecture research: 2026-03-27*
