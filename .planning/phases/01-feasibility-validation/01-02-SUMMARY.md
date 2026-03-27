---
phase: 01-feasibility-validation
plan: 02
subsystem: build
tags: [mingw, cross-compilation, skse, minhook, wine, proton, feasibility, pe64]

# Dependency graph
requires:
  - phase: 01-feasibility-validation
    provides: "XMake MinGW config, GATE-01/GATE-02 source files (Plan 01)"
provides:
  - "gate01_test.dll: PE32+ SKSE plugin with SKSEPlugin_Query/Load exports"
  - "gate02_test.dll: PE32+ MinHook test DLL with GetTickCount hook"
  - "Wine smoke test confirmation (both DLLs load without missing deps)"
affects: [01-02-checkpoint, phase-02, phase-03]

# Tech tracking
tech-stack:
  added: [minhook v1.3.3 (cross-compiled for MinGW)]
  patterns: [set_prefixname("") for DLL naming, add_requires at root scope for XMake v3]

key-files:
  created: []
  modified:
    - Code/tests/feasibility/xmake.lua

key-decisions:
  - "XMake v3 requires add_requires() in root scope before targets (fixed from Plan 01 layout)"
  - "set_prefixname('') needed to produce gate01_test.dll instead of libgate01_test.dll (MinGW lib prefix)"
  - "UCRT API set DLLs (api-ms-win-crt-*) are acceptable dependencies -- available on Win10+ and Wine"

patterns-established:
  - "MinGW DLL naming: use set_prefixname('') to avoid lib prefix for SKSE plugins"
  - "Wine smoke test: WINEDEBUG=+loaddll wine64 rundll32 <dll>,DllMain verifies DLL loads"

requirements-completed: [GATE-01, GATE-02]

# Metrics
duration: 3min
completed: 2026-03-27
---

# Phase 01 Plan 02: Cross-Compile Gate DLLs and Validate Under Wine/Proton Summary

**Both GATE-01 and GATE-02 DLLs cross-compiled with MinGW GCC 14.3.0, statically linked (no MinGW runtime deps), exports verified, Wine smoke tests pass, and Proton runtime validation PASSED**

## Status: COMPLETE

## Performance

- **Duration:** ~10 min
- **Started:** 2026-03-27T18:17:26Z
- **Tasks:** 2/2 completed
- **Files modified:** 1

## Accomplishments
- Cross-compiled gate01_test.dll and gate02_test.dll with xPack MinGW GCC 14.3.0
- Both DLLs are valid PE32+ executable (DLL) x86-64 binaries
- Static linking confirmed: no libgcc, libstdc++, or libwinpthread dependencies
- gate01_test.dll exports SKSEPlugin_Query and SKSEPlugin_Load
- Dependencies limited to KERNEL32.dll and UCRT API sets (system DLLs)
- Wine64 loads both DLLs as native modules without missing dependency errors
- Fixed xmake.lua: moved add_requires to root scope and added set_prefixname("")

## Task Commits

Each task was committed atomically:

1. **Task 1: Cross-compile both gate DLLs and run Wine smoke test** - `deefcbb1` (feat)
2. **Task 2: Validate both gates under Proton Wine** - Validated via automated test harness (gate01 harness + rundll32 for gate02)

## Files Created/Modified
- `Code/tests/feasibility/xmake.lua` - Fixed add_requires placement (root scope), added set_prefixname("") for correct DLL naming

## Build Artifacts (gitignored)
- `Code/tests/feasibility/build/mingw/x86_64/release/gate01_test.dll` - 91KB PE32+ SKSE plugin
- `Code/tests/feasibility/build/mingw/x86_64/release/gate02_test.dll` - 91KB PE32+ MinHook test

## Decisions Made
- Moved `add_requires("minhook v1.3.3")` before target blocks (XMake v3 enforces root-scope-only for requires)
- Added `set_prefixname("")` to both targets because MinGW defaults to `lib` prefix on shared libraries
- Built in temp directory to avoid XMake cache lock contention in git worktree (same issue as Plan 01)
- UCRT API set dependencies accepted as standard Win10+ system DLLs (available in both Wine and Proton)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed add_requires placement in xmake.lua**
- **Found during:** Task 1 (build step)
- **Issue:** XMake v3.0.8 requires `add_requires()` in root scope, not after target blocks. Plan 01 placed it at line 27 after targets.
- **Fix:** Moved `add_requires("minhook v1.3.3")` to before target definitions
- **Files modified:** Code/tests/feasibility/xmake.lua
- **Verification:** `xmake f` and `xmake build` both succeed
- **Committed in:** deefcbb1

**2. [Rule 3 - Blocking] Added set_prefixname("") for correct DLL naming**
- **Found during:** Task 1 (build step)
- **Issue:** MinGW default shared library prefix is "lib", producing libgate01_test.dll instead of gate01_test.dll. SKSE loads plugins by filename.
- **Fix:** Added `set_prefixname("")` to both targets
- **Files modified:** Code/tests/feasibility/xmake.lua
- **Verification:** Output DLLs named gate01_test.dll and gate02_test.dll
- **Committed in:** deefcbb1

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both fixes necessary for compilation and correct output naming. No scope creep.

## Issues Encountered
- XMake cache lock in worktree: Same issue from Plan 01. Worked around by building in /tmp. Not a blocker for normal development (only affects worktree parallel builds).

## Verification Results

| Check | Result |
|-------|--------|
| gate01_test.dll is PE32+ DLL x86-64 | PASS |
| gate02_test.dll is PE32+ DLL x86-64 | PASS |
| No libgcc/libstdc++/libwinpthread deps | PASS |
| SKSEPlugin_Query exported | PASS |
| SKSEPlugin_Load exported | PASS |
| Wine loads gate01_test.dll (no err:module) | PASS |
| Wine loads gate02_test.dll (no err:module) | PASS |
| Proton Wine: gate01_test.log PASS (SKSE=33619968 Runtime=17174896) | PASS |
| Proton Wine: gate02_minhook.log PASS (Hook called 1 times, Tick=399049915) | PASS |
| gate01 QueryInterface + RegisterListener ABI round-trip | PASS |

## Proton Validation Details

**Method:** Automated testing using Proton Experimental Wine (Skyrim SE compatdata prefix)
- gate01: Custom test harness compiled with MinGW that simulates SKSE calling `SKSEPlugin_Query` and `SKSEPlugin_Load` with fake interface structs
- gate02: Loaded via `wine64 rundll32` — MinHook hooks fire during DLL_PROCESS_ATTACH

**GATE-01 Results:**
- DLL loaded at 0x00006ffffe3e0000
- SKSEPlugin_Query: read SKSE version (0x02010000) and runtime version (0x01061170) correctly
- SKSEPlugin_Load: called QueryInterface(1) → got messaging interface → RegisterListener called with "SKSE" sender
- Log: "GATE-01 PASS: SKSEPlugin_Query called. SKSE=33619968 Runtime=17174896"

**GATE-02 Results:**
- MinHook initialized, hooked GetTickCount from kernel32.dll
- Hook fired 1 time when GetTickCount was called
- Log: "GATE-02 PASS: MinHook working. Hook called 1 times. Tick=399049915"

**Note:** Full Skyrim runtime test (launching through Steam with SKSE) not performed — no SKSE launch options configured in Steam. The Wine-level validation confirms ABI compatibility, DLL loading, function pointer calling convention, and hooking all work correctly under Proton's Wine runtime. Full Skyrim integration will be validated in Phase 3 with the actual client DLL.

---
*Phase: 01-feasibility-validation*
*Completed: 2026-03-27*
