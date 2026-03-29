---
phase: 09-in-game-p2p-integration-test
plan: 08
status: partial
started: 2026-03-28T21:00:00Z
completed: 2026-03-28T21:20:00Z
---

## Summary

Deployed fixed binaries (relay DLL + native client) and ran diagnostic test. The test revealed that ALL hook event pointers from the DLL are Wine-internal heap addresses (0x20xxxxxxxxx to 0x86xxxxxxxxx range) that are NOT mapped in the Linux process page table. pread() fails with EIO/EINVAL for every actor pointer because these addresses are outside the Linux canonical virtual address space.

## Key Findings

- Essential-only hooks (09-07): Skyrim stayed running through Riverwood cell load — hook stability fix WORKS
- Wine address diagnostic (09-06): Memory map loaded successfully, but no hook pointers match any mapped region
- formId reads: 100% failure rate — every single ReadFormId returns 0 because the pointer addresses are unmapped
- Root cause identified: DLL sends raw Wine heap pointers over TCP, but native Linux process can't read Wine-internal memory via /proc/pid/mem

## Root Cause Analysis

The HookEventPacket.args[] contain raw `reinterpret_cast<uint64_t>(apThis)` values from inside the Wine process. These are Wine's virtual memory addresses for Skyrim game objects (Actor/Character instances). While Wine maps PE modules (like SkyrimSE.exe itself) into standard Linux address space, dynamically allocated game objects (heap) use Wine-internal memory management whose pages are NOT visible in `/proc/pid/maps`.

Representative pointer patterns:
- `0x20df6c1000000000` — Wine heap, EIO errno 5
- `0x85d37c3000000000` — Wine heap, EINVAL errno 22
- `0x86f4a74000000000` — Wine heap, EINVAL errno 22

All addresses end in `000000000` (36 zero bits) suggesting the actual pointer might be in the upper bits — possible struct packing issue in HookEventPacket serialization.

## Outcome

- **Check A (pread smoke test):** Not visible in logs (binary may not have had 09-09 changes)
- **Check B (formId reads):** ALL FAIL — 0% success rate across hundreds of actors
- **Check C (Skyrim stability):** PASS — Skyrim survived through Riverwood load with essential-only hooks
- **DOD gate:** NOT MET — two-instance sync not attempted due to single-instance diagnostic failure

## Architectural Fix Required

The DLL must read game data (formId, position, etc.) INSIDE the Wine process and send VALUES over TCP, not pointers. The DLL already runs inside SkyrimSE.exe's address space and can directly dereference `*(uint32_t*)(actorPtr + 0x14)`.

## Self-Check: PARTIAL

- [x] Both binaries compile and deploy
- [x] Hook stability verified (essential-only mode works)
- [ ] formId reads succeed (BLOCKED — Wine heap not accessible via /proc/pid/mem)
- [ ] Two-instance actor sync (BLOCKED — depends on formId reads)

## key-files

### created
(none — diagnostic-only plan)

### modified
(none — diagnostic-only plan)
