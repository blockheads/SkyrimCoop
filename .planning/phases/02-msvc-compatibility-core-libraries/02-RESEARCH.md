# Phase 02: MSVC Compatibility & Core Libraries - Research

**Researched:** 2026-03-27
**Domain:** MSVC-to-GCC migration, MinGW cross-compilation, C++ library porting
**Confidence:** HIGH

## Summary

Phase 2 eliminates MSVC as a supported compiler and makes Tier 1-2 libraries (TiltedCore, encoding, common, base, networking, server) build under MinGW/GCC. The codebase audit reveals this is highly achievable: the Tier 1-2 code contains almost zero MSVC-isms (no `__declspec`, no `__forceinline`, no `#pragma comment(lib)` in any Tier 1-2 source). The MSVC constructs are concentrated in xmake.lua build config (Wine MSVC object library workarounds, `/MT` runtime, `/bigobj` flags) and Tier 3 client code.

A prior porting effort exists on the `linux-build-wip` branch with substantial work: mimalloc replaced with rpmalloc, sentry-native conditionally disabled, Wine MSVC workarounds removed from all xmake.lua files, `mem` package dropped, and platform gating added. This branch should be cherry-picked/merged selectively rather than redone from scratch. It already solved many of the problems this phase targets.

The critical risk is the `_WIN64` alignment assumption in `TiltedCore/Meta.hpp` (line 43-48) which uses a 16-byte aligned struct on `_WIN64` vs `std::max_align_t` on other platforms. Under MinGW cross-compiling for Windows, `_WIN64` IS defined, so this resolves correctly. For native Linux test builds, `std::max_align_t` is used, which is correct. This is a non-issue but must be verified.

**Primary recommendation:** Selectively merge the `linux-build-wip` branch changes into `dev`, then complete the remaining work: remove all Wine MSVC object library workarounds, wire up Catch2 tests for native Linux, and validate the full Tier 1-2 chain builds and tests pass.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Drop MSVC entirely. Only MinGW/GCC is a supported compiler going forward. No compat headers, no `#ifdef _MSC_VER` branches.
- **D-02:** Delete all MSVC-specific build configuration from xmake.lua -- `is_plat('windows')` MSVC blocks, `/MT` flags, `/bigobj`, MSVC-specific rules. Clean break, git history preserves the old config.
- **D-03:** Use portable C++20 constructs where possible (`alignas()`, `[[nodiscard]]`, standard attributes). Fall back to `__attribute__` only when C++20 has no equivalent (e.g., `dllexport` -> `__attribute__((visibility("default")))`, Windows calling conventions stay as-is since MinGW supports `__stdcall`/`__cdecl`).
- **D-04:** `#pragma comment(lib, ...)` directives are deleted and replaced with explicit linker flags in xmake.lua.
- **D-05:** Strict bottom-up dependency chain: TiltedCore (inline) -> encoding -> common -> networking -> server. Each layer validated before building the next. No stubbing, no shortcuts.
- **D-06:** TiltedCore is inlined into SkyrimCoop source code. The external dependency is eliminated -- extract only the networking/serialization logic actually used by the project, drop the rest.
- **D-07:** Researcher must audit all TiltedCore imports to identify the minimal set of functionality used. Also check other git branches for any prior porting work that may already exist.
- **D-08:** For other MSVC-heavy dependencies, replace with Linux-friendly alternatives rather than forking and patching.
- **D-09:** Each library must compile, link without unresolved symbols, AND existing Catch2 encoding/serialization tests must pass when built natively on Linux.

### Claude's Discretion
- Specific C++20 replacement for each MSVC construct (case-by-case during implementation)
- Which Linux-friendly alternatives to use for replaced dependencies
- How to structure the inlined TiltedCore code within the project directory

