---
phase: 02-msvc-compatibility-core-libraries
plan: 01
subsystem: infra
tags: [xmake, rpmalloc, mimalloc, build-system, tiltedcore, mingw]

# Dependency graph
requires:
  - phase: 01-mingw-toolchain-skse-gate
    provides: MinGW platform block in root xmake.lua, validated SKSE plugin loading
provides:
  - Root xmake.lua cleaned of all MSVC blocks, mimalloc replaced with rpmalloc, sentry-native removed
  - TiltedCore builds as unconditional static library with rpmalloc allocator
  - Foundation for all Tier 1-2 library builds under MinGW/GCC
affects: [02-02, 02-03, all-downstream-targets]

# Tech tracking
tech-stack:
  added: [rpmalloc]
  patterns: [unconditional-static-lib, no-wine-msvc-workaround]

key-files:
  created:
    - Code/TiltedCore/RpmallocAllocator.hpp
    - Code/TiltedCore/RpmallocAllocator.cpp
  modified:
    - xmake.lua
    - Code/TiltedCore/xmake.lua
    - Code/TiltedCore/Allocator.cpp

key-decisions:
  - "rpmalloc replaces mimalloc as default allocator (MinGW/GCC compatible, proven on linux-build-wip branch)"
  - "All is_plat('windows') MSVC blocks deleted from root xmake.lua per D-02 (clean break, git history preserves)"
  - "TiltedCore builds as unconditional static lib (Wine MSVC object workaround removed)"

patterns-established:
  - "No MSVC-specific build config in root xmake.lua -- MinGW and Linux only"
  - "Libraries use set_kind('static') unconditionally, no platform-conditional kind switching"

requirements-completed: [BUILD-02]

# Metrics
duration: 3min
completed: 2026-03-27
---

# Phase 02 Plan 01: Root Build Config and TiltedCore Allocator Migration Summary

**Root xmake.lua cleaned of MSVC blocks and sentry-native, TiltedCore migrated from mimalloc to rpmalloc with Wine MSVC object workaround removed**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-27T19:48:25Z
- **Completed:** 2026-03-27T19:51:11Z
- **Tasks:** 2
- **Files modified:** 5 (plus 2 deleted)

## Accomplishments
- Root xmake.lua has zero MSVC blocks, zero sentry-native refs, zero mimalloc refs -- ready for MinGW/GCC builds
- TiltedCore allocator swapped from mimalloc to rpmalloc with identical API surface
- Wine MSVC object library workaround removed from TiltedCore (builds as unconditional static lib)
- MimallocAllocator files deleted, RpmallocAllocator files created following linux-build-wip branch patterns

## Task Commits

Each task was committed atomically:

1. **Task 1: Clean root xmake.lua** - `46a93fe7` (chore)
2. **Task 2: Migrate TiltedCore from mimalloc to rpmalloc** - `32d14fb2` (feat)

## Files Created/Modified
- `xmake.lua` - Removed MSVC blocks, swapped mimalloc for rpmalloc, removed sentry-native and mem packages
- `Code/TiltedCore/xmake.lua` - Simplified to unconditional static lib with rpmalloc dependency
- `Code/TiltedCore/RpmallocAllocator.hpp` - New rpmalloc allocator header (replaces MimallocAllocator)
- `Code/TiltedCore/RpmallocAllocator.cpp` - New rpmalloc allocator implementation
- `Code/TiltedCore/Allocator.cpp` - Default allocator switched from MimallocAllocator to RpmallocAllocator
- `Code/TiltedCore/MimallocAllocator.hpp` - DELETED
- `Code/TiltedCore/MimallocAllocator.cpp` - DELETED

## Decisions Made
- Used rpmalloc as mimalloc replacement (proven on linux-build-wip branch, MinGW/GCC compatible)
- Deleted all MSVC blocks rather than conditionalizing (per D-02 locked decision)
- Removed sentry-native entirely (not needed for MinGW builds, can be re-added later if needed)
- Removed mem 1.0.0 package (Tier 3 client-only, not needed for Tier 1-2 builds)
- Kept upload-symbols task as harmless legacy (doesn't affect builds)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Root build config and TiltedCore are ready for Plan 02 (Tier 1-2 library xmake.lua cleanup)
- All downstream targets can now resolve rpmalloc instead of mimalloc
- No blockers identified

---
*Phase: 02-msvc-compatibility-core-libraries*
*Completed: 2026-03-27*
