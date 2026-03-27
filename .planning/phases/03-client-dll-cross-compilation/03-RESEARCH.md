# Phase 3: Client DLL Cross-Compilation - Research

**Researched:** 2026-03-27
**Domain:** MinGW cross-compilation of SKSE plugin DLL with CEF/Discord/DirectXTK exclusion
**Confidence:** HIGH

## Summary

Phase 3 takes the MinGW toolchain established in Phases 1-2 and applies it to the client DLL (`SkyrimTogetherClient`). The primary challenge is surgically excluding CEF, Discord SDK, and DirectXTK while keeping the core game logic, networking, hooking, and Skyrim integration intact. The codebase has deep CEF tendrils -- not just in OverlayService/OverlayClient, but also in InputService, PartyService, and Renderer.cpp, all of which call CEF APIs (CefListValue, OverlayApp, ExecuteAsync). The DiscordService header directly includes `<discord.h>` and uses Discord SDK types throughout. RenderSystemD3D11 and ImguiService both depend on D3D11 headers and COM interfaces.

The secondary challenge is the mimalloc-to-rpmalloc migration in `Code/client/Games/Memory.cpp`, which hooks Skyrim's heap allocator using mimalloc APIs (`mi_malloc`, `mi_malloc_size`, `mi_calloc`, `mi_free`, `mi_malloc_aligned`). This file also uses `#pragma optimize("", off)` which is MSVC-specific.

The tertiary challenge is client-specific MSVC-isms: `#pragma comment(lib, "version.lib")` in VersionDb.h, `#pragma optimize` in Memory.cpp and AnimationExperiments.cpp, `_PIFV` type in Memory.cpp, and the `<intrin.h>` include in the precompiled header. There are ~546 instances of MSVC-specific patterns across client code (POINTER_SKYRIMSE macros, TP_THIS_FUNCTION, TP_HOOK_IAT etc.), but most of these are from the TiltedPhoques framework which Phase 2 already made MinGW-compatible.

**Primary recommendation:** Structure work in three waves: (1) XMake build system changes to exclude CEF/Discord/DirectXTK and add per-feature guards, (2) Source code guards and mimalloc-to-rpmalloc migration, (3) Compile, fix remaining MSVC-isms, and smoke test.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Compile-time exclusion using per-feature preprocessor guards: `HAS_CEF`, `HAS_DISCORD`, `HAS_DIRECTXTK`. Not a single umbrella flag -- each feature is independently toggleable.
- **D-02:** OverlayService, OverlayClient, RenderSystemD3D11, and DiscordService become no-ops when their respective guards are undefined. The DLL loads but has no UI overlay.
- **D-03:** SkyrimCoopUI and SkyrimCoopUIProcess targets are excluded from the MinGW build via XMake platform/config guards.
- **D-04:** ImGui (already vendored in `Code/external/imgui`) stays wired in the build for Phase 6 readiness.
- **D-05:** Cross-compile MinHook and Xbyak as-is first. Both have some MinGW support upstream. GATE-02 (Phase 1) already validated basic MinHook hooking under MinGW.
- **D-06:** If compilation fails, apply small targeted patches (ifdef guards, type fixes, asm syntax). If patches grow too large or complex, reconsider and evaluate alternatives. No hard line count -- use judgment.
- **D-07:** Validation target is **Load + Connect**: DLL loads into Skyrim SE via SKSE under Proton, and establishes a connection to a locally running server. Full character sync is NOT required in Phase 3.
- **D-08:** A smoke test script is included in scope. The script launches Skyrim under Wine/Proton, spins up a local server, and verifies the client establishes a connection. Returns pass/fail.
- **D-09:** Manual testing supplements the smoke test for edge cases.
- **D-10:** Extend Phase 2 patterns to client code: portable C++20 where possible, `__attribute__` fallbacks, delete MSVC build blocks. Skyrim struct alignments use `alignas()` with `static_assert` for layout verification.
- **D-11:** Switch client from mimalloc to rpmalloc, consistent with Phase 2 decision. Memory.cpp updated to use rpmalloc APIs.
- **D-12:** Windows syslinks (version, dbghelp, kernel32) kept as-is -- MinGW provides import libraries for all standard Windows DLLs.