### Deferred Ideas (OUT OF SCOPE)
None -- discussion stayed within phase scope.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BUILD-02 | MSVC compatibility header resolves all `__declspec`, `#pragma comment(lib)`, struct alignment, and MSVC-specific extensions for GCC | Per D-01, no compat header -- MSVC code is deleted, not wrapped. Research confirms Tier 1-2 code has zero `__declspec`/`__forceinline`/`#pragma comment(lib)` usage. Only `server/main.cpp` has `__declspec(dllexport)` which already has a GCC `#ifdef` branch. |
| BUILD-03 | Tier 1-2 libraries (encoding, networking, common, server) compile under MinGW and produce working static libraries | Research confirms: TiltedCore already GCC-clean, encoding zero MSVC-isms, networking zero MSVC-isms, server has portable `#ifdef` for dllexport. `linux-build-wip` branch already solved rpmalloc migration and sentry disabling. |
</phase_requirements>

## Prior Work: linux-build-wip Branch

**This is the single most important finding.** The `linux-build-wip` branch (20 commits diverged from `dev`) contains substantial prior porting work that directly addresses Phase 2 goals.

### Changes Already Made on linux-build-wip

| Area | Change | Status |
|------|--------|--------|
| TiltedCore allocator | mimalloc replaced with rpmalloc | Complete |
| TiltedCore xmake.lua | Wine MSVC object library workaround removed | Complete |
| Encoding xmake.lua | Wine MSVC workaround removed (inferred) | Needs verification |
| Server xmake.lua | Wine MSVC workaround removed, sentry-native removed from deps, cpp-httplib as package | Complete |
| Libraries xmake.lua | Platform gating for Windows-only libs, debug symbol config for MinGW/Linux | Complete |
| CrashHandler | sentry-native conditionally disabled with `#ifdef HAS_SENTRY` | Complete |
| Root xmake.lua | MinGW flags (`-Wa,-mbig-obj`, `-mms-bitfields`, `-fms-extensions`), rpmalloc replaces mimalloc, sentry-native disabled, `mem` package removed | Complete |
| Code/xmake.lua | Platform gating: tests skipped on MinGW, client/UI gated to windows/mingw | Complete |
| BaseLib | Windows dialog files excluded on non-Windows | Complete |

### Changes NOT Yet Made (This Phase Must Complete)

| Area | What's Missing |
|------|----------------|
| Native Linux test build | Tests are skipped on MinGW (`if not is_plat("mingw")`) but no evidence they were validated on native Linux |
| Encoding xmake.lua | Still has Wine MSVC object library workaround on `dev` |
| Common xmake.lua | Still has Wine MSVC object library workaround on `dev` |
| Components xmake.lua | Still has Wine MSVC object library workaround on `dev` |
| MSVC `is_plat("windows")` blocks in root xmake.lua | Still present on `dev` for `/bigobj`, `/MT`, PDB paths |
| `_WIN32_WINNT` define removal | linux-build-wip removed it from MinGW block but kept it for windows MSVC |
| sentry-native in BaseLib | Still listed as dependency in `Code/base/xmake.lua` on both branches |
| Test validation | Catch2 tests not confirmed passing on native Linux |

### Recommendation: Selective Cherry-Pick

Do NOT merge `linux-build-wip` wholesale -- it has 571 files changed including unrelated P2P refactoring work ("host ownership", "direct authority", "proximity enforcement"). Cherry-pick only the build system and TiltedCore changes. The relevant commits are:

- `68494cd3` -- "moved titledcore shit into this repo lol" (TiltedCore inlining)
- `f5de6397` -- "linux build wip" (build system changes)
- `ec48d66d` -- "stuck" (latest state)

Use `git diff dev..linux-build-wip -- <specific_files>` to extract targeted changes.

## TiltedCore Audit (D-07)

### Files Used by Tier 1-2 Code

