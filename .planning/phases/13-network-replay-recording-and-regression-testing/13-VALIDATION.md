---
phase: 13
slug: network-replay-recording-and-regression-testing
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-29
---

# Phase 13 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 (header-only) |
| **Config file** | `Code/tests/replay/xmake.lua` |
| **Quick run command** | `xmake build ReplayTests && xmake run ReplayTests` |
| **Full suite command** | `xmake build ReplayTests && xmake run ReplayTests` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build ReplayTests && xmake run ReplayTests`
- **After every plan wave:** Run `xmake build ReplayTests && xmake run ReplayTests`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 13-01-01 | 01 | 1 | D-01 | unit | `xmake build ReplayTests && xmake run ReplayTests` | ❌ W0 | ⬜ pending |
| 13-01-02 | 01 | 1 | D-09 | unit | `xmake build ReplayTests && xmake run ReplayTests` | ❌ W0 | ⬜ pending |
| 13-02-01 | 02 | 1 | D-06, D-10 | integration | `xmake build ReplayTests && xmake run ReplayTests "[Integration]"` | ❌ W0 | ⬜ pending |
| 13-02-02 | 02 | 1 | D-04 | CI gate | `tests/ci_test.sh` | ✅ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- Existing infrastructure covers all phase requirements. `ReplayTests` xmake target already exists from Phase 12. New test `.cpp` files are auto-discovered by `add_files("*.cpp")` glob.

---

## Manual-Only Verifications

*All phase behaviors have automated verification.*

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