### Claude's Discretion
- Specific MinHook/Xbyak patches if needed (case-by-case during implementation)
- Smoke test script implementation details (shell script vs Python, log parsing approach)
- Order of client source file cleanup (which files to tackle first)
- How to structure the per-feature guard macros in XMake configuration

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BUILD-04 | Tier 3 client DLL compiles under MinGW (CEF and DirectXTK excluded, replaced by ImGui) | XMake guard patterns, per-feature preprocessor flags, CEF dependency map across 5 service files, Memory.cpp rpmalloc migration, MSVC-ism cleanup patterns |
| BUILD-05 | MinGW-compiled DLL loads into Skyrim SE under Proton without crash | Smoke test script architecture, SKSE ABI validation via Phase 1 GATE-01/GATE-02 precedent, MinHook/Xbyak cross-compilation from feasibility tests |
</phase_requirements>

## Project Constraints (from CLAUDE.md)

- Build system: XMake 2.8.5+, MinGW cross-compilation
- C++20 standard, no exceptions
- Naming: `a` prefix for args, `m_` for members, PascalCase for classes
- Error handling via return codes and spdlog logging, not exceptions
- `#pragma once` for header guards
- Code formatted with clang-format
- PRs to `dev` branch

## Architecture Patterns

### CEF Dependency Map (Files Requiring Guards)

Research identified exactly which files use CEF APIs and need `HAS_CEF` guards:

| File | CEF Usage | Guard Strategy |
|------|-----------|----------------|
| `Services/OverlayService.h` | `CefRefPtr<OverlayApp>`, `#include <include/internal/cef_ptr.h>` | Entire class becomes no-op stub |
| `Services/OverlayClient.h` | Inherits `TiltedPhoques::OverlayClient`, uses `CefRefPtr<CefBrowser>` | Entire file excluded |
| `Services/Generic/OverlayService.cpp` | `CefListValue::Create()` throughout, `OverlayApp`, `OverlayRenderHandlerD3D11` | Entire file excluded (stub .cpp used instead) |
| `Services/Generic/OverlayClient.cpp` | Full CEF client implementation | Entire file excluded |
| `Services/Generic/InputService.cpp` | `#include <include/internal/cef_types.h>`, `cef_key_event_type_t`, `EVENTFLAG_*`, `MBT_*` constants, `OverlayApp::InjectKey/InjectMouseMove/InjectMouseButton/InjectMouseWheel` | Needs significant rework -- CEF types deeply embedded in input processing |
| `Services/Generic/PartyService.cpp` | `#include <OverlayApp.hpp>`, `CefListValue::Create()`, `ExecuteAsync("partyInfo")`, `ExecuteAsync("partyInviteReceived")` | Guard CEF calls only (party logic stays) |
| `Games/Renderer.cpp` | `D3D11Hook::Get().OnPresent()`, `D3D11Hook::Get().OnCreate()`, `RenderSystemD3D11` include | Guard rendering hooks |

### Discord Dependency Map

| File | Discord Usage | Guard Strategy |
|------|---------------|----------------|
| `Services/DiscordService.h` | `#include <discord.h>`, Discord SDK types throughout | Entire class becomes no-op stub |
| `Services/Generic/DiscordService.cpp` | Full Discord SDK implementation | Entire file excluded |
| `Services/Generic/InputService.cpp` | `World::Get().ctx().at<DiscordService>().WndProcHandler()` | Guard the call |

### D3D11/DirectXTK Dependency Map

| File | D3D Usage | Guard Strategy |
|------|-----------|----------------|
| `TiltedOnlineApp.h` | `#include <d3d11.h>`, `ID3D11Device* m_pDevice` | Guard D3D members and includes |
| `TiltedOnlineApp.cpp` | `RenderSystemD3D11` creation, `D3D_FEATURE_LEVEL`, `CreateEarlyDxDevice` | Guard BeginMain/EndMain D3D code |
| `Systems/RenderSystemD3D11.h/.cpp` | Full D3D11 rendering system | Entire file excluded |
| `Services/ImguiService.h/.cpp` | `ImGui_ImplDX11_Init`, D3D11 device/context | Guard D3D11 backend (ImGui framework stays for Phase 6) |
| `NvidiaUtil.h/.cpp` | D3D11 device creation for Nvidia fix | Guard or exclude |
| `Games/Skyrim/BSGraphics/BSGraphicsRenderer.h/.cpp` | `ID3D11Device*`, `IDXGISwapChain*` in Skyrim structs | These are Skyrim struct definitions -- keep as data layout, guard functional code |
| `Games/Renderer.cpp` | `D3D11Hook`, `BGSRenderer` with swap chain access | Guard rendering hook setup |

### Recommended Guard Pattern

Per-feature guards defined in XMake, propagated via `add_defines()`:

```lua
-- In Code/client/xmake.lua for MinGW
if is_plat("mingw") then
    -- CEF not available on MinGW
    -- Discord SDK not available on MinGW
    -- DirectXTK not available on MinGW
else
    add_defines("HAS_CEF=1")
    add_defines("HAS_DISCORD=1")
    add_defines("HAS_DIRECTXTK=1")
end
```

In source code, follow the established `HAS_SENTRY` pattern from `CrashHandler.cpp`:

```cpp
// OverlayService.h -- no-op stub pattern
#pragma once

#ifdef HAS_CEF
#include <include/internal/cef_ptr.h>
// ... full CEF-dependent class definition
#else
// No-op stub -- DLL loads without UI overlay
struct OverlayService
{
    OverlayService(World& aWorld, TransportService& transport, entt::dispatcher& aDispatcher);
    ~OverlayService() noexcept;
    TP_NOCOPYMOVE(OverlayService);

    void Create(void*) noexcept {}
    void Render() noexcept {}
    void Reset() const noexcept {}
    void Reload() noexcept {}
    void Initialize() noexcept {}
    void SetActive(bool) noexcept {}
    [[nodiscard]] bool GetActive() const noexcept { return false; }
    void SetInGame(bool) noexcept {}
    [[nodiscard]] bool GetInGame() const noexcept { return false; }
    void SetVersion(const std::string&) {}
    void* GetOverlayApp() const noexcept { return nullptr; }
    void SendSystemMessage(const std::string&) {}
    void SetPlayerHealthPercentage(uint32_t) const noexcept {}

private:
    World& m_world;
    TransportService& m_transport;
    // No CEF members, no event connections that call CEF
};
#endif
```

### World.cpp Service Initialization Guards

World.cpp creates all services in its constructor. Guard pattern:

```cpp
// Always create -- constructor is a no-op without HAS_CEF
ctx().emplace<OverlayService>(*this, m_transport, m_dispatcher);

#ifdef HAS_CEF
ctx().emplace<InputService>(ctx().at<OverlayService>());
#endif

#ifdef HAS_DISCORD
ctx().emplace<DiscordService>(m_dispatcher);
#endif
```

### TiltedOnlineApp.cpp Guards

```cpp
bool TiltedOnlineApp::BeginMain()
{
    World::Create();
#ifdef HAS_DISCORD
    World::Get().ctx().at<DiscordService>().Init();
#endif
#ifdef HAS_CEF
    World::Get().ctx().emplace<RenderSystemD3D11>(
        World::Get().ctx().at<OverlayService>(),
        World::Get().ctx().at<ImguiService>());
#endif
    LoadScriptExender();
    // ...
}
```

### Memory.cpp: mimalloc to rpmalloc Migration

Current state: `Code/client/Games/Memory.cpp` uses mimalloc directly:
- `#include <mimalloc.h>` and `#include <TiltedCore/MimallocAllocator.hpp>`
- `mi_malloc_size()`, `mi_free()`, `mi_calloc()`, `mi_malloc()`, `mi_malloc_aligned()`
- `static TiltedPhoques::MimallocAllocator s_allocator;`

Migration mapping (rpmalloc API equivalents):

| mimalloc | rpmalloc | Notes |
|----------|----------|-------|
| `mi_malloc(size)` | `rpmalloc(size)` | Direct replacement |
| `mi_malloc_aligned(size, align)` | `rpmemalign(align, size)` | Note: arg order swapped |
| `mi_malloc_size(ptr)` | `rpmalloc_usable_size(ptr)` | Direct replacement |
| `mi_free(ptr)` | `rpfree(ptr)` | Direct replacement |
| `mi_calloc(count, size)` | `rpcalloc(count, size)` | Direct replacement |
| `MimallocAllocator` | `RpmallocAllocator` | Already exists in TiltedCore |