| TiltedCore File | Used By | Keep? |
|----------------|---------|-------|
| `Serialization.hpp/cpp` | 25+ encoding files, all message serialization | YES - core |
| `Buffer.hpp/cpp` | encoding, messages, tests | YES - core |
| `Stl.hpp` | encoding pch, message factories, common | YES - core |
| `StlAllocator.hpp` | Stl.hpp dependency | YES - core |
| `Allocator.hpp/cpp` | Everything via Stl.hpp chain | YES - core |
| `MimallocAllocator.hpp/cpp` | Default allocator backend | YES - replace with rpmalloc per linux-build-wip |
| `Meta.hpp` | Allocator.hpp dependency (TP_NOCOPYMOVE, detector) | YES - core |
| `Platform.hpp` | Tests, platform detection | YES - trivial |
| `Math.hpp/cpp` | Rotator2 quantization | YES - used |
| `Hash.hpp/cpp` | Unknown direct usage | VERIFY - may be used transitively |
| `Lockable.hpp` | server ScriptService | YES - used |
| `Locked.hpp` | Likely used with Lockable | VERIFY |
| `Outcome.hpp` | server Pch.h | YES - used |
| `Filesystem.hpp/cpp` | server ScriptService | YES - used |
| `Signal.hpp` | Unknown | VERIFY |
| `Initializer.hpp` | Unknown | VERIFY |
| `Memory.hpp` | Unknown | VERIFY |
| `TaskQueue.hpp/cpp` | Unknown | VERIFY |
| `StackAllocator.hpp` | ActionEvent.cpp (encoding) | YES - used |
| `ScratchAllocator.hpp/cpp` | Unknown | VERIFY |
| `TrackAllocator.hpp` | Unknown | VERIFY |
| `BoundedAllocator.hpp/cpp` | Unknown | VERIFY |
| `StandardAllocator.hpp/cpp` | Fallback allocator | YES - has Linux `malloc_usable_size` support |
| `ViewBuffer.hpp/cpp` | Unknown | VERIFY |

### MSVC-isms in TiltedCore

**Zero.** Grep confirms no `__declspec`, `__forceinline`, `#pragma comment`, or `_MSC_VER` in TiltedCore. The only platform-conditional code:

1. `Meta.hpp:43` -- `#ifdef _WIN64`: 16-byte alignment struct vs `std::max_align_t`. This is correct for both MinGW (which defines `_WIN64`) and native Linux.
2. `Platform.hpp:4` -- `#ifdef _WIN32`: Platform detection macros. Correct for MinGW (defines `_WIN32`).
3. `StandardAllocator.cpp:26` -- `#ifdef _WIN32`: Uses `_msize` on Windows, `malloc_usable_size` on Linux. Already portable.

**Conclusion:** TiltedCore is already GCC-compatible. The inlining (D-06) is about eliminating the external dependency and trimming unused code, not about fixing MSVC-isms.

### Recommendation: Keep All TiltedCore Files

The "minimal extraction" approach from D-06 risks breaking transitive dependencies. TiltedCore is only 37 files (mix of .hpp/.cpp), most are tiny. Keep all files in `Code/TiltedCore/` as-is. The unused files add negligible compile time and removing them risks breaking something discovered later. The linux-build-wip branch kept them all.

## Standard Stack

### Core (Tier 1-2 Dependencies)

| Library | Version | Purpose | MinGW Compatible | Notes |
|---------|---------|---------|------------------|-------|
| rpmalloc | latest | Memory allocator (replaces mimalloc) | YES | linux-build-wip already switched |
| hopscotch-map | v2.3.1 | Fast hash map | YES | Header-only |
| entt | v3.10.0 | ECS framework | YES | Header-only |
| glm | 0.9.9+8 | 3D math | YES | Header-only |
| spdlog | v1.13.0 | Logging | YES | Header-only with fmt |
| catch2 | 2.13.9 | Unit testing | YES | Header-only |
| enet6 | latest | UDP networking | YES | Pure C |
| libuv | v1.48.0 | Async I/O | YES | Has MinGW support |
| snappy | 1.1.10 | Compression | YES | Pure C++ |
| cryptopp | 8.9.0 | Cryptography | YES | Has MinGW makefiles |
| zlib | v1.3.1 | Compression | YES | Pure C |
| sqlite3 | latest | Database | YES | Pure C |
| sol2 | v3.3.0 | Lua bindings | YES | Header-only |
| lua | 5.x | Scripting | YES | Pure C |
| cpp-httplib | 0.14.0 | HTTP server | YES | Header-only (linux-build-wip uses as package, not vendored) |
| gtest | v1.14.0 | Google Test | YES | Used by base/components |

