---
phase: 09-in-game-p2p-integration-test
plan: 01
subsystem: testing
tags: [integration-test, multi-bot, stress-test, enet6, MockClient, headless]

# Dependency graph
requires:
  - phase: 05-testing-framework
    provides: "MockClient, fake_player binary, GameServer test infrastructure"
  - phase: 08-p2p-embedded-gameserver
    provides: "skyrim-coop --host-only embedded GameServer"
provides:
  - "Multi-bot stress test tool (--bots N concurrent MockClient threads)"
  - "Broadcast verification (--verify-broadcast)"
  - "Metrics CSV output (--metrics)"
  - "BroadcastTrackingClient subclass counting ServerReferencesMoveRequest opcodes"
affects: [09-in-game-p2p-integration-test, ci-pipeline]

# Tech tracking
tech-stack:
  added: []
  patterns: ["BroadcastTrackingClient pattern for opcode-counting in tests", "Per-thread rpmalloc init for multi-threaded test workers"]

key-files:
  created: []
  modified: ["Code/tests/integration/fake_player.cpp"]

key-decisions:
  - "BroadcastTrackingClient inherits MockClient, overrides OnConsume to count ServerReferencesMoveRequest opcodes"
  - "Broadcast verification gracefully handles headless mode (no character assignment = no broadcasts expected)"
  - "Per-bot phase offset (id * 2.0f seconds) for distinct walk patterns"

patterns-established:
  - "Multi-threaded bot workers with BotContext struct for per-bot atomic stats"
  - "Graceful verification: hard-require auth, soft-check broadcasts (headless-aware)"

requirements-completed: [D-02, D-05]

# Metrics
duration: 3min
completed: 2026-03-29
---

# Phase 09 Plan 01: Multi-Bot Stress Test Summary

**Extended fake_player with --bots N concurrent MockClient threads, broadcast verification, and CSV metrics output against embedded GameServer**

## Performance

- **Duration:** 3 min
- **Started:** 2026-03-29T00:11:10Z
- **Completed:** 2026-03-29T00:14:35Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments
- fake_player supports --bots N, --verify-broadcast, --metrics, --tick-rate flags
- BroadcastTrackingClient subclass counts ServerReferencesMoveRequest opcodes per bot
- 4 bots connect, authenticate, and send 25 moves each (5s at 5Hz) against --host-only without crash
- CSV metrics output: per-bot moves_sent, broadcasts_received, connect_time_ms
- Self-test mode unchanged and backwards compatible

## Task Commits

Each task was committed atomically:

1. **Task 1: Add multi-bot and metrics support to fake_player** - `0ea8429f` (feat)
2. **Task 2: Validate multi-bot headless test against --host-only** - validation only, no code changes

## Files Created/Modified
- `Code/tests/integration/fake_player.cpp` - Extended with BotContext, BroadcastTrackingClient, RunMultiBot(), --bots/--verify-broadcast/--metrics/--tick-rate CLI flags

## Decisions Made
- BroadcastTrackingClient inherits MockClient and overrides OnConsume to track ServerReferencesMoveRequest opcodes via m_lastOpcode check after base class parsing
- Broadcast verification is headless-aware: requires all bots authenticated (hard), reports broadcast counts but does not fail on zero (expected without character assignment)
- Bot worker threads use per-thread rpmalloc_thread_initialize/finalize for allocator safety

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- fake_player multi-bot mode ready for CI integration
- Broadcast counts will be non-zero when used with a real Skyrim host that triggers character assignment
- Ready for Plan 02 (test_ingame_sync.sh enhancements)

---
*Phase: 09-in-game-p2p-integration-test*
*Completed: 2026-03-29*

## Self-Check: PASSED
