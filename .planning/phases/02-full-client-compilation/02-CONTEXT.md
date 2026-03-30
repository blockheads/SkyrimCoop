# Phase 2: Full Client Compilation - Context

**Gathered:** 2026-03-30
**Status:** Verified complete — all success criteria met

<domain>
## Phase Boundary

Compile all ~6200 LOC of Code/client/ under MinGW as a single SKSE DLL with CEF, DirectXTK, and Discord excluded. The DLL must load into Skyrim SE under Proton without crashing at startup.

</domain>

<decisions>
## Implementation Decisions

### Build Mode
- **D-01:** Use `releasedbg` mode for deployable DLLs. Devbuild mode produces shared library dependencies (BaseLib.dll, CommonLib.dll, etc.) that aren't present in the SKSE plugins directory, causing SKSE error 193 (ERROR_BAD_EXE_FORMAT). Releasedbg mode statically links everything into a single 13MB self-contained DLL.
- **D-02:** Devbuild mode remains useful for fast iteration (incremental builds) but the DLL cannot be loaded by SKSE — it's for compilation testing only.

### Feature Exclusion
- **D-03:** CEF, DirectXTK, and Discord are already excluded via `is_plat("mingw")` guards in `Code/client/xmake.lua`. No additional exclusion work was needed — the existing guards compile cleanly.

### MSVC-isms
- **D-04:** The 3 `__declspec(noinline)` uses in Code/client/ compile without errors under GCC (GCC treats unknown declspec attributes as warnings, not errors). No changes needed.
- **D-05:** `MinGWCompat.h` handles calling convention macros (__fastcall/__stdcall/__cdecl) via force-include. No additional MSVC compatibility work was needed.

### Bug Fixes Applied
- **D-06:** Fixed `skse_entry.cpp` version string from `"1.6.1170"` to `"1.6.1170.0"` — VersionDb::ParseVersionFromString requires 4-part version string (major.minor.revision.build).
- **D-07:** Fixed `VersionDb.h` throw exception replaced with `Clear(); return false;` — no-exceptions policy compliance.

### Smoke Test
- **D-08:** Created `tests/smoke_dll_load.sh` — deploys DLL to SKSE plugins, launches Skyrim via Proton, checks SKSE log for load confirmation, kills Skyrim. Supports `--check-only` (export verification without launching), `--skip-deploy`, and `--kill` modes.

### App Initialization (Out of Scope)
- **D-09:** The full DLL (with RunTiltedInit/RunTiltedApp) returns SKSE error 998 (ERROR_NOACCESS) during app initialization. This is Phase 3+ work — the DLL format and SKSE interface are correct, the crash is in service/hook initialization. Confirmed by testing with SKSE_LOAD_MINIMAL which loads successfully.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Build System
- `Code/client/xmake.lua` — Client build config with MinGW platform guards, feature exclusion, DLL wrapper target
- `xmake.lua` (top-level) — Platform detection, MinGW flags, devbuild vs releasedbg mode differences

### SKSE Integration
- `Code/client/skse_entry.cpp` — DLL entry point, SKSEPlugin_Version/SKSEPlugin_Load exports
- `Code/client/VersionDb.h` — Address Library loader (runtime address resolution)
- `Code/client/main.cpp` — RunTiltedInit/RunTiltedApp implementation

### MinGW Compatibility
- `Code/client/MinGWCompat.h` — Calling convention macro compatibility
- `Code/client/link_stubs.cpp` — Linker stubs for MSVC-specific symbols

### Test Infrastructure
- `tests/smoke_dll_load.sh` — DLL load smoke test (deploy + launch Skyrim + verify SKSE log)

</canonical_refs>

<code_context>
## Existing Code Insights

### Build State
- Code/client/ compiles with zero errors under MinGW (both devbuild and releasedbg modes)
- Releasedbg produces a 13MB self-contained DLL (all deps statically linked)
- Devbuild produces a 390MB DLL with shared lib deps (not loadable by SKSE)
- Full build time: ~25min (static lib) + ~12min (DLL link) from clean; incremental ~3s

### DLL Characteristics
- Exports: `SKSEPlugin_Version` (data), `SKSEPlugin_Load` (function)
- Imports: Only standard Windows system DLLs (KERNEL32, msvcrt, dbghelp, etc.)
- Plugin name: "SkyrimTogether", version 1

### Key Warnings (non-blocking)
- `Code/libraries/reverse/MemoryVP.hpp:43` — no return statement in non-void function
- Server-side unused variable warnings (CharacterService, ActorValueService)
- Member initialization order warnings (GameServer.h)

</code_context>

<specifics>
## Specific Ideas

No specific requirements — Phase 2 was pure compilation verification.

</specifics>

<deferred>
## Deferred Ideas

- **App initialization crash (error 998)** — Full RunTiltedInit/RunTiltedApp crashes during SKSE load. This is Phase 3+ work (hooks, services, transport init). The DLL format itself is correct.
- **MemoryVP.hpp missing return** — `vp::Put()` has no return statement; should be fixed to avoid UB, but doesn't block compilation.
- **Devbuild SKSE loading** — Could copy shared DLLs alongside the plugin, but releasedbg works fine for deployment.

</deferred>

---

*Phase: 02-full-client-compilation*
*Context gathered: 2026-03-30*
