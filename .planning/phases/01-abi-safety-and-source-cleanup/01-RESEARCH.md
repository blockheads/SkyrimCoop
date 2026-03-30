# Phase 1: ABI Safety and Source Cleanup - Research

**Researched:** 2026-03-30
**Domain:** C++ ABI compatibility (MinGW/MSVC), try/catch removal, build system cleanup
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Test `-flayout-compat=ms` empirically. If supported, add to xmake.lua and re-enable `SKYRIM_STRUCT_ASSERT`. **(RESOLVED — see findings below)**
- **D-02:** No CI/build-time check for GCC version compatibility — let it error naturally.
- **D-03:** Replace all `__declspec(align(N))` in game headers with `alignas(N)`. Known locations: `Code/client/Games/Skyrim/SaveLoad.h` (lines 145, 165), `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` (line 7). Leave `Code/external/DirectXTK/` untouched.
- **D-04:** Replace ALL try/catch sites in `Code/client/` — consistent no-exceptions policy.
- **D-05:** Error-code replacement style — match surrounding code patterns at each site.
- **D-06:** Delete `Code/relay_dll/` entirely — remove directory and all xmake references. Git history preserves the code.
- **D-07:** Keep `Code/native_client/` in the source tree as dead code — remove from xmake build system only. Phase 3 will reference transport code. Defer full deletion to Phase 3.

### Claude's Discretion

- Error-code replacement pattern (single bool chain vs individual checks) — match whatever pattern is established in surrounding code at each site.

### Deferred Ideas (OUT OF SCOPE)

- Delete `Code/native_client/` directory — deferred to Phase 3 after transport code ported to LoopbackTransport.
</user_constraints>

---

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BUILD-02 | ABI safety fixes applied — `__declspec(align)` replaced with `alignas()`, try/catch UB removed from HostService, struct layout correctness verified with MinGW | Fully mapped — see Standard Stack and Architecture Patterns sections |
| BUILD-03 | Code/relay_dll/ and Code/native_client/ deleted from source tree and build system | Partially mapped — D-06 deletes relay_dll/, D-07 only removes native_client from build system (directory kept); tests referencing relay_dll must also be deleted/updated |
</phase_requirements>

---

## Summary

Phase 1 is pure mechanical cleanup: no new code, no architecture changes. Three work streams run independently: (1) resolve the `-flayout-compat=ms` ABI flag status, (2) replace `__declspec(align)` with `alignas()` in three game headers, (3) eliminate try/catch from two sites in client code, (4) delete `Code/relay_dll/` and all its build references.

**Critical empirical finding:** `-flayout-compat=ms` is NOT supported by the installed MinGW GCC 10 (`x86_64-w64-mingw32-g++ (GCC) 10-posix 20220113`). The flag was introduced in GCC 12. Per D-01 the prescribed action when the flag is absent is: delete the stale TODO comment in `TiltedOnlinePCH.h` (line 25) and document the finding. The `SKYRIM_STRUCT_ASSERT` suppression macro stays as-is — it is correct given this toolchain.

**try/catch audit finding:** The CONTEXT.md lists `TESObjectREFR.cpp` as a try/catch site but this is incorrect. That file has zero `try`/`catch` keywords. Only two real sites exist: `HostService.cpp` (lines 33-68) and `CrashHandler.cpp` (lines 52-93).

**relay_dll deletion scope finding:** `Code/relay_dll/` is referenced not only in `Code/xmake.lua` and the relay_dll's own `xmake.lua`, but also in three test build files (`tests/xmake.lua`, `tests/replay/xmake.lua`, `tests/integration/xmake.lua`). Five test source files directly `#include` relay_dll headers (`relay_protocol_tests.cpp`, `relay_integration_tests.cpp`, `command_queue_tests.cpp`, `line_buffer_tests.cpp`, `dirty_cache_tests.cpp`, `json_protocol_tests.cpp`, and `integration/mock_skyrim_loader.cpp`). These test files test relay_dll functionality exclusively and must be deleted when relay_dll is deleted.

