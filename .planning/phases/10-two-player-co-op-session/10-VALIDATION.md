---
phase: 10
slug: two-player-co-op-session
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-03-28
updated: 2026-03-28
---

# Phase 10 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 + bash integration scripts |
| **Config file** | Code/tests/ (Catch2 header-only) |
| **Quick run command** | `xmake build TPTests && ./build/release/tests/TPTests` |
| **Full suite command** | `xmake build TPTests && ./build/release/tests/TPTests && tests/test_ingame_sync.sh` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build SkyrimCoopHooksDLL SkyrimCoopNative TPTests && ./build/release/tests/TPTests`
- **After every plan wave:** Run `xmake build TPTests && ./build/release/tests/TPTests && tests/test_ingame_sync.sh`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 10-01-00 | 01 | 0 | D-04 | scaffold | `xmake build TPTests` | Code/tests/router_test.cpp | ⬜ pending |
| 10-01-01 | 01 | 1 | D-03, D-04 | unit | `./build/release/tests/TPTests "[router]"` | Code/tests/router_test.cpp | ⬜ pending |
| 10-01-02 | 01 | 1 | D-01, D-03 | build | `xmake build SkyrimCoopNative` | N/A | ⬜ pending |
| 10-01-03 | 01 | 1 | D-02, D-06 | build | `xmake build SkyrimCoopNative` | N/A | ⬜ pending |
| 10-02-01 | 02 | 1 | D-11 | build | `xmake build SkyrimCoopHooksDLL` | N/A | ⬜ pending |
| 10-02-02 | 02 | 1 | D-07, D-08 | build + manual | `xmake build SkyrimCoopHooksDLL` + visual verification | N/A | ⬜ pending |
| 10-03-01 | 03 | 2 | D-10, D-11, D-12 | syntax | `bash -n tests/launch_two_player.sh` | N/A | ⬜ pending |
| 10-04-01 | 04 | 3 | D-16, D-19 | manual | Visual verification by user | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [x] `Code/tests/router_test.cpp` — unit tests for ServerMessageRouter with AssignCharacter/CharacterSpawn/NotifyRemove (created in Plan 01 Task 0)
- [ ] Integration: no automated test for two-instance sync (manual verification per D-20)

*Wave 0 test file is created as Plan 01 Task 0, before the handlers are implemented (RED phase).*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| CMD_SET_POSITION execution | D-07, D-08 | Requires Skyrim running under Proton with DLL loaded | Load Skyrim, connect, verify character position updates |
| Movement sync between instances | D-16, D-19 | Requires two Skyrim instances running simultaneously | Launch both instances with launch script, walk in Riverwood, observe other player moving |
| Character visibility | D-05, D-06 | Requires visual confirmation of NPC spawn | Connect client, verify remote character model appears in game |

*These manual verifications constitute the phase gate per D-20.*

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 30s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved (post-revision)
