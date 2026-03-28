---
status: partial
phase: 03-client-dll-cross-compilation
source: [03-VERIFICATION.md]
started: 2026-03-28T03:30:00Z
updated: 2026-03-28T03:30:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. DLL Load in Skyrim Under Proton (BUILD-05 re-test)
expected: Run `tests/smoke_test_client.sh` from project root (auto-detects DLL). Script reports PE32+ x86-64 OK, deploys DLL to Data/SKSE/Plugins/, Skyrim SE launches under Proton via SKSE, client log shows plugin loaded within 90s, game does not crash within 30s of main menu. This re-tests the 0x3E6 blocker after Plan 05 fix.
result: [pending]

### 2. Server Connection + Character Sync
expected: With Skyrim loaded via the DLL: start a local SkyrimTogetherServer, connect from the in-game menu. Client log shows OnConnected or 'connected to server'. Server log shows 'player connected'. A second player character is visible in-game when a second client joins.
result: [pending]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Gaps