Critical: rpmalloc requires `rpmalloc_initialize()` before any allocation and `rpmalloc_thread_initialize()` per thread. The TiltedCore `RpmallocAllocator` handles this, but the raw `rpmalloc()` calls in the hook functions need the init call to have happened first. The `s_memoryHooks` Initializer runs early -- ensure rpmalloc init precedes it.

### MSVC-ism Cleanup in Client Code

| Pattern | Location | Fix |
|---------|----------|-----|
| `#pragma comment(lib, "version.lib")` | `VersionDb.h` | Delete -- already linked via `add_syslinks("version")` in xmake.lua |
| `#pragma optimize("", off/on)` | `Memory.cpp`, `AnimationExperiments.cpp` | Replace with `__attribute__((optimize("O0")))` on functions, or use `#pragma GCC optimize("O0")` |
| `<intrin.h>` in PCH | `TiltedOnlinePCH.h` | Guard with `#ifdef _MSC_VER` -- MinGW uses `<x86intrin.h>` instead |
| `_PIFV` type | `Memory.cpp` line 168 | `typedef int (*_PIFV)(void)` -- provide definition under MinGW |
| `strcpy_s`, `strncpy_s` | `DiscordService.cpp` | These exist in MinGW's `<string.h>` with `__STDC_WANT_LIB_EXT1__` or use portable alternatives |
| `__cdecl` in hook declarations | `Memory.cpp` | MinGW supports `__cdecl` -- no change needed |

### InputService Rework Strategy

InputService.cpp is the most complex file to guard because CEF types are deeply embedded in the input processing logic (`cef_key_event_type_t`, `cef_mouse_button_type_t`, `EVENTFLAG_*` constants). Two approaches:

**Option A (Recommended): Define equivalent constants locally when HAS_CEF is not defined.**
The CEF event types are just integer constants. Define them locally so the input processing logic compiles without CEF headers, but the actual CEF dispatch calls are guarded. This preserves the input processing infrastructure for Phase 6 (ImGui will need similar input handling).

**Option B: Exclude InputService entirely from MinGW build.**
Simpler but loses input handling infrastructure. Since Phase 3 goal is Load + Connect only (no UI interaction needed), this is acceptable.

Recommendation: Option B for Phase 3. InputService is tightly coupled to CEF overlay activation, and without CEF there is no overlay to activate. Phase 6 will rewrite InputService for ImGui anyway.

### XMake Build System Changes

Current `Code/client/xmake.lua` unconditionally adds:
- `add_deps("SkyrimCoopUIProcess", "SkyrimCoopUI")` -- must be conditional
- `add_packages("mimalloc", "discord", "cef")` -- must be conditional
- `after_install` copies CEF and Discord DLLs -- must be conditional

Current `Code/libraries/xmake.lua`:
- `SkyrimCoopUI` and `SkyrimCoopUIProcess` are already inside `if is_plat("windows") or is_plat("mingw")` block
- These need to be further restricted to exclude MinGW, OR gated on `HAS_CEF`

The `WINE_MSVC_BUILD` define already exists as precedent for conditional compilation in the client xmake.lua.

### ImGui in MinGW Build

Per D-04, ImGui stays in the build. Current state:
- `ImguiService.h` includes `imgui/ImGuiDriver.h` (vendored)
- `ImguiService.cpp` uses `ImGui_ImplDX11_Init` (D3D11 backend)
- `ImGuiImpl` target in `Code/external/imgui/xmake.lua` needs verification

For Phase 3: ImGui framework code compiles, but the D3D11 backend initialization in ImguiService is guarded behind `HAS_DIRECTXTK` (or a more specific `HAS_D3D11`). ImguiService becomes a partial no-op -- the ImGui context exists but has no rendering backend until Phase 6 adds one.

### Precompiled Header Adaptation

