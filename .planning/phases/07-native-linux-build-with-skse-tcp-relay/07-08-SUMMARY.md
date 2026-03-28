---
phase: 07-native-linux-build-with-skse-tcp-relay
plan: 08
subsystem: testing
tags: [smoke-test, ptrace, tcp-relay, mingw, linux, skse]

# Dependency graph
requires:
  - phase: 07-native-linux-build-with-skse-tcp-relay/07
    provides: "CharacterService rewrite, InterpolationSystem, AnimationSystem, 8 native services"
provides:
  - "Smoke test scripts validating full relay architecture builds and unit tests pass"
  - "ptrace environment validation script"
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns: ["xmake project root resolution for worktree-safe build artifact discovery"]

key-files:
  created:
    - tests/smoke_tcp_relay.sh
    - tests/smoke_ptrace.sh
  modified: []

key-decisions:
  - "Catch2 comma-separated tag syntax for OR-matching multiple test groups"
  - "xmake show projectdir parsing for worktree-safe build artifact location"

patterns-established:
  - "Smoke test pattern: build both platforms, validate exports, run unit tests, check formatting"

requirements-completed: []

# Metrics
duration: 5min
completed: 2026-03-28
status: complete
---

# Phase 07 Plan 08: Integration Smoke Tests and Human Verification Summary

**Smoke test scripts validating DLL builds (MinGW), native binary builds (Linux), SKSE symbol exports, 23 relay unit tests (905 assertions), and ptrace environment -- checkpoint pending for human DLL-load verification in Skyrim**

## Performance

- **Duration:** 5 min (Task 1 only -- checkpoint pending for Task 2)
- **Started:** 2026-03-28T16:51:44Z
- **Completed:** In progress (awaiting human verification)
- **Tasks:** 1/2 (Task 2 is checkpoint:human-verify)
- **Files modified:** 2

## Accomplishments
- Created smoke_ptrace.sh: validates ptrace_scope, runs ProcMem unit tests, tests /proc/pid/mem readability
- Created smoke_tcp_relay.sh: builds DLL (MinGW) and native binary (Linux), checks SKSEPlugin_Version and SKSEPlugin_Load exports, runs all 23 relay unit tests (905 assertions pass), clang-format check
- Fixed Catch2 tag filtering (comma-separated OR syntax vs space-separated AND)
- Added xmake project root resolution for worktree-safe build artifact discovery

## Task Commits

Each task was committed atomically:

1. **Task 1: Create smoke tests for TCP relay and ptrace access** - `2aa25c15` (test)

2. **Task 2: Human verification of relay architecture** - `8fc970a9` (fix) — DLL loads in Skyrim under Proton, 31/31 hooks installed, native process connects via TCP, all 8 services run. Required fixing: file-based logging, relay info file handoff (Wine can't CreateProcess ELF), and Address Library v2 parser (missing variable-length name field + wrong delta encoding).

## Files Created/Modified
- `tests/smoke_ptrace.sh` - Validates ptrace environment (scope, /proc/pid/mem access, ProcMem unit tests)
- `tests/smoke_tcp_relay.sh` - Full relay build and test validation (MinGW DLL, Linux native, SKSE exports, unit tests, clang-format)

## Decisions Made
- Used comma-separated Catch2 tag syntax for OR-matching: `[RelayProtocol],[CommandQueue],[PointerTable],[ProcMem]`
- Parse xmake projectdir from `xmake show` output (strip ANSI codes) for worktree-safe artifact discovery
- clang-format check is warning-only (not blocking) since clang-format may not be installed

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed Catch2 tag filter syntax**
- **Found during:** Task 1 (smoke test creation)
- **Issue:** Plan specified space-separated tags which Catch2 interprets as AND (no tests matched)
- **Fix:** Changed to comma-separated tags for OR matching
- **Files modified:** tests/smoke_tcp_relay.sh
- **Verification:** 23 test cases (905 assertions) now run and pass
- **Committed in:** 2aa25c15

**2. [Rule 3 - Blocking] Fixed build artifact path resolution for worktrees**
- **Found during:** Task 1 (smoke test creation)
- **Issue:** `find build` searched worktree's build/ dir, but xmake outputs to project root's build/
- **Fix:** Parse xmake projectdir from `xmake show` and search that build directory
- **Files modified:** tests/smoke_tcp_relay.sh
- **Verification:** DLL and native binary found correctly
- **Committed in:** 2aa25c15

---

**Total deviations:** 2 auto-fixed (1 bug, 1 blocking)
**Impact on plan:** Both fixes necessary for smoke tests to actually validate builds. No scope creep.

## Issues Encountered
- clang-format not installed on build machine -- check produces WARNING but does not fail (by design)

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Awaiting human verification (Task 2 checkpoint) to confirm DLL loads in Skyrim under Proton
- All automated validation passes: both binaries build, SKSE exports present, 905 unit test assertions pass
- Phase 7 completion depends on this human verification step

---
*Phase: 07-native-linux-build-with-skse-tcp-relay*
*Status: Complete — human verified, all fixes committed*