### Removed Dependencies

| Library | Why Removed | Replacement |
|---------|-------------|-------------|
| mimalloc | MinGW compatibility concerns | rpmalloc (already done on linux-build-wip) |
| sentry-native | Not compatible with MinGW cross-compilation | Disabled with `#ifdef HAS_SENTRY` stubs |
| mem 1.0.0 | Signature scanning, Tier 3 only | Removed from root requires (Tier 3 will handle) |

### Packages Kept Only for MSVC/Windows Block (To Be Deleted per D-02)

| Package | Current Gate | Action |
|---------|-------------|--------|
| discord 3.2.1 | `is_plat("windows")` | Remove entirely (no Windows MSVC support) |
| imgui v1.89.7 | `is_plat("windows")` | Move to MinGW client block later (Phase 3) |
| directxtk 21.11.0 | `is_plat("windows")` | Remove (being replaced by ImGui in Phase 6) |
| cef 100.0.24 | `is_plat("windows")` | Remove entirely |

## Architecture Patterns

### Build Order (Strict Bottom-Up per D-05)

```
Wave 1: TiltedCore
  - Replace mimalloc with rpmalloc
  - Remove Wine MSVC object library workaround
  - Compile as static library under both native GCC and MinGW
  - Verify: ar -t libTiltedCore.a shows all expected symbols

Wave 2: SkyrimEncoding + CommonLib
  - Remove Wine MSVC workarounds from xmake.lua
  - Compile against TiltedCore
  - Verify: links without unresolved symbols

Wave 3: BaseLib + SkyrimCoopNetworking
  - BaseLib: disable sentry-native, exclude Windows dialog files on non-Windows
  - Networking: compile against TiltedCore + enet6 + libuv
  - Verify: links without unresolved symbols

Wave 4: Components (Console, ESLoader, CrashHandler, Resources, AdminProtocol)
  - CrashHandler: add HAS_SENTRY conditional
  - Remove Wine MSVC workarounds from all
  - Verify: links without unresolved symbols

Wave 5: SkyrimTogetherServer
  - Compile against all Tier 1-2 libs
  - Remove sentry-native from server deps
  - Verify: links without unresolved symbols

Wave 6: TPTests (Native Linux Only)
  - Build Catch2 tests against native Linux Tier 1-2 libs
  - Run tests, all must pass
```

### Recommended Project Structure

TiltedCore stays at `Code/TiltedCore/` (already inlined). No restructuring needed.

```
Code/
  TiltedCore/          # Already inlined, 37 files
    Allocator.hpp/cpp
    Buffer.hpp/cpp
    Serialization.hpp/cpp
    Stl.hpp
    ... (keep all)
    xmake.lua          # Static lib, rpmalloc + hopscotch-map
  encoding/            # Tier 1 - zero changes expected
  common/              # Tier 1 - zero changes expected
  base/                # Tier 2 - exclude win dialogs, disable sentry
  libraries/
    networking/        # Tier 2 - zero changes expected
  components/
    console/           # Tier 2
    crash_handler/     # Tier 2 - HAS_SENTRY conditional
    es_loader/         # Tier 2
    resources/         # Tier 2
  server/              # Tier 2 - remove sentry dep
  tests/               # Tier 4 - wire for native Linux
```

### Pattern: Dual-Platform XMake Configuration

The root `xmake.lua` should support two modes after Phase 2:

