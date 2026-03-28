---
phase: 5
slug: testing-ci-pipeline
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-28
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 (existing TPTests) |
| **Config file** | Code/tests/xmake.lua |
| **Quick run command** | `xmake build TPTests && xmake run TPTests` |
| **Full suite command** | `xmake build TPTests && xmake run TPTests` |
| **Estimated runtime** | ~10 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build TPTests && xmake run TPTests`
- **After every plan wave:** Run `xmake build TPTests && xmake run TPTests`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 05-01-01 | 01 | 1 | TEST-01 | build | `xmake build SkyrimTogetherServer 2>&1; echo $?` | ✅ | ⬜ pending |
| 05-02-01 | 02 | 2 | TEST-02 | integration | `xmake run TPTests "MessageRoundTrip"` | ❌ W0 | ⬜ pending |
| 05-03-01 | 03 | 3 | TEST-03 | integration | `xmake run TPTests "FakePlayerConnect"` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/MessageRoundTrip.cpp` — stubs for TEST-02 message serialization tests
- [ ] `Code/tests/FakePlayerConnect.cpp` — stubs for TEST-03 fake player integration test
- [ ] Update `Code/tests/xmake.lua` — add server library dependencies for integration tests

*Existing Catch2 framework covers unit test needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Fake player sees remote actor in Skyrim memory | TEST-03 (stretch) | Requires live Skyrim + Wine/Proton | Launch Skyrim via Proton, host game, run fake player harness, verify /proc/pid/mem reads |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
