---
phase: 09-in-game-p2p-integration-test
plan: 10
status: partial
started: 2026-03-28T21:00:00Z
completed: 2026-03-28T21:20:00Z
---

## Summary

Deployed fixed binaries with pread diagnostics (09-09) and ran human-verified test. The diagnostic output conclusively identifies the root cause: DLL hook event pointers are Wine-internal heap addresses not accessible via Linux /proc/pid/mem. The fix requires the DLL to read formIds inside Wine and send values, not pointers.

## Diagnostic Results

### Check A: pread smoke test
NOT VISIBLE in output — the deployed binary may not have included the 09-09 smoke test code. However, the failure pattern is conclusive without it.

### Check B: formId reads
ALL FAIL — hundreds of actor pointers tested, 0% success rate. Every pointer is in the 0x20xxxxxxxxx to 0x86xxxxxxxxx range, outside Linux canonical address space. Errors: EIO (errno 5) for 0x20-0x21 range, EINVAL (errno 22) for 0x85-0x86 range.

### Check C: Skyrim stability
PASS — Skyrim survived through Riverwood cell load. Essential-only hooks (09-07) prevent the crash.

### Step 5: Two-instance test
SKIPPED — single-instance diagnostics show all reads fail, two-instance test would also fail.

## Root Cause (Confirmed)

Wine heap allocations for Skyrim game objects are NOT mapped in Linux /proc/pid/maps. The DLL sends raw Wine-internal pointers (reinterpret_cast from hook arguments), but the native Linux process cannot read these via pread() on /proc/pid/mem.

**Fix:** Restructure DLL-to-native protocol so the DLL reads formId/position/health INSIDE the Wine process and sends the VALUES in HookEventPacket, not raw pointers.

## Self-Check: PARTIAL

- [x] Both binaries build and deploy
- [x] Diagnostic output clearly identifies root cause
- [ ] formId reads succeed (BLOCKED — architectural issue)
- [ ] Two-instance actor sync (BLOCKED — depends on formId reads)

## key-files

### created
(none)

### modified
(none)