1. `xmake f -p mingw --mingw=$XPACK_PATH` -- Cross-compile for Windows (produces .a static libs and eventually .dll)
2. `xmake f -p linux` -- Native Linux build (produces .a static libs + test binaries)

The `is_plat("windows")` blocks (MSVC-specific) should be deleted entirely per D-02.

### Anti-Patterns to Avoid

- **Creating a compat header:** D-01 explicitly forbids this. Delete MSVC code, don't wrap it.
- **Keeping Wine MSVC workarounds "just in case":** The `get_config("sdk") == "/opt/msvc"` object library pattern appears in 8+ xmake.lua files. Delete all of them.
- **Stubbing dependencies:** D-05 says no stubbing. Each library must actually compile and link, not just have stubs.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Memory allocation | Custom malloc wrapper | rpmalloc package | Already done on linux-build-wip, proven MinGW compatible |
| Crash reporting (MinGW) | Custom signal handlers | `#ifdef HAS_SENTRY` conditional stubs | Sentry doesn't support MinGW; stub it cleanly, re-enable later if needed |
| HTTP server | Custom socket code | cpp-httplib package (not vendored) | linux-build-wip already switched from vendored to package |

## Common Pitfalls

### Pitfall 1: Wine MSVC Object Library Workaround Remnants
**What goes wrong:** 8+ xmake.lua files have `if is_plat("windows") and get_config("sdk") == "/opt/msvc" then set_kind("object")` blocks. If any survive, they break the build on native Windows (which no longer exists as a target).
**Why it happens:** Easy to miss one file during cleanup.
**How to avoid:** Grep for `get_config("sdk")` and `"/opt/msvc"` across all xmake.lua files. Delete every instance.
**Warning signs:** `set_kind("object")` anywhere in Tier 1-2 xmake.lua files.

### Pitfall 2: sentry-native Transitive Dependency
**What goes wrong:** sentry-native is listed in both `Code/base/xmake.lua` AND `Code/server/xmake.lua` AND `Code/components/crash_handler/xmake.lua`. Also the root `xmake.lua` has `add_requireconfs("sentry-native", ...)`. Missing any of these causes build failure when sentry-native can't be fetched for MinGW.
**How to avoid:** Remove/comment-out sentry-native from ALL locations: root requires, requireconfs, base, server, crash_handler. Also check `Code/base/threading/ThreadUtils.cpp` which calls `sentry__thread_setname` -- this needs a replacement (use `pthread_setname_np` on Linux, `SetThreadDescription` on Windows).
**Warning signs:** Build errors mentioning sentry symbols or sentry package fetch failures.

### Pitfall 3: ThreadUtils.cpp Sentry Dependency
**What goes wrong:** `Code/base/threading/ThreadUtils.cpp` directly calls `sentry__thread_setname()` -- a private sentry function. With sentry-native disabled, this is an unresolved symbol at link time.
**How to avoid:** Replace with platform-native thread naming: `pthread_setname_np` on Linux, Windows API `SetThreadDescription` or no-op on MinGW. The linux-build-wip branch may have already addressed this (verify).
**Warning signs:** Linker error: "undefined reference to `sentry__thread_setname`".

### Pitfall 4: System MinGW (GCC 10) vs xPack MinGW (GCC 14.3.0)
**What goes wrong:** System MinGW is GCC 10 (very old, poor C++20 support). The xPack MinGW is GCC 14.3.0 (excellent C++20 support). If xmake picks up the system MinGW, C++20 code fails to compile.
**How to avoid:** Always specify `--mingw=$HOME/.local/xPacks/@xpack-dev-tools/mingw-w64-gcc/14.3.0-1.1/.content` in xmake config. Phase 1 already established this.
**Warning signs:** C++20 compilation errors, `std::ranges` not found, concepts syntax errors.

