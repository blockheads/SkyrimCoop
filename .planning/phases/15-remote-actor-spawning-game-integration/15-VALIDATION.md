---
phase: 15
slug: remote-actor-spawning-game-integration
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-29
---

# Phase 15 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 |
| **Config file** | Code/tests/main.cpp (Catch2 header-only) |
| **Quick run command** | `./build/linux/x86_64/releasedbg/ReplayTests "[Spawn]"` |
| **Full suite command** | `./build/linux/x86_64/releasedbg/ReplayTests` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `./build/linux/x86_64/releasedbg/ReplayTests "[Spawn]" "[Remove]" "[SpawnResult]"`
- **After every plan wave:** Run `./build/linux/x86_64/releasedbg/ReplayTests`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 10 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 15-01-01 | 01 | 1 | P15-01 | unit | `./build/.../ReplayTests "[SpawnCmd]"` | ❌ W0 | ⬜ pending |
| 15-01-02 | 01 | 1 | P15-02 | unit | `./build/.../ReplayTests "[RemoveCmd]"` | ❌ W0 | ⬜ pending |
| 15-02-01 | 02 | 1 | P15-03 | unit | `./build/.../ReplayTests "[SpawnResult]"` | ❌ W0 | ⬜ pending |
| 15-02-02 | 02 | 1 | P15-04 | unit | `./build/.../ReplayTests "[ServerIdMap]"` | ❌ W0 | ⬜ pending |
| 15-03-01 | 03 | 2 | P15-05 | integration | `./build/.../ReplayTests "[SpawnFlow]"` | ❌ W0 | ⬜ pending |
| 15-04-01 | 04 | 2 | P15-06 | manual-only | Manual: run script, check INI | N/A | ⬜ pending |
| 15-05-01 | 05 | 3 | P15-07 | manual-only | Manual: run two-player test | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/spawn_command_tests.cpp` — stubs for P15-01, P15-02 (JSON parse to CommandSlot)
- [ ] `Code/tests/spawn_result_tests.cpp` — stubs for P15-03, P15-04 (spawn_result event handling, serverId map)
- [ ] `Code/tests/replay/spawn_flow_tests.cpp` — stubs for P15-05 (end-to-end spawn message flow)

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| INI patching in launch scripts | P15-06 | Shell script modification, requires actual Skyrim INI files | Run launch script, verify bAlwaysActive=1 and bFull Screen=0 in Skyrim.ini |
| In-game actor spawn via DLL | P15-07 | Requires Skyrim running under Proton with relay DLL loaded | Run two-player test, verify remote player spawns as NPC in-game |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 10s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
