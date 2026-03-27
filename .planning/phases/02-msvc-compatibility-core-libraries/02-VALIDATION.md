---
phase: 2
slug: msvc-compatibility-core-libraries
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-27
---

# Phase 02 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 |
| **Config file** | Code/tests/xmake.lua |
| **Quick run command** | `xmake build <target>` (per-library build check) |
| **Full suite command** | `xmake f -p linux && xmake build -g Server && xmake run TPTests` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build <target>` for affected library
- **After every plan wave:** Run `xmake f -p linux && xmake build -g Server && xmake run TPTests`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 02-01-01 | 01 | 1 | BUILD-02 | build | `xmake build TiltedCore` (native Linux) | N/A (build) | pending |
| 02-01-02 | 01 | 1 | BUILD-02 | build | `xmake build SkyrimEncoding` (native Linux) | N/A (build) | pending |
| 02-02-01 | 02 | 2 | BUILD-03 | build | `xmake build CommonLib BaseLib` (native Linux) | N/A (build) | pending |
| 02-02-02 | 02 | 2 | BUILD-03 | build | `xmake build SkyrimCoopNetworking` (native Linux) | N/A (build) | pending |
| 02-03-01 | 03 | 3 | BUILD-03 | build | `xmake build SkyrimTogetherServer` (MinGW + native Linux) | N/A (build) | pending |
| 02-03-02 | 03 | 3 | BUILD-03 | unit | `xmake run TPTests` (native Linux) | Code/tests/encoding.cpp | pending |

*Status: pending / green / red / flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/xmake.lua` — Update TPTests deps to use local TiltedCore target (not package)
- [ ] `Code/tests/xmake.lua` — Add rpmalloc dependency if not transitively provided
- [ ] Verify native Linux platform: `xmake f -p linux` configures without errors

*Wave 0 items are prerequisites resolved in the first plan.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| MinGW cross-compile | BUILD-03 | Requires xPack MinGW toolchain | `xmake f -p mingw --mingw=$XPACK_PATH && xmake build -g Server` |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