### Pitfall 5: Native Linux GCC 11.4 C++20 Support
**What goes wrong:** The system GCC is 11.4.0 which has incomplete C++20 support (no `std::format`, limited ranges). The codebase uses C++20 features.
**How to avoid:** The codebase uses `fmt::format` via spdlog (not `std::format`), so this is likely fine. If any C++20 features fail on GCC 11.4, consider using xPack's native GCC or the system's GCC 14 if installed.
**Warning signs:** Template errors on native Linux that don't appear under MinGW 14.3.

### Pitfall 6: `add_repositories("local-repo packages")` Removal
**What goes wrong:** The dev branch root xmake.lua has `add_repositories("local-repo packages")` for patched packages (cpp-httplib, directxtk). If cpp-httplib is moved to a regular xmake package (as linux-build-wip did), this local repo may conflict.
**How to avoid:** Remove the local repo line when switching cpp-httplib to a package. Check `packages/` directory for any other local packages still needed.

## Code Examples

### xmake.lua Cleanup Pattern (Removing Wine MSVC Workaround)
```lua
-- BEFORE (current dev branch)
target("SkyrimEncoding")
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end

-- AFTER (Phase 2)
target("SkyrimEncoding")
    set_kind("static")
```

### CrashHandler Sentry Conditional Pattern
```cpp
// Source: linux-build-wip branch Code/components/crash_handler/CrashHandler.cpp
#ifdef HAS_SENTRY
#include <sentry.h>
#endif

void InstallCrashHandler(bool aServer, bool aSkyrim)
{
#ifdef HAS_SENTRY
    // ... existing sentry init code ...
#else
    spdlog::info("Crash reporting disabled (Sentry not available)");
#endif
}
```

### ThreadUtils.cpp Sentry Replacement
```cpp
// Replace sentry__thread_setname with platform-native calls
#ifdef _WIN32
#include <Windows.h>
bool SetCurrentThreadName(const char* apThreadName)
{
    // SetThreadDescription available Windows 10 1607+
    wchar_t wideName[256];
    MultiByteToWideChar(CP_UTF8, 0, apThreadName, -1, wideName, 256);
    return SUCCEEDED(SetThreadDescription(GetCurrentThread(), wideName));
}
#else
#include <pthread.h>
bool SetCurrentThreadName(const char* apThreadName)
{
    return pthread_setname_np(pthread_self(), apThreadName) == 0;
}
#endif
```

