# Technology Stack: MinGW Full Client + Embedded Host-Authoritative Server

**Project:** SkyrimCoop v2.0
**Researched:** 2026-03-30
**Focus:** Stack additions and changes needed to build Code/client/ under MinGW as a single SKSE DLL with embedded GameServer
**Confidence:** HIGH for proven items, HIGH for new additions, MEDIUM for ABI compat gaps

---

## Already Proven — No Changes Needed

Do not re-research or revisit these. They are validated by prior phases.

| Technology | Version | Status |
|------------|---------|--------|
| xPack MinGW-w64 GCC | 14.3.0-1.1 | Proven: GATE-01, GATE-02. Installed at `~/.local/xPacks/`. |
| XMake + `-p mingw` | 2.8.5+ | Proven: all Tier 1-3 libs build. MinGW platform support confirmed. |
| SKSE plugin ABI | 2.2+ | Proven: DLL loads in Skyrim. `extern "C"` entry points via GATE-01. |
| Address Library v2 | — | Proven: runtime address resolution works. Phase 7. |
| MinHook | v1.3.3 | Proven: x64 function hooking under MinGW. Phase 1 GATE-02. |
| enet6 UDP networking | — | Proven: GameServer communicates with enet6. Phase 5/9. |
| EnTT ECS | v3.10.0 | In use. Builds under MinGW via xmake package. |
| spdlog | v1.13.0 | In use. Builds under MinGW. |
| MinGWCompat.h | — | Proven: calling convention `#undef`s work. Force-include pattern established. |
| `-mms-bitfields` | — | Proven: MSVC-compatible bitfield layout. In root xmake.lua. |
| Split DWARF + lld | GCC 14.3 / lld-18 | Proven: `devbuild` mode, `SkyrimTogetherClient.map` produced. |
| Message encoding | 58+61 opcodes | Existing STR codebase. Builds under MinGW via SkyrimEncoding target. |
| Server builds on Linux | — | Existing STR codebase. Linux CI works. |

---

## New Dependencies for v2.0

### Server Packages Required Under MinGW

When the server is included in the MinGW build (`Code/server/` is unconditionally included in `Code/xmake.lua`), xmake must resolve these packages for the `mingw` platform. They are not currently guarded with `is_plat` checks.

| Package | Version | Risk | Notes |
|---------|---------|------|-------|
| sol2 | v3.3.0 | LOW | Header-only C++ library. Declared in `Code/server/xmake.lua` unconditionally. Explicit GCC/Clang support in sol2 docs. Server already builds this on Linux. No MinGW-specific risk. |
| lua | (via sol2 config) | LOW | Pure C99. `add_requires("sol2 v3.3.0", {configs = {lua = "lua"}})` pulls Lua as a dep. C builds trivially on MinGW. |
| sqlite3 | (xmake package) | LOW | Pure C. Used by server. Cross-compiles trivially via MinGW xmake package. |
| nlohmann_json | v3.11.3 | LOW | Header-only C++. Already in root `add_requires`. No platform-specific code. |

**Integration point:** These are declared in `Code/server/xmake.lua` which is always included. If xmake cannot resolve `sol2` or `lua` for the `mingw` platform at configure time, you will see package resolution errors, not compile errors. Run `xmake f -p mingw` first and watch for package fetch failures before starting compilation work.

### No New Libraries Required

The embedded server architecture does not require new dependencies. The client already declares `add_deps("SkyrimTogetherServer")` in `Code/client/xmake.lua`. `HostService` already exists and is wired to instantiate `Server::GameServer`. The dependency chain is already expressed in the build system — the issue is making it compile cleanly, not adding new packages.

---

## ABI Compatibility Gaps to Close

These are concrete issues discovered during research that require changes to `MinGWCompat.h` or source files. None are blockers at configure time — they generate silent warnings today and will need fixing when specific compilation units fail or produce wrong behavior.

### Gap 1: `__declspec(align(N))` Silently Dropped

**Confirmed experimentally:** MinGW GCC 14.3 emits `-Wattributes` warning and ignores `__declspec(align(N))`. The alignment is not applied.

**Affected files in Code/client/:**
- `Games/Skyrim/SaveLoad.h:145` — `__declspec(align(8)) BGSSaveLoadScrapBuffer`
- `Games/Skyrim/SaveLoad.h:165` — `__declspec(align(8)) BGSLoadFormData`
- `Games/Skyrim/BSGraphics/BSGraphicsRenderer.h:7` — `__declspec(align(8)) BSCriticalSection`

