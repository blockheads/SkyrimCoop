---
phase: 02-msvc-compatibility-core-libraries
plan: 03
subsystem: infra
tags: [xmake, mingw, linux, build-validation, catch2, rpmalloc, tests]

# Dependency graph
requires:
  - phase: 02-msvc-compatibility-core-libraries
    plan: 01
    provides: Root xmake.lua cleaned, TiltedCore migrated to rpmalloc
  - phase: 02-msvc-compatibility-core-libraries
    plan: 02
    provides: All Tier 1-2 xmake.lua files cleaned of Wine MSVC workarounds
provides:
  - Full MinGW cross-compilation of Tier 1-2 chain validated (TiltedCore through SkyrimTogetherServer)
  - Native Linux server build validated
  - All Catch2 tests passing on native Linux (D-09)
  - TPTests config fixed to use transitive dependencies
affects: [phase-03-client-migration, all-downstream-builds]

# Tech tracking
tech-stack:
  added: []
  patterns: [rpmalloc-explicit-init, platform-gated-packages, case-sensitive-includes]

key-files:
  created: []
  modified:
    - Code/tests/xmake.lua
    - Code/tests/main.cpp
    - Code/TiltedCore/BoundedAllocator.hpp
    - Code/TiltedCore/BoundedAllocator.cpp
    - Code/xmake.lua
    - Code/components/xmake.lua
    - Code/components/imgui/xmake.lua
    - Code/base/dialogues/win/TaskDialog.h
    - Code/base/dialogues/win/TaskDialog.cpp
    - xmake.lua

key-decisions:
  - "rpmalloc requires explicit initialization in test main (unlike mimalloc auto-init)"
  - "minhook and xbyak gated to windows|mingw in root xmake.lua (unsupported on Linux)"
  - "External vendored libs (DirectXTK, imgui) re-added with windows|mingw platform gate"
  - "Windows header includes lowercased for MinGW case-sensitivity (windows.h, commctrl.h)"

patterns-established:
  - "Test executables must call rpmalloc_initialize() before any TiltedCore allocations"
  - "Windows-only packages gated with is_plat conditionals in root add_requires"
  - "Case-sensitive includes for MinGW: always use lowercase windows.h"

requirements-completed: [BUILD-03]

# Metrics
duration: 14min
completed: 2026-03-27
---

# Phase 02 Plan 03: Build Validation and Test Verification Summary

**Full Tier 1-2 MinGW cross-compile chain validated, native Linux server build succeeds, all 28 Catch2 test assertions pass on Linux (D-09)**

## Performance

- **Duration:** 14 min
- **Started:** 2026-03-27T20:01:04Z
- **Completed:** 2026-03-27T20:15:16Z
- **Tasks:** 2
- **Files modified:** 10

## Accomplishments
- TPTests xmake.lua cleaned: removed TiltedCore/mimalloc/hopscotch-map packages (inherited transitively), removed Wine MSVC disable check
- Full MinGW cross-compilation of 11 Tier 1-2 static libraries validated (TiltedCore, SkyrimEncoding, CommonLib, BaseLib, SkyrimCoopNetworking, AdminProtocol, Console, ESLoader, CrashHandler, Resources, SkyrimTogetherServer)
- Native Linux server build succeeds with all Tier 1-2 targets
- All 28 assertions in 5 Catch2 test cases pass on native Linux (D-09 requirement met)
- D-04 compliance verified: zero active #pragma comment(lib) in Tier 1-2 code (vendored files properly guarded by _MSC_VER)

## Task Commits

Each task was committed atomically:

1. **Task 1: Fix TPTests xmake.lua and validate full MinGW build** - `8069991a` (feat)
2. **Task 2: Validate native Linux server build and run TPTests (D-09)** - `aeb97959` (feat)

## Files Created/Modified
- `Code/tests/xmake.lua` - Simplified to only direct dependencies (catch2, glm), everything else inherited transitively
- `Code/tests/main.cpp` - Custom Catch2 runner with rpmalloc initialization/finalization
- `Code/TiltedCore/BoundedAllocator.hpp` - Updated to inherit from RpmallocAllocator (was MimallocAllocator)
- `Code/TiltedCore/BoundedAllocator.cpp` - Updated parent class references to RpmallocAllocator
- `Code/xmake.lua` - Re-added external/DirectXTK and external/imgui includes gated to windows|mingw
- `Code/components/xmake.lua` - Re-added imgui component include gated to windows|mingw
- `Code/components/imgui/xmake.lua` - Extended platform gate to include mingw
- `Code/base/dialogues/win/TaskDialog.h` - Lowercased Windows.h include for MinGW
- `Code/base/dialogues/win/TaskDialog.cpp` - Lowercased Windows.h and Commctrl.h includes for MinGW
- `xmake.lua` - Gated minhook and xbyak packages to windows|mingw

