---
phase: 03-client-dll-cross-compilation
plan: 01
subsystem: build-system
tags: [xmake, mingw, rpmalloc, cross-compilation, preprocessor-guards]

# Dependency graph
requires:
  - phase: 02-msvc-compatibility-core-libraries
    provides: "MinGW-compatible core libraries with rpmalloc allocator"
provides:
  - "Feature-gated client xmake.lua with HAS_CEF/HAS_DISCORD/HAS_DIRECTXTK guards"
  - "CEF/UI targets excluded from MinGW builds in libraries xmake.lua"
  - "MinGW-compatible PCH (x86intrin.h fallback)"
  - "Memory.cpp fully migrated from mimalloc to rpmalloc"
affects: [03-02-PLAN, 03-03-PLAN, phase-06-ui]

# Tech tracking
tech-stack:
  added: []
  patterns: ["per-feature preprocessor guards for platform-specific dependencies", "rpmalloc API for memory hooks"]

key-files:
  created: []
  modified:
    - Code/client/xmake.lua
    - Code/libraries/xmake.lua
    - Code/client/TiltedOnlinePCH.h
    - Code/client/Games/Memory.cpp

key-decisions:
  - "imgui kept unconditional for Phase 6 readiness"
  - "HookFormAllocateSentinelInit guarded behind _MSC_VER (MSVC CRT-specific, irrelevant for MinGW)"

patterns-established:
  - "Per-feature guard pattern: if not is_plat('mingw') for Windows-only features"
  - "rpmalloc API mapping: rpmemalign(alignment, size) argument order"

requirements-completed: [BUILD-04]

# Metrics
duration: 2min
completed: 2026-03-27
---

# Phase 03 Plan 01: Build System Feature Gates Summary

**Per-feature preprocessor guards (HAS_CEF/HAS_DISCORD/HAS_DIRECTXTK) added to client xmake.lua, CEF/UI targets excluded from MinGW, Memory.cpp migrated from mimalloc to rpmalloc, PCH fixed for MinGW with x86intrin.h fallback**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-27T21:19:52Z
- **Completed:** 2026-03-27T21:22:29Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Client xmake.lua now defines HAS_CEF, HAS_DISCORD, HAS_DIRECTXTK only for non-MinGW builds
- SkyrimCoopUI and SkyrimCoopUIProcess targets wrapped in `not is_plat("mingw")` guard
- Memory.cpp fully migrated from mimalloc to rpmalloc with correct API mapping (rpmemalign alignment-first)
- PCH intrin.h guarded with _MSC_VER / x86intrin.h fallback for MinGW compatibility
- WINE_MSVC_BUILD conditional removed (clean break per Phase 2 D-02)
- ImGui kept as unconditional package for Phase 6 readiness

## Task Commits

Each task was committed atomically:

1. **Task 1: Add per-feature guards to client and library xmake.lua files** - `c49fb849` (feat)
2. **Task 2: Fix PCH for MinGW and migrate Memory.cpp to rpmalloc** - `0cf2e7be` (feat)

## Files Created/Modified
- `Code/client/xmake.lua` - Feature-gated client build config with HAS_CEF/HAS_DISCORD/HAS_DIRECTXTK guards, rpmalloc package
- `Code/libraries/xmake.lua` - SkyrimCoopUI/UIProcess targets excluded from MinGW builds
- `Code/client/TiltedOnlinePCH.h` - MinGW-compatible PCH with x86intrin.h fallback
- `Code/client/Games/Memory.cpp` - Full rpmalloc migration replacing all mimalloc API calls

## Decisions Made
- Kept imgui as unconditional package (not gated behind MinGW check) for Phase 6 readiness
- Guarded _initterm_e hook and HookFormAllocateSentinelInit behind _MSC_VER since these are MSVC CRT-specific and irrelevant for MinGW
- Used #pragma GCC optimize("O0") as MinGW equivalent of #pragma optimize("", off)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- clang-format not available in execution environment; code follows existing file formatting conventions

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Build system foundation ready for Plan 02 (MSVC-ism cleanup in client source files)
- Per-feature guards established for HAS_CEF/HAS_DISCORD/HAS_DIRECTXTK to be used in source file guards
- Memory.cpp rpmalloc migration complete, no mimalloc references remain in client code

---
*Phase: 03-client-dll-cross-compilation*
*Completed: 2026-03-27*