**Risk assessment:** These three structs use `align(8)`, which matches the natural alignment of their pointer/integer members on x64. The structs will have correct size/layout without the explicit annotation (confirmed: `BGSSaveLoadScrapBuffer` = 0x18 either way). `SKYRIM_STRUCT_ASSERT` is already a no-op on MinGW, so no build failures. The risk is future game struct additions where alignment diverges from natural alignment — and those won't be caught.

**Fix:** Add to `MinGWCompat.h` after the calling convention undefs:
```cpp
// Map __declspec(align(N)) to alignas(N) for MinGW.
// MinGW GCC ignores __declspec(align) silently; use GCC's __attribute__((aligned)) instead.
// The compiler's __declspec macro only handles dllexport/dllimport on GCC.
// Note: This only works because NOMINMAX and other __declspec uses are safe to alias.
// Do NOT add a catch-all __declspec redef — it would break dllexport/dllimport.
```
Then replace the three `__declspec(align(N))` uses with `alignas(N)` directly in the headers (preferred over macro magic for clarity). The headers are already in the SkyrimCoop repo, not SKSE proper — edits are permitted.

### Gap 2: `__declspec(noinline)` Silently Dropped

**Status:** Same issue as align. MinGW GCC only recognizes `__declspec(dllexport)` and `__declspec(dllimport)`. All other `__declspec` annotations emit `-Wattributes` and are silently ignored.

**Affected files in Code/client/:**
- `Systems/FaceGenSystem.cpp:22` — `__declspec(noinline) NiTriBasedGeom* GetHeadTriBasedGeom`
- `Services/Debug/Views/ComponentView.cpp:36` — `static __declspec(noinline) bool DrawInWorldSpace`
- `Services/Debug/DebugService.cpp:80` — `void __declspec(noinline) DebugService::PlaceActorInWorld`

**Risk assessment:** `noinline` is a performance annotation — its absence causes the functions to be inlined when the compiler decides. This cannot cause incorrect behavior. However, FaceGenSystem's noinline use may be intentional to prevent register clobbering bugs with specific game API calls. Low severity, but document for FaceGenSystem work.

**Fix:** Replace with `__attribute__((noinline))` guarded by `#ifndef _MSC_VER`, or add a compat macro in `MinGWCompat.h`:
```cpp
#if defined(__GNUC__) && !defined(_MSC_VER)
#define __declspec_noinline __attribute__((noinline))
#else
#define __declspec_noinline __declspec(noinline)
#endif
```
Then update the three call sites. Low priority — will produce warnings, not failures.

### Gap 3: `-flayout-compat=ms` TODO is a Non-Existent Flag

**Status:** `TiltedOnlinePCH.h` line 26 contains:
```
// TODO: Upgrade to GCC 12+ and use -flayout-compat=ms for proper MSVC ABI compat.
```

**Finding:** This flag does not exist in GCC. Confirmed by checking GCC 14.3 target options — there is no `-flayout-compat` or equivalent. The real mitigations are already in place:
- `-mms-bitfields` handles bitfield layout (in root `xmake.lua`)
- `SKYRIM_STRUCT_ASSERT(...)` is `/* no-op */` on MinGW (in `TiltedOnlinePCH.h`)
- Game struct layout is validated at runtime via Address Library offsets, not compile-time asserts

**Action:** Remove the TODO comment from `TiltedOnlinePCH.h`. It misleads future developers into searching for a non-existent compiler option. The existing mitigations are the correct approach.

---

## Build System Integration Points

### Server PCH Under MinGW

The client's PCH needed force-include (not `.gch`) on MinGW because the `#undef __fastcall` state at the end of the PCH must survive to compilation units. `Code/server/xmake.lua` uses `set_pcxxheader("Pch.h")` unconditionally.

**Assessment:** Server `Pch.h` has no calling convention `#undef`s at the end. The `.gch` approach works for server. No change needed.

### PE/COFF Symbol Budget: Embedded Server Impact

`Code/client/xmake.lua` already notes the `>65535 symbols` PE/COFF ordinal limit and addresses it by *not* using `--export-all-symbols` on the final `SkyrimTogetherClientDLL` target. The DLL exports only the two SKSE entry points (`SKSEPlugin_Version`, `SKSEPlugin_Load`).

