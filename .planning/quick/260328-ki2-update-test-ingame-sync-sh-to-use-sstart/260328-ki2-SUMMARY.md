---
phase: quick
plan: 260328-ki2
subsystem: testing
tags: [integration-test, skyrim-ini, automation]
key-files:
  modified:
    - tests/test_ingame_sync.sh
decisions:
  - "sStartingCell=Riverwood chosen as it loads fast and is the default new-game area"
  - "Backup-then-restore approach over sed-only for safety"
metrics:
  duration: 90s
  completed: 2026-03-28
---

# Quick Plan 260328-ki2: Update test_ingame_sync.sh to Use sStartingCell

INI injection of sStartingCell=Riverwood for fully automated Skyrim cell load in integration tests

## What Was Done

### Task 1: Add sStartingCell INI injection with cleanup and increased timeouts

**Commit:** `9c837fae`

Added three capabilities to `tests/test_ingame_sync.sh`:

1. **INI injection** (`inject_starting_cell()`): Backs up Skyrim.ini, removes any existing `sStartingCell=` line, then injects `sStartingCell=Riverwood` after the `[General]` section header. This causes Skyrim to boot directly into Riverwood cell without menu interaction.

2. **INI restore** (`restore_skyrim_ini()`): Restores from backup if available, otherwise falls back to sed removal of the injected line. Called from `cleanup()` trap so it runs on success, failure, and signal exit.

3. **Timeout increases**: `TIMEOUT_SERVER` 30 -> 90 seconds, `TIMEOUT_SKYRIM` 60 -> 120 seconds. Full cell load via sStartingCell takes longer than bare process startup.

4. **Prerequisite check** (1h): Verifies `Skyrim.ini` exists before test runs.

5. **--check-only safety**: The injection call is placed after the `check_only` early-exit guard, so `--check-only` never touches the INI file.

## Deviations from Plan

None - plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED
