---
phase: 14
slug: enet6-network-traffic-replay
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-29
---

# Phase 14 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 |
| **Config file** | Code/tests/replay/xmake.lua |
| **Quick run command** | `xmake build ReplayTests && xmake run ReplayTests` |
| **Full suite command** | `xmake build ReplayTests && xmake run ReplayTests` |
| **Estimated runtime** | ~5 seconds |

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
| TBD | TBD | TBD | TBD | unit/integration | `xmake build ReplayTests && xmake run ReplayTests` | ⬜ | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- Existing infrastructure covers all phase requirements. ReplayTests target already links SkyrimTogetherServer and SkyrimEncoding.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Live recording produces valid NDJSON | N/A | Requires running Skyrim + connecting client | Run host+client session with --record-enet flag, verify NDJSON files written |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
