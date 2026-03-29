---
phase: 12
slug: ndjson-replay-capture-and-integration-tests
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-29
---

# Phase 12 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 (header-only) |
| **Config file** | Code/tests/main.cpp (Catch2 entry point) |
| **Quick run command** | `xmake build ReplayTests && xmake run ReplayTests` |
| **Full suite command** | `xmake build ReplayTests && xmake run ReplayTests` |
| **Estimated runtime** | ~2 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build ReplayTests && xmake run ReplayTests`
- **After every plan wave:** Run `xmake build ReplayTests && xmake run ReplayTests`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 12-01-01 | 01 | 1 | — | build | `xmake build ReplayTests` | ❌ W0 | ⬜ pending |
| 12-01-02 | 01 | 1 | — | unit | `xmake run ReplayTests "ReplayHarness"` | ❌ W0 | ⬜ pending |
| 12-02-01 | 02 | 2 | — | integration | `xmake run ReplayTests "actor_spawn"` | ❌ W0 | ⬜ pending |
| 12-02-02 | 02 | 2 | — | integration | `xmake run ReplayTests "movement_sync"` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/replay/replay_harness.h` — ReplayHarness class
- [ ] `Code/tests/replay/replay_tests.cpp` — Catch2 test cases
- [ ] `tests/fixtures/replay/` — Golden NDJSON fixture files
- [ ] ReplayTests xmake target — new build target linking Catch2 + native_client services

*Existing Catch2 infrastructure covers test framework needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| `--replay` flag in skyrim-coop binary | D-01 | Requires built binary + fixture file | Run `./skyrim-coop --replay tests/fixtures/replay/actor_spawn.ndjson` and verify clean exit |
| `--real-time` flag timing | D-02 | Timing-dependent | Run with `--real-time` and observe ~real-time pacing |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