**Primary recommendation:** Execute the four work streams in order — flag test → alignas replacement → try/catch removal → relay_dll deletion with cascading test file cleanup — then run `xmake f -p mingw` to confirm clean configuration.

---

## Standard Stack

### Core
| Library/Tool | Version | Purpose | Why Standard |
|---|---|---|---|
| MinGW-w64 GCC | 10-posix (installed) | Cross-compile Windows DLL from Linux | Locked toolchain per CLAUDE.md |
| XMake | 2.8.5+ | Build orchestration | Locked build system per CLAUDE.md |
| C++20 | standard | Language version | Locked per CLAUDE.md |

### ABI Compatibility Layer (Already Established)
| Mechanism | Location | What It Does |
|---|---|---|
| `-mms-bitfields` | `native_client/xmake.lua`, `tests/replay/xmake.lua` | Forces MSVC bitfield layout in GCC |
| `MinGWCompat.h` | `Code/client/` | Calling convention stubs (`__fastcall`, `__stdcall` no-ops on x64) |
| `SKYRIM_STRUCT_ASSERT` macro | `TiltedOnlinePCH.h` lines 26-30 | Guards static_asserts to MSVC only — stays correct with GCC 10 |

**Note:** `-flayout-compat=ms` would supplement `-mms-bitfields` for tail-padding reuse but requires GCC 12+. With GCC 10, the existing approach (suppress struct asserts + `-mms-bitfields`) is the best available.

---

## Architecture Patterns

### -flayout-compat=ms: Empirical Result

**Finding:** Flag NOT supported on installed toolchain (GCC 10). Command tested:
```
x86_64-w64-mingw32-g++ -flayout-compat=ms -x c++ /dev/null -fsyntax-only
# Output: error: unrecognized command-line option '-flayout-compat=ms'
```

**Action per D-01:** Delete the TODO comment at `TiltedOnlinePCH.h` line 25:
```
// TODO: Upgrade to GCC 12+ and use -flayout-compat=ms for proper MSVC ABI compat.
```
Replace with a factual note documenting the finding:
```cpp
// NOTE: -flayout-compat=ms (GCC 12+) is not available on the installed toolchain
// (GCC 10). SKYRIM_STRUCT_ASSERT is suppressed on non-MSVC builds. The -mms-bitfields
// flag handles bitfield layout. Tail-padding reuse differences are accepted for now.
```
The `SKYRIM_STRUCT_ASSERT` suppression block (lines 26-30) is UNCHANGED — it remains correct.

### alignas() Replacement Pattern

**Standard C++11 syntax for struct alignment:**
```cpp
// Before (MSVC-only)
struct __declspec(align(8)) BGSSaveLoadScrapBuffer { ... };

// After (standard C++11, supported by all compilers including MinGW)
struct alignas(8) BGSSaveLoadScrapBuffer { ... };
```

**Exact locations:**
- `Code/client/Games/Skyrim/SaveLoad.h` line 145: `struct __declspec(align(8)) BGSSaveLoadScrapBuffer`
- `Code/client/Games/Skyrim/SaveLoad.h` line 165: `struct __declspec(align(8)) BGSLoadFormData`
- `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` line 7: `struct __declspec(align(8)) BSCriticalSection`

**Confidence:** HIGH — `alignas(N)` is standard C++11, supported by GCC, Clang, and MSVC.

### try/catch Removal Pattern

**Site 1: HostService.cpp (lines 33-68)**

Current code wraps `std::make_unique<>` calls and server initialization in `try { ... } catch (const std::exception& e)`. The `make_unique` calls cannot fail without throwing, which is UB in a `noexcept` context. Replacement follows D-05 (match surrounding code) and the CONTEXT.md success criterion ("if (!pServer->IsListening()) return false;"):