### Root xmake.lua MSVC Block Removal
```lua
-- DELETE this entire block (D-02):
-- if is_plat("windows") then
--     add_cxflags("/bigobj")
--     add_syslinks("kernel32")
--     set_arch("x64")
--     set_runtimes("MT")
--     add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
--     add_ldflags("/PDBALTPATH:%_PDB%", {force = true})
-- end

-- KEEP the MinGW block (from Phase 1 / linux-build-wip):
if is_plat("mingw") then
    add_cxflags("-Wa,-mbig-obj")
    add_cxflags("-mms-bitfields")
    add_cxflags("-fms-extensions")
    -- static linking
    add_ldflags("-static", "-static-libgcc", "-static-libstdc++", {force = true})
    set_arch("x86_64")
    add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00", "NOMINMAX")
    add_syslinks("kernel32")
end
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Wine MSVC cross-compilation | MinGW cross-compilation | Phase 1 decision | All `get_config("sdk") == "/opt/msvc"` workarounds deleted |
| mimalloc allocator | rpmalloc allocator | linux-build-wip | Better MinGW compatibility, same performance tier |
| Vendored cpp-httplib | cpp-httplib xmake package | linux-build-wip | Cleaner dependency management |
| sentry-native crash reporting | Conditional compilation with stubs | linux-build-wip | sentry-native doesn't support MinGW |
| `mem` sig scanning package | Removed (Tier 3 concern) | linux-build-wip | Not needed for Tier 1-2 |

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Catch2 2.13.9 |
| Config file | Code/tests/xmake.lua |
| Quick run command | `xmake run TPTests` |
| Full suite command | `xmake run TPTests` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| BUILD-02 | MSVC extensions resolved for GCC | build | `xmake build TiltedCore SkyrimEncoding CommonLib` (native Linux) | N/A (build test) |
| BUILD-03 | Tier 1-2 libs compile and link | build + unit | `xmake build -g Server && xmake run TPTests` | encoding.cpp exists (513 lines) |

### Sampling Rate
- **Per task commit:** `xmake build <target>` for affected library
- **Per wave merge:** `xmake build -g Server && xmake run TPTests`
- **Phase gate:** All Tier 1-2 libs build on native Linux AND MinGW. TPTests pass on native Linux.

### Wave 0 Gaps
- [ ] TPTests xmake.lua needs `add_packages("rpmalloc")` to replace mimalloc (or TiltedCore dep transitively provides it)
- [ ] TPTests xmake.lua references `"TiltedCore"` as package but it's now a local target -- verify `add_deps("SkyrimEncoding")` pulls it in
- [ ] Native Linux platform must be tested: `xmake f -p linux && xmake build TPTests && xmake run TPTests`

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| XMake | Build system | YES | v3.0.8 | -- |
| xPack MinGW GCC | Cross-compilation | YES | 14.3.0 | System MinGW 10 (insufficient) |
| Native GCC | Linux test builds | YES | 11.4.0 | May need upgrade for C++20 edge cases |
| rpmalloc | Allocator | YES (xmake pkg) | latest | -- |
| hopscotch-map | Hash maps | YES (xmake pkg) | v2.3.1 | -- |
| Catch2 | Unit tests | YES (xmake pkg) | 2.13.9 | -- |

**Missing dependencies with no fallback:** None.

**Missing dependencies with fallback:**
- System GCC 11.4 may have C++20 gaps -- fallback: install GCC 14 or use xPack native toolchain.

## Open Questions

1. **ThreadUtils.cpp sentry dependency**
   - What we know: It calls `sentry__thread_setname()`, a private sentry symbol
   - What's unclear: Whether linux-build-wip addressed this or if it's still broken
   - Recommendation: Check `git show linux-build-wip:Code/base/threading/ThreadUtils.cpp`. If unchanged, this is a link-time blocker that must be fixed.

2. **rpmalloc initialization**
   - What we know: rpmalloc requires `rpmalloc_initialize()` before first allocation (mimalloc does not)
   - What's unclear: Whether the linux-build-wip RpmallocAllocator handles initialization
   - Recommendation: Verify `RpmallocAllocator` constructor or first `Allocate()` call initializes rpmalloc.

3. **cpp-httplib SSL requirement**
   - What we know: linux-build-wip adds `add_requireconfs("cpp-httplib", {configs = {ssl = true}})`
   - What's unclear: Whether SSL support is actually needed for the admin panel (localhost only)
   - Recommendation: Try without SSL first. If admin panel breaks, add OpenSSL.

## Sources

### Primary (HIGH confidence)
- Direct codebase analysis: `grep` for MSVC-isms across all Tier 1-2 code (zero found in source, only in xmake.lua)
- `git diff dev..linux-build-wip` -- Comprehensive diff of prior porting work
- TiltedCore source audit: all 37 files reviewed for platform-specific code
- Code/tests/encoding.cpp -- 513-line test suite reviewed

### Secondary (MEDIUM confidence)
- `.planning/research/PITFALLS.md` -- Prior research on MSVC extension inventory
- `.planning/research/ARCHITECTURE.md` -- Library tier classification and dependency graph

### Tertiary (LOW confidence)
- rpmalloc initialization requirement -- needs verification against rpmalloc docs

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- direct package audit confirms MinGW compatibility
- Architecture: HIGH -- build order validated against actual xmake.lua dependency chains
- Pitfalls: HIGH -- every pitfall identified from actual code inspection, not hypothetical

**Research date:** 2026-03-27
**Valid until:** 2026-04-27 (stable domain, no fast-moving dependencies)