`TiltedOnlinePCH.h` includes:
- `<windows.h>`, `<winsock2.h>`, `<ws2tcpip.h>` -- MinGW provides all of these
- `<intrin.h>` -- needs `#ifdef _MSC_VER` / `#else` `<x86intrin.h>`
- TiltedCore headers -- already MinGW-compatible from Phase 2
- TiltedReverse headers (`AutoPtr.hpp`, `App.hpp`, `FunctionHook.hpp`, etc.) -- depend on MinHook/Xbyak
- `<Commctrl.h>` in main.cpp -- MinGW provides this

The PCH includes `<FunctionHook.hpp>` and `<JitAssembly.hpp>` which depend on MinHook and Xbyak respectively. These are the critical path for MinHook/Xbyak cross-compilation success.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| rpmalloc API wrappers | Custom malloc wrappers | `TiltedPhoques::RpmallocAllocator` | Already exists in TiltedCore, handles init/thread-init |
| CEF type stubs | Full CEF type system stubs | Exclude files entirely + minimal struct stubs | CEF type hierarchy is massive; stubbing individual types is endless |
| Windows API stubs for MinGW | Custom Windows API headers | MinGW's built-in Windows headers | MinGW provides complete Win32 API headers |
| MinHook alternative | Custom hooking library | MinHook v1.3.3 (validated in GATE-02) | Already proven to work under MinGW in Phase 1 feasibility tests |

## Common Pitfalls

### Pitfall 1: CEF Leakage Through PartyService and InputService
**What goes wrong:** Guards are added to OverlayService/OverlayClient but CEF calls in PartyService.cpp (lines 22, 128-137, 147-149) and InputService.cpp (line 11, throughout) are missed, causing link failures.
**Why it happens:** CEF usage extends beyond the "obvious" overlay files into services that dispatch UI events.
**How to avoid:** Use the dependency map above. Grep for `CefListValue`, `CefRefPtr`, `OverlayApp`, `ExecuteAsync`, `cef_` across all client code before declaring guards complete.
**Warning signs:** Link errors mentioning `CefListValue::Create`, `OverlayApp::ExecuteAsync`, or `cef_` symbols.

### Pitfall 2: Memory.cpp Hook Functions Need rpmalloc Thread Init
**What goes wrong:** rpmalloc calls in hooked malloc/free crash because `rpmalloc_thread_initialize()` hasn't been called on the calling thread.
**Why it happens:** Skyrim's heap hooks fire on threads that rpmalloc doesn't know about. mimalloc handles this transparently; rpmalloc requires explicit per-thread init.
**How to avoid:** Use `rpmalloc_thread_initialize()` at the start of each hooked function if the thread hasn't been initialized, or call `rpmalloc_initialize()` with thread cache enabled. Check `RpmallocAllocator` source for how TiltedCore handles this.
**Warning signs:** Crashes in `rpmalloc` internals, null pointer dereferences in allocation functions.

### Pitfall 3: OverlayService Stub Constructor Disconnects Event Wiring
**What goes wrong:** The no-op OverlayService stub omits the event dispatcher connections, but other services still dispatch events (UpdateEvent, ConnectedEvent, etc.) that OverlayService was handling. Some of those handlers forwarded state to the UI -- if the stub doesn't handle them, no crash but potential state tracking loss.
**Why it happens:** The original OverlayService constructor wires 15 event connections. The stub needs the same constructor signature but does NOT need to wire events (since all handlers are no-ops).
**How to avoid:** Keep the constructor signature identical. Simply don't wire events in the stub. Verify no other code relies on OverlayService having active event handlers.

### Pitfall 4: ImguiService D3D11 Init Called Without Guard
**What goes wrong:** `ImguiService::Create()` is called from RenderSystemD3D11::OnDeviceCreation. If RenderSystemD3D11 is excluded but ImguiService isn't, `Create()` never gets called. But if someone adds a different call path, it would crash trying to use D3D11 APIs.
**How to avoid:** ImguiService::Create() should only be callable when D3D11 is available. Guard the method body, not just the call site. Phase 6 will replace the D3D11 backend.