```cpp
bool HostService::StartHosting(uint16_t aPort, uint8_t aMaxPlayers) noexcept
{
    if (m_isHosting)
    {
        spdlog::warn("[HostService] Already hosting a session");
        return true;
    }

    spdlog::info("[HostService] Starting P2P host session on port {} for up to {} players", aPort, aMaxPlayers);

    if (!spdlog::get("EmbeddedServer"))
    {
        auto embeddedLogger = spdlog::stdout_color_mt("EmbeddedServer");
        embeddedLogger->set_pattern("[EmbeddedServer] [%l] %v");
    }

    m_pConsole = std::make_unique<ServerConsole::ConsoleRegistry>("EmbeddedServer");
    m_pGameServer = std::make_unique<Server::GameServer>(*m_pConsole);
    m_pGameServer->Initialize();

    if (!m_pGameServer->IsListening())
    {
        spdlog::error("[HostService] Server failed to start listening on requested port");
        m_pGameServer.reset();
        m_pConsole.reset();
        return false;
    }

    m_isHosting = true;
    spdlog::info("[HostService] Successfully started hosting session on port {}", m_pGameServer->GetPort());
    // ...
    return true;
}
```

**Site 2: CrashHandler.cpp (lines 52-93)**

The `try { ... } catch (...) // Mini-dump is best effort only.` block wraps Win32 C APIs (`CreateFileA`, `MiniDumpWriteDump`) that do not throw C++ exceptions. The `catch` is dead code. The body is sequential and already handles failure via the existing `if (!hDumpFile)` check at line 95.

Replacement: remove the `try {` opener and `} catch (...) { }` closer entirely. Keep all the code between them exactly as-is. The `if (!hDumpFile)` guard at line 95 already handles the `CreateFileA` failure case.

**Site 3: TESObjectREFR.cpp — PHANTOM, DO NOT TASK**

CONTEXT.md listed this as a try/catch site but it has no try/catch. Verified by grep: zero matches in the file for `try` or `catch` as keywords. No work needed here.

### relay_dll Deletion: Complete Scope

**Files to delete:**
- `Code/relay_dll/` — entire directory (14 files including `xmake.lua`)

**xmake.lua references to remove:**

1. `Code/xmake.lua` line 36: `includes("relay_dll")` — delete this line
2. `Code/tests/xmake.lua` lines 7, 14:
   - Remove `"../relay_dll"` from the `add_includedirs(...)` list
   - Remove `add_files("../relay_dll/command_queue.cpp")` line
3. `Code/tests/replay/xmake.lua` line 36: `add_includedirs("../../relay_dll")` — delete this line
4. `Code/tests/integration/xmake.lua` line 23: `"../../relay_dll"` in includedirs — delete this line
5. `Code/tests/integration/xmake.lua` line 43: `add_includedirs("../../relay_dll")` — delete this line

**Test source files to delete** (test relay_dll functionality exclusively):
- `Code/tests/relay_protocol_tests.cpp` — tests `protocol.h`
- `Code/tests/relay_integration_tests.cpp` — tests `json_protocol.h`
- `Code/tests/command_queue_tests.cpp` — tests `command_queue.h`
- `Code/tests/line_buffer_tests.cpp` — tests `line_buffer.h`
- `Code/tests/dirty_cache_tests.cpp` — tests `dirty_cache.h`
- `Code/tests/json_protocol_tests.cpp` — tests `json_protocol.h`
- `Code/tests/integration/mock_skyrim_loader.cpp` — tests relay TCP protocol