The embedded server adds `~5,310 LOC` of C++ class templates and services to the static archive, adding template instantiations but not exported symbols. Since ordinal limits apply only to DLL exports (not total symbols in the PE's symbol table), and the final DLL only exports two symbols, the embedded server does not create a new PE/COFF risk.

**In devbuild mode:** Internal components (server, networking, encoding) are separate `.dll` files via `devbuild_shared()`. Only the final `SkyrimTogetherClientDLL` is the one-DLL target with limited exports. This architecture already handles the symbol budget correctly.

### sol2 PCH Interaction

`sol/sol.hpp` is a 30,000+ line header included via the server PCH. In the client's static archive chain, the server's PCH precompilation will be used only for server `.cpp` files. Client `.cpp` files use the client PCH (which does not include sol2). No interaction issue.

However, unity builds (`--unitybuild`) can cause sol2 to be re-included multiple times if batch grouping mixes server and client source files. **Recommendation:** Keep server and client in separate unity build groups (they are in separate xmake targets, so this is automatic) and verify that `xmake config --unitybuild=y` does not create cross-target batches.

---

## What NOT to Add

| Technology | Why Not |
|------------|---------|
| CEF | MSVC binary distribution only. Already excluded by `if not is_plat("mingw")` guard. Do not attempt MinGW linkage. |
| DirectXTK | MSVC-only (D3D/Windows SDK headers use MSVC extensions). Already excluded. UI is deferred to a later milestone. |
| Discord SDK | Windows MSVC binary. Already excluded. Steam invites are out of scope for v2.0. |
| sentry-native | Not compatible with MinGW cross-compilation. Already disabled in `CrashHandler` xmake (`-- sentry-native disabled`). The CrashHandler component builds fine without it (no `HAS_SENTRY` define). |
| Steam SDK | Out of scope. Direct IP connect is the v2.0 mechanism. |
| Vivox | Excluded via `has_config("vivox")` guard. Not needed for co-op. |
| Native Linux SKSE client | Out of scope per PROJECT.md. SKSE stays Windows, runs under Proton. |
| LLDB Wine patches | Deferred per v1.0 research. werat patches target LLVM 14.x, rebase to 21.x is uncharted work. GDB + DWARF is sufficient. |

---

## Version Compatibility Matrix

| Package | Version Pinned | Compatible With | Notes |
|---------|----------------|-----------------|-------|
| sol2 | v3.3.0 | Lua 5.1-5.4, LuaJIT | Server specifies `{configs = {lua = "lua"}}` which pins to standard Lua. |
| EnTT | v3.10.0 | C++17/20 | No breaking API changes in this version for the patterns used. |
| enet6 | (unpinned) | MinGW x64 | UDP/UTP transport. Already proven on both Linux and MinGW platforms. |
| ImGui | v1.89.7 | D3D11, Win32 backend | Windows-only backends (D3D11, Win32) compile on MinGW via d3d11.h/windows.h from mingw-w64 headers. |

---

## Sources

- Experimental: `x86_64-w64-mingw32-g++ --help=target` — confirmed `-flayout-compat=ms` does not exist in GCC 14.3
- Experimental: MinGW `__declspec(align(8))` compile test — confirmed silently ignored with `-Wattributes`
- `Code/client/xmake.lua` — devbuild shared strategy, PE/COFF ordinal limit note, sol2 PCH architecture
- `Code/server/xmake.lua` — sol2/lua/sqlite3/nlohmann_json package declarations
- `Code/client/MinGWCompat.h` — current scope (calling conventions only)
- `Code/client/TiltedOnlinePCH.h` — `-flayout-compat=ms` TODO, `SKYRIM_STRUCT_ASSERT` no-op on MinGW
- `Code/client/link_stubs.cpp` — existing MinGW stub pattern for unresolved symbols
- `Code/components/crash_handler/xmake.lua` — sentry-native disabled for MinGW, confirmed
- sol2 documentation: https://sol2.readthedocs.io/en/latest/ — header-only, GCC/Clang support confirmed

---

*Stack research for: SkyrimCoop v2.0 — MinGW full client + embedded host-authoritative server*
*Researched: 2026-03-30*
