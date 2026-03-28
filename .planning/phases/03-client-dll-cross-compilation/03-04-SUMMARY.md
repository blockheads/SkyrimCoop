---
phase: 03-client-dll-cross-compilation
plan: 04
status: complete
started: 2026-03-27T19:00:00Z
completed: 2026-03-27T20:30:00Z
duration: 90min
gap_closure: true
---

## Summary

Closed the DLL linking gap: `xmake build SkyrimTogetherClientDLL` now produces a 12MB `SkyrimTogetherClient.dll` (PE32+ x86-64) with `SKSEPlugin_Load` and `SKSEPlugin_Version` exports.

## What Was Built

1. **skse_entry.cpp** — Thin DLL entry point that exports SKSE plugin symbols and bridges to `RunTiltedInit`/`RunTiltedApp`. Uses inline SKSE struct definitions (same C-compatible pattern as gate01 feasibility test).

2. **link_stubs.cpp** — Stub implementations for two categories of unresolved symbols:
   - Skyrim engine virtual functions (ActorValueOwner, IAnimationGraphManagerHolder) — resolved at runtime via address library when DLL loads into Skyrim's process
   - MSVC UCRT intrinsics (`__intrinsic_setjmpex`, `__local_stdio_printf_options`, `__stdio_common_vsnwprintf_s`) — needed because lua and libuv packages were compiled with MSVC

3. **SkyrimTogetherClientDLL xmake target** — Shared library target with:
   - `before_link` cleanup of stale `.dll.a` import libraries (root cause of initial link failures)
   - `--noinhibit-exec` flag (MinGW equivalent of MSVC `/FORCE`) for remaining unresolved game engine symbols
   - `--allow-multiple-definition` for static data in headers (MinGW equiv of `/FORCE:MULTIPLE`)

## Key Decisions

| # | Decision | Why |
|---|----------|-----|
| 1 | Use `--noinhibit-exec` instead of individual stubs | Skyrim has dozens of engine symbols resolved at runtime; stubbing each is unsustainable |
| 2 | Keep link_stubs.cpp for MSVC intrinsics and core engine vtables | These must be defined for the PE linker even with --noinhibit-exec |
| 3 | Add `before_link` callback for .dll.a cleanup | Stale import libraries from failed builds caused the linker to use tiny .dll.a instead of 1.5GB .a |

## Deviations

- Plan specified `add_ldflags` for `--whole-archive`; xmake strips ldflags for shared targets. Used `add_deps` with `before_link` cleanup instead.
- Added `link_stubs.cpp` (not in original plan) for MSVC intrinsic compatibility and core engine stubs.
- Added `--noinhibit-exec` (not in plan) because MinGW PE linker has no equivalent of `--unresolved-symbols=ignore-all` for PE/COFF.

## Key Files

### Created
- `Code/client/skse_entry.cpp` — SKSE DLL entry point
- `Code/client/link_stubs.cpp` — Linker stubs for runtime-resolved symbols

### Modified
- `Code/client/xmake.lua` — Added SkyrimTogetherClientDLL target, excluded DLL-only files from static lib glob
- `tests/smoke_test_client.sh` — Updated auto-detection to exclude cache dirs

## Verification

- `file SkyrimTogetherClient.dll` → `PE32+ executable (DLL) (console) x86-64, for MS Windows`
- `objdump -p` shows exports: `SKSEPlugin_Load`, `SKSEPlugin_Version`
- DLL size: 12MB (contains all client code via dep linking)
- Smoke test auto-detection: finds DLL in build tree
- Static library target unaffected: `libSkyrimTogetherClient.a` (1.5GB) still builds

## Self-Check: PASSED