**native_client references:** `native_client` source files also `#include "json_protocol.h"` via the `../relay_dll` include path (used in `dispatch_hook_event.cpp`, all Services/*.cpp, `game_bridge/tcp_client.cpp`). Since D-07 removes `native_client` from the build system but keeps the source tree, these includes will become broken dead code — that is acceptable since `native_client` no longer compiles. The planner should note this but need not fix it (Phase 3 will rewrite that transport layer).

### native_client Build System Removal

Per D-07: remove `Code/native_client/` from the xmake build system only. Action:

1. `Code/xmake.lua` line 38: `includes("native_client")` — delete this line
2. The `Code/native_client/xmake.lua` file itself is NOT deleted (kept as dead code reference)

After this change, `SkyrimCoopNative` target will no longer be built by `xmake`. Source files remain on disk.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Alignment specification | Custom `__declspec(align)` or macros | `alignas(N)` standard keyword | Compiler-portable, no macro layer needed |
| Error propagation without exceptions | Complex result-wrapper types | Boolean returns + early return pattern | Already established in codebase (`noexcept` + bool return convention) |

---

## Common Pitfalls

### Pitfall 1: Phantom try/catch site in TESObjectREFR.cpp
**What goes wrong:** Planner creates a task to fix try/catch in TESObjectREFR.cpp, implementer searches and finds nothing.
**Why it happens:** CONTEXT.md listed it based on earlier analysis that was incorrect.
**How to avoid:** Do not create a task for TESObjectREFR.cpp. Only two real sites exist: `HostService.cpp` and `CrashHandler.cpp`.

### Pitfall 2: Incomplete relay_dll reference sweep
**What goes wrong:** `Code/xmake.lua` is cleaned, but `tests/xmake.lua`, `tests/replay/xmake.lua`, and `tests/integration/xmake.lua` still reference `relay_dll` as an include path or source file. Build fails after relay_dll/ is deleted.
**Why it happens:** Indirect references in test build files are easy to miss.
**How to avoid:** The test xmake references are documented above. The test source files that exclusively test relay_dll code must also be deleted.

### Pitfall 3: Forgetting to delete relay_dll test source files
**What goes wrong:** relay_dll/ is deleted but `relay_protocol_tests.cpp` etc. remain. They include `protocol.h` which no longer exists. `TPTests` build fails.
**Why it happens:** Test files are separate from the library they test.
**How to avoid:** Delete the 7 relay-specific test files listed in the Architecture Patterns section along with the relay_dll directory.

### Pitfall 4: alignas() placement in struct declaration
**What goes wrong:** `alignas(N)` placed after `struct` keyword in wrong position.
**Warning signs:** Compiler error on the struct declaration line.
**How to avoid:** Correct syntax is `struct alignas(N) Name { }` — `alignas` goes between `struct` and the name.

### Pitfall 5: -flayout-compat=ms "add conditionally" approach
**What goes wrong:** Implementer adds `-flayout-compat=ms` inside an `if GCC >= 12` check "for future proofing."
**Why it happens:** The TODO comment implied this was desirable.
**How to avoid:** Per D-02, no conditional version checks. Delete the TODO, document the empirical result, leave the flag out entirely. If the toolchain upgrades to GCC 12, that is a separate task.

---

## Code Examples

### alignas() replacement (verified C++11 standard)
```cpp
// SaveLoad.h line 145 — BEFORE
struct __declspec(align(8)) BGSSaveLoadScrapBuffer
{
    char* pBuffer;
    struct ScrapHeap* pScrapHeap;
    uint32_t uiSize;
};

// AFTER
struct alignas(8) BGSSaveLoadScrapBuffer
{
    char* pBuffer;
    struct ScrapHeap* pScrapHeap;
    uint32_t uiSize;
};
```

### try/catch removal in CrashHandler.cpp (before/after)
```cpp
// BEFORE (lines 52-93)
try
{
    // ... CreateFileA, MiniDumpWriteDump, etc.
}
catch (...) // Mini-dump is best effort only.
{
}

if (!hDumpFile)
    spdlog::critical(...)

// AFTER: remove try { } and catch (...) { } wrappers, keep body as-is
// ... CreateFileA, MiniDumpWriteDump, etc.

if (!hDumpFile)
    spdlog::critical(...)
```

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|---|---|---|---|---|
| x86_64-w64-mingw32-g++ | All compilation | Yes | GCC 10-posix 20220113 | — |
| xmake | Build system | Yes | detected via `xmake f -p mingw` success | — |

**xmake f -p mingw baseline:** Currently passes cleanly. Success criterion 5 ("`xmake f -p mingw` completes without errors after deletions") means maintaining this passing state through the cleanup, not achieving it for the first time.

**-flayout-compat=ms availability:** NOT available on GCC 10. Requires GCC 12+. This is a confirmed constraint, not a blocker — it changes the action for D-01 from "add flag" to "delete TODO comment."

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | none (headers-only) |
| Quick run command | `xmake build TPTests && ./build/linux/x86_64/release/tests/TPTests` |
| Full suite command | same as quick (all tests in one binary) |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| BUILD-02 | `alignas(N)` compiles without warnings | build smoke | `xmake f -p mingw && xmake -p mingw -t SkyrimTogetherClient` | N/A — build verification |
| BUILD-02 | try/catch removed — `noexcept` functions compile | build smoke | same | N/A — build verification |
| BUILD-03 | relay_dll removed from build, xmake f -p mingw clean | build smoke | `xmake f -p mingw` | N/A — build verification |

**Note:** Phase 1 has no unit-testable behavior — it is pure cleanup verified by build success. All validation is compile-time: `xmake f -p mingw` must pass, and `xmake -p mingw` must compile the client target without errors related to the changed files.

### Sampling Rate
- **Per task commit:** `xmake f -p mingw` (configuration check)
- **Per wave merge:** `xmake f -p mingw` (full config verification)
- **Phase gate:** `xmake f -p mingw` passes before `/gsd:verify-work`

### Wave 0 Gaps
None — no test files need to be created. Build verification commands exist.

---

## Open Questions

1. **Should relay-specific test source files be deleted or just excluded from compilation?**
   - What we know: They will fail to compile once `relay_dll/` is deleted (missing includes)
   - What's unclear: Whether they contain any reusable test infrastructure (main.cpp, mock_client.cpp, etc.)
   - Recommendation: Delete the 7 relay-specific test files listed above. `main.cpp`, `mock_client.cpp`, `encoding.cpp`, `loopback_transport_tests.cpp`, `mock_client_tests.cpp`, `pointer_table_tests.cpp`, `proc_memory_tests.cpp`, `router_test.cpp`, `server_roundtrip_tests.cpp` are NOT relay-specific and should be kept.

2. **Does `tests/integration/fake_player` target need cleanup?**
   - What we know: `tests/integration/xmake.lua` has two targets: `fake_player` and `mock_skyrim_loader`. Only `mock_skyrim_loader` includes relay_dll headers. `fake_player` includes `../../native_client/game_bridge` only.
   - Recommendation: Delete `mock_skyrim_loader.cpp` and remove its target block from `tests/integration/xmake.lua`. Keep `fake_player`.

---

## Sources

### Primary (HIGH confidence)
- Empirical test: `x86_64-w64-mingw32-g++ -flayout-compat=ms -x c++ /dev/null -fsyntax-only` — flag not supported on GCC 10
- Direct source reading: `Code/client/TiltedOnlinePCH.h`, `Code/client/Games/Skyrim/SaveLoad.h`, `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.h`, `Code/client/Services/HostService.cpp`, `Code/client/CrashHandler.cpp`
- Direct source reading: `Code/xmake.lua`, `Code/tests/xmake.lua`, `Code/tests/replay/xmake.lua`, `Code/tests/integration/xmake.lua`, `Code/relay_dll/xmake.lua`, `Code/native_client/xmake.lua`
- Grep audit: all try/catch sites in `Code/client/` confirmed
- Grep audit: all relay_dll header consumers confirmed

### Secondary (MEDIUM confidence)
- C++ standard: `alignas(N)` is ISO C++11 — supported by GCC, Clang, MSVC
- GCC documentation: `-flayout-compat=ms` introduced in GCC 12 (training knowledge, consistent with empirical failure on GCC 10)

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — empirically verified toolchain versions
- Architecture: HIGH — derived from direct code reading and empirical flag test
- Pitfalls: HIGH — all derived from actual code inspection, not speculation

**Research date:** 2026-03-30
**Valid until:** 2026-04-30 (stable cleanup domain, no fast-moving dependencies)