### Pitfall 5: `_initterm_e` Hook in Memory.cpp is MSVC CRT Specific
**What goes wrong:** Memory.cpp hooks `_initterm_e` (MSVC CRT initialization function) to detect EngineFixes DLL. Under MinGW, `_initterm_e` may have different behavior or not be hookable via IAT since MinGW uses its own CRT initialization.
**How to avoid:** Guard the `_initterm_e` hook and `HookFormAllocateSentinelInit()` behind platform detection. The EngineFixes compatibility code is Windows/MSVC-specific and irrelevant for MinGW builds (EngineFixes is an SKSE plugin that loads via MSVC).

## Code Examples

### XMake Conditional Package Addition
```lua
-- Code/client/xmake.lua
-- Core packages (always needed)
add_packages("spdlog", "hopscotch-map", "cryptopp", "enet6",
             "minhook", "entt", "glm", "mem", "xbyak",
             "rpmalloc")  -- replaces mimalloc

-- Feature-gated packages
if not is_plat("mingw") then
    add_defines("HAS_CEF=1", "HAS_DISCORD=1", "HAS_DIRECTXTK=1")
    add_packages("discord", "cef")
    add_deps("SkyrimCoopUIProcess", "SkyrimCoopUI")
end

-- ImGui always included (Phase 6 readiness)
add_packages("imgui")
```

### DiscordService No-Op Stub
```cpp
// Services/DiscordService.h
#pragma once

#ifdef HAS_DISCORD
#include <discord.h>
// ... full implementation
#else
struct DiscordService
{
    DiscordService(entt::dispatcher&) {}
    ~DiscordService() = default;
    bool Init() { return false; }
    void Update() {}
    void WndProcHandler(HWND, UINT, WPARAM, LPARAM) {}
};
#endif
```

### rpmalloc Migration in Memory.cpp
```cpp
// Replace mimalloc includes
// OLD: #include <TiltedCore/MimallocAllocator.hpp>
// OLD: #include <mimalloc.h>
#include <TiltedCore/RpmallocAllocator.hpp>
#include <rpmalloc.h>

// Replace allocator
// OLD: static TiltedPhoques::MimallocAllocator s_allocator;
static TiltedPhoques::RpmallocAllocator s_allocator;

// Replace function implementations
size_t Hook_msize(void* apData)
{
    return rpmalloc_usable_size(apData);
}

void Hookfree(void* apData)
{
    rpfree(apData);
}

void* Hookcalloc(size_t aCount, size_t aSize)
{
    return rpcalloc(aCount, aSize);
}

void* Hookmalloc(size_t aSize)
{
    return rpmalloc(aSize);
}

void* Hook_aligned_malloc(size_t aSize, size_t aAlignment)
{
    return rpmemalign(aAlignment, aSize);  // Note: arg order swapped from mi_malloc_aligned
}

void Hook_aligned_free(void* apData)
{
    rpfree(apData);
}
```

### Smoke Test Script (Shell)
```bash
#!/bin/bash
# smoke_test_client.sh -- Verify MinGW client DLL loads and connects
set -euo pipefail

DLL_PATH="build/mingw/x86_64/releasedbg/SkyrimTogetherClient.dll"
SERVER_BIN="build/linux/x64/release/SkyrimTogetherServer"
SKYRIM_DIR="${SKYRIM_DIR:-$HOME/.steam/steam/steamapps/common/Skyrim Special Edition}"
LOG_FILE="/tmp/skyrimcoop_smoke.log"

# 1. Start local server
$SERVER_BIN &
SERVER_PID=$!
sleep 2

# 2. Deploy DLL to SKSE plugins
cp "$DLL_PATH" "$SKYRIM_DIR/Data/SKSE/Plugins/SkyrimTogetherClient.dll"

# 3. Launch Skyrim under Proton (headless where possible)
# Capture log output for connection verification
PROTON_LOG=1 steam -applaunch 489830 &
SKYRIM_PID=$!

# 4. Wait for connection log entry (timeout 60s)
TIMEOUT=60
CONNECTED=false
for i in $(seq 1 $TIMEOUT); do
    if grep -q "Connected to server" "$LOG_FILE" 2>/dev/null; then
        CONNECTED=true
        break
    fi
    sleep 1
done

# 5. Cleanup
kill $SKYRIM_PID 2>/dev/null || true
kill $SERVER_PID 2>/dev/null || true

# 6. Report
if $CONNECTED; then
    echo "PASS: Client DLL loaded and connected to server"
    exit 0
else
    echo "FAIL: Client did not connect within ${TIMEOUT}s"
    exit 1
fi
```

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | `Code/tests/xmake.lua` |
| Quick run command | `xmake build TPTests && xmake run TPTests` |
| Full suite command | `xmake build TPTests && xmake run TPTests` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| BUILD-04 | Client DLL compiles under MinGW | build | `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient` | N/A (build verification) |
| BUILD-05 | DLL loads into Skyrim SE under Proton | smoke | `./tests/smoke_test_client.sh` | Wave 0 |

