---
phase: 02-msvc-compatibility-core-libraries
plan: 02
subsystem: infra
tags: [xmake, sentry, mingw, build-system, pthread, crash-handler]

# Dependency graph
requires:
  - phase: 02-msvc-compatibility-core-libraries
    plan: 01
    provides: Root xmake.lua cleaned, TiltedCore migrated to rpmalloc
provides:
  - All Tier 1-2 xmake.lua files cleaned of Wine MSVC workarounds
  - sentry-native fully removed from build config and source code
  - ThreadUtils.cpp uses platform-native thread naming (no sentry dependency)
  - CrashHandler.cpp uses HAS_SENTRY conditional compilation
  - Tier 3 client targets gated to windows|mingw
  - BaseLib excludes Windows dialog files on non-Windows platforms
affects: [02-03, all-server-targets, all-client-targets]

# Tech tracking
tech-stack:
  added: []
  patterns: [has-sentry-conditional, platform-native-thread-naming, tier3-platform-gating]

key-files:
  created: []
  modified:
    - Code/encoding/xmake.lua
    - Code/common/xmake.lua
    - Code/base/xmake.lua
    - Code/base/threading/ThreadUtils.cpp
    - Code/libraries/xmake.lua
    - Code/components/xmake.lua
    - Code/components/crash_handler/CrashHandler.cpp
    - Code/components/crash_handler/xmake.lua
    - Code/admin_protocol/xmake.lua
    - Code/server/xmake.lua
    - Code/server_runner/xmake.lua
    - Code/xmake.lua

key-decisions:
  - "sentry-native removed entirely with HAS_SENTRY conditionals for future re-enablement"
  - "ThreadUtils uses SetThreadDescription (Win10+) and pthread_setname_np (Linux) instead of sentry internals"
  - "Tier 3 targets (Reverse, Hooks, UI, UIProcess) gated behind is_plat windows|mingw"
  - "imgui component include removed from components/xmake.lua (Tier 3 client concern)"
  - "Tests gated to non-mingw platforms (need native build)"

patterns-established:
  - "HAS_SENTRY conditional pattern for optional sentry-native support"
  - "Tier 3 client targets wrapped in is_plat('windows') or is_plat('mingw') gate"
  - "Platform-native thread naming: SetThreadDescription on Windows, pthread_setname_np on Linux"

requirements-completed: [BUILD-02, BUILD-03]

# Metrics
duration: 4min
completed: 2026-03-27
---

# Phase 02 Plan 02: Tier 1-2 Library Cleanup and Sentry Removal Summary

**Removed Wine MSVC workarounds from 9 xmake.lua files, replaced sentry dependency with platform-native thread naming, and added HAS_SENTRY conditional compilation to CrashHandler**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-27T19:53:30Z
- **Completed:** 2026-03-27T19:58:02Z
- **Tasks:** 2
- **Files modified:** 12

## Accomplishments
- All Tier 1-2 xmake.lua files use unconditional `set_kind("static")` -- zero Wine MSVC workarounds remain
- sentry-native completely removed from build config (base, server, server_runner, crash_handler)
- ThreadUtils.cpp rewritten to use Windows SetThreadDescription and Linux pthread_setname_np
- CrashHandler.cpp wrapped in HAS_SENTRY conditionals with graceful fallback logging
- Tier 3 client libraries (Reverse, Hooks, UI, UIProcess) properly gated to windows|mingw
- mimalloc replaced with rpmalloc in all library targets

## Task Commits

Each task was committed atomically:

1. **Task 1: Remove Wine MSVC workarounds from all Tier 1-2 xmake.lua files and gate Tier 3 targets** - `f1508696` (chore)
2. **Task 2: Fix ThreadUtils.cpp sentry dependency and add HAS_SENTRY conditionals to CrashHandler** - `946f58e8` (feat)

## Files Created/Modified
- `Code/encoding/xmake.lua` - Removed Wine MSVC workaround, unconditional static lib
- `Code/common/xmake.lua` - Removed Wine MSVC workaround, unconditional static lib
- `Code/base/xmake.lua` - Removed Wine MSVC workaround, removed sentry-native, added dialog file exclusion
- `Code/base/threading/ThreadUtils.cpp` - Rewritten with platform-native thread naming APIs
- `Code/libraries/xmake.lua` - Removed all Wine MSVC workarounds, replaced mimalloc with rpmalloc, gated Tier 3 targets
- `Code/components/xmake.lua` - Removed Wine MSVC workaround, removed imgui include
- `Code/components/crash_handler/CrashHandler.cpp` - Added HAS_SENTRY conditional compilation
- `Code/components/crash_handler/xmake.lua` - Removed sentry-native package (commented for future re-enablement)
- `Code/admin_protocol/xmake.lua` - Removed Wine MSVC workaround
- `Code/server/xmake.lua` - Removed Wine MSVC workaround and sentry-native package
- `Code/server_runner/xmake.lua` - Removed sentry bin copy and sentry-native/TiltedCore packages
- `Code/xmake.lua` - Added mingw to client gate, removed DirectXTK/imgui includes, gated tests

## Decisions Made
- Removed sentry-native entirely rather than conditionalizing in build files; HAS_SENTRY in source code allows future re-enablement
- Used SetThreadDescription (Win10 1607+ API) instead of older RaiseException-based thread naming
- Gated tests to non-mingw (cross-compiled tests can't run on build host)
- Removed `mem` package from Reverse and Hooks targets (Tier 3 client-only, not needed)
- Removed TiltedCore from server_runner packages (already inherited via deps)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- All Tier 1-2 library build configs are clean and ready for MinGW/GCC compilation
- Plan 03 (compilation testing and fix-up) can proceed
- No blockers identified

---
*Phase: 02-msvc-compatibility-core-libraries*
*Completed: 2026-03-27*