## Decisions Made
- Used custom Catch2 runner (`CATCH_CONFIG_RUNNER`) instead of `CATCH_CONFIG_MAIN` to call `rpmalloc_initialize()` before any tests run
- Gated minhook/xbyak to windows|mingw in root xmake.lua since they are unsupported on Linux
- Re-added external vendored library includes (DirectXTK, imgui) that Plan 02 removed, but properly gated to windows|mingw -- these are needed for client target resolution on MinGW
- Accepted existing `#pragma comment(lib)` in vendored imgui files since they are already guarded by `#ifdef _MSC_VER`

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] BoundedAllocator still referenced MimallocAllocator**
- **Found during:** Task 1 (MinGW build)
- **Issue:** BoundedAllocator.hpp included MimallocAllocator.hpp and inherited from MimallocAllocator, which was deleted in Plan 01
- **Fix:** Updated to include RpmallocAllocator.hpp and inherit from RpmallocAllocator
- **Files modified:** Code/TiltedCore/BoundedAllocator.hpp, Code/TiltedCore/BoundedAllocator.cpp
- **Verification:** TiltedCore compiles successfully under MinGW
- **Committed in:** 8069991a (Task 1 commit)

**2. [Rule 3 - Blocking] External vendored library includes removed by Plan 02**
- **Found during:** Task 1 (MinGW configure)
- **Issue:** Plan 02 removed `includes("external/DirectXTK")` and `includes("external/imgui")` from Code/xmake.lua, but client targets still depend on imgui target
- **Fix:** Re-added both includes gated to `is_plat("windows") or is_plat("mingw")`, also added imgui component include back to components/xmake.lua with same gate, and extended imgui component's own xmake.lua gate to include mingw
- **Files modified:** Code/xmake.lua, Code/components/xmake.lua, Code/components/imgui/xmake.lua
- **Verification:** MinGW configure succeeds, all targets resolve
- **Committed in:** 8069991a (Task 1 commit)

**3. [Rule 3 - Blocking] Windows.h case sensitivity on MinGW**
- **Found during:** Task 1 (MinGW build of BaseLib)
- **Issue:** TaskDialog files used `<Windows.h>` and `<Commctrl.h>` which don't exist on case-sensitive Linux filesystem (MinGW provides lowercase headers)
- **Fix:** Changed to `<windows.h>` and `<commctrl.h>`
- **Files modified:** Code/base/dialogues/win/TaskDialog.h, Code/base/dialogues/win/TaskDialog.cpp
- **Verification:** BaseLib compiles under MinGW
- **Committed in:** 8069991a (Task 1 commit)

**4. [Rule 3 - Blocking] minhook package unsupported on Linux**
- **Found during:** Task 2 (Linux configure)
- **Issue:** minhook v1.3.3 and xbyak v7.06 in root add_requires() are Windows-only packages, causing Linux configure failure
- **Fix:** Moved to platform-gated add_requires inside `is_plat("windows") or is_plat("mingw")` block
- **Files modified:** xmake.lua (root)
- **Verification:** Linux configure and build succeed
- **Committed in:** aeb97959 (Task 2 commit)

**5. [Rule 1 - Bug] rpmalloc not initialized in test main**
- **Found during:** Task 2 (TPTests SIGSEGV on Linux)
- **Issue:** Tests crashed with SIGSEGV because rpmalloc requires explicit rpmalloc_initialize() before any allocations, unlike mimalloc which auto-initialized
- **Fix:** Replaced CATCH_CONFIG_MAIN with custom main() that calls rpmalloc_initialize()/rpmalloc_finalize()
- **Files modified:** Code/tests/main.cpp
- **Verification:** All 28 assertions in 5 test cases pass
- **Committed in:** aeb97959 (Task 2 commit)

---

**Total deviations:** 5 auto-fixed (2 bugs from Plan 01 migration, 3 blocking issues)
**Impact on plan:** All fixes were necessary for build validation to succeed. No scope creep -- these are direct consequences of the MSVC-to-MinGW migration that the validation step was designed to catch.

## Issues Encountered
- xmake cache contention from parallel agent execution required using XMAKE_CONFIGDIR for isolated build config
- Build warnings (unused variables, integer size, member reorder) are pre-existing and non-fatal

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Phase 02 is now complete: all Tier 1-2 libraries build under both MinGW cross-compilation and native Linux
- Native Linux test suite passes (D-09 validated)
- Phase 03 (client migration) can proceed with confidence that the core library chain compiles under MinGW
- Tier 3 client targets (SkyrimTogetherClient, etc.) are properly gated but not yet buildable (Phase 3 scope)
- No blockers identified

## Known Stubs

None - this plan is build validation only, no UI or data flow stubs.

---
*Phase: 02-msvc-compatibility-core-libraries*
*Completed: 2026-03-27*
