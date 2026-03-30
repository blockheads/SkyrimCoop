---
phase: 1
slug: abi-safety-and-source-cleanup
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-30
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 (header-only) |
| **Config file** | none |
| **Quick run command** | `xmake f -p mingw` |
| **Full suite command** | `xmake f -p mingw` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake f -p mingw`
- **After every plan wave:** Run `xmake f -p mingw`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 01-01-01 | 01 | 1 | BUILD-02 | build smoke | `xmake f -p mingw` | N/A | ⬜ pending |
| 01-01-02 | 01 | 1 | BUILD-02 | build smoke | `xmake f -p mingw` | N/A | ⬜ pending |
| 01-01-03 | 01 | 1 | BUILD-02 | build smoke | `xmake f -p mingw` | N/A | ⬜ pending |
| 01-02-01 | 02 | 1 | BUILD-03 | build smoke | `xmake f -p mingw` | N/A | ⬜ pending |
| 01-02-02 | 02 | 1 | BUILD-03 | build smoke | `xmake f -p mingw` | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Existing infrastructure covers all phase requirements. Phase 1 is pure cleanup verified by build success — no test files need to be created.

---

## Manual-Only Verifications

All phase behaviors have automated verification (build commands).

---

## Validation Sign-Off

- [x] All tasks have automated verify (build smoke tests)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