### Sampling Rate
- **Per task commit:** `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient` (compilation check)
- **Per wave merge:** Full compilation + smoke test
- **Phase gate:** DLL compiles clean + loads in Skyrim + connects to server

### Wave 0 Gaps
- [ ] `tests/smoke_test_client.sh` -- smoke test script for DLL load + connect verification
- [ ] Build verification: `xmake build SkyrimTogetherClient` under MinGW must succeed

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| MinGW GCC | Cross-compilation | Yes | 10-win32 20220113 | -- |
| XMake | Build system | Yes | v3.0.8 | -- |
| Wine | Smoke testing | Yes | 6.0.3 | -- |
| Skyrim SE | DLL load testing | Manual check needed | -- | Manual test only |
| rpmalloc | Memory allocator | Via xmake package | -- | -- |

**Note:** MinGW GCC 10 is older (2020). C++20 support is mostly complete but some features (`std::format`, `<concepts>` library) may have gaps. The codebase uses `fmt::format` via spdlog rather than `std::format`, so this should not be an issue. Verify during compilation.

## Open Questions

1. **ImGui vendored build under MinGW**
   - What we know: ImGui is vendored in `Code/external/imgui/` with D3D11 backends
   - What's unclear: Whether the vendored ImGui xmake.lua compiles under MinGW, and whether the D3D11 backend files should be excluded now or in Phase 6
   - Recommendation: Compile ImGui core library only (no D3D11 backend) under MinGW. Phase 6 will add the appropriate backend.

2. **InputService under MinGW**
   - What we know: InputService is deeply coupled to CEF types and OverlayApp
   - What's unclear: Whether any input processing is needed for Phase 3 (Load + Connect only, no user interaction via the mod UI)
   - Recommendation: Exclude InputService from MinGW build entirely. Phase 6 will rewrite it for ImGui.

3. **Renderer.cpp hooks under MinGW**
   - What we know: `Games/Renderer.cpp` hooks Skyrim's viewport creation and present calls, routing them to D3D11Hook
   - What's unclear: Whether the Renderer hooks are needed for basic game functionality (they set the window title to "Skyrim Together")
   - Recommendation: Guard the D3D11 hook setup. The viewport hook can optionally stay (it just modifies a string) but the D3D11 present hook should be excluded.

## Sources

### Primary (HIGH confidence)
- Direct codebase analysis: grep/read of all client source files for CEF, Discord, D3D11 dependencies
- `Code/client/xmake.lua` -- current build configuration
- `Code/libraries/xmake.lua` -- library target gating patterns
- `Code/components/crash_handler/CrashHandler.cpp` -- HAS_SENTRY guard pattern (established precedent)
- Phase 1 feasibility tests (`Code/tests/feasibility/`) -- MinHook validation under MinGW
- Phase 2 CONTEXT.md -- MSVC-drop and rpmalloc decisions

### Secondary (MEDIUM confidence)
- `.planning/research/PITFALLS.md` -- MinHook/Xbyak cross-compilation risks, MSVC extension inventory
- rpmalloc API documentation (training data)

### Tertiary (LOW confidence)
- MinGW GCC 10 C++20 feature completeness (verify during compilation)
- Wine 6.0.3 SKSE plugin loading behavior (verify during smoke test)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- all packages identified, versions pinned in xmake.lua
- Architecture: HIGH -- complete dependency map built from direct source analysis of every affected file
- Pitfalls: HIGH -- CEF leakage through PartyService/InputService confirmed by grep, rpmalloc threading documented

**Research date:** 2026-03-27
**Valid until:** 2026-04-27 (stable codebase, no upstream changes expected)
