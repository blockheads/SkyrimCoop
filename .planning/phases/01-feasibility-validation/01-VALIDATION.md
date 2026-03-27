---
phase: 1
slug: feasibility-validation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-27
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Manual runtime validation (log file inspection) + XMake build success |
| **Config file** | `Code/tests/feasibility/xmake.lua` (Wave 0 creates) |
| **Quick run command** | `xmake f -p mingw --mingw=$XPACK_PATH && xmake build gate01_skse_plugin` |
| **Full suite command** | Build both gates + deploy to Skyrim + verify log files |
| **Estimated runtime** | ~30 seconds (build) + ~60 seconds (runtime validation) |

---

## Sampling Rate

- **After every task commit:** Run `xmake build` for the relevant target
- **After every plan wave:** Build both gates + attempt Wine DLL load test
- **Before `/gsd:verify-work`:** Both DLLs load in Skyrim under Proton, log files confirm success
- **Max feedback latency:** 30 seconds (build time)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 01-01-01 | 01 | 1 | BUILD-01 | smoke | `xmake f -p mingw --mingw=$XPACK_PATH -y && echo PASS` | Wave 0 | pending |
| 01-02-01 | 02 | 1 | GATE-01 | manual (runtime) | `xmake build gate01_skse_plugin` (build only) | Wave 0 | pending |
| 01-03-01 | 03 | 1 | GATE-02 | manual (runtime) | `xmake build gate02_minhook` (build only) | Wave 0 | pending |

*Status: pending / green / red / flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/feasibility/xmake.lua` — build config for test DLLs
- [ ] `Code/tests/feasibility/gate01_skse_plugin/main.cpp` — GATE-01 test plugin
- [ ] `Code/tests/feasibility/gate02_minhook/main.cpp` — GATE-02 test plugin
- [ ] XMake installation on developer machine
- [ ] Skyrim SE + SKSE installation verification

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| MinGW DLL loads in Skyrim under Proton, exercises SKSE APIs | GATE-01 | Requires live Skyrim process + SKSE runtime | 1. Copy DLL to Skyrim Data/SKSE/Plugins/ 2. Launch Skyrim via Proton 3. Check SKSE log for plugin output |
| MinHook hook fires from MinGW DLL in Skyrim | GATE-02 | Requires live Skyrim process to hook functions | 1. Copy DLL to Skyrim Data/SKSE/Plugins/ 2. Launch Skyrim via Proton 3. Check log for hook fire confirmation |

---

## Validation Sign-Off

- [ ] All tasks have automated verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
