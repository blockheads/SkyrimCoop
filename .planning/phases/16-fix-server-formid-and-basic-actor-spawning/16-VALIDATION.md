---
phase: 16
slug: fix-server-formid-and-basic-actor-spawning
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-29
---

# Phase 16 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 (C++ unit/replay tests) |
| **Config file** | `Code/tests/xmake.lua` |
| **Quick run command** | `xmake build TPTests && ./build/linux/x86_64/release/TPTests` |
| **Full suite command** | `xmake build TPTests && ./build/linux/x86_64/release/TPTests` |
| **Estimated runtime** | ~5 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build TPTests && ./build/linux/x86_64/release/TPTests`
- **After every plan wave:** Run full suite
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 5 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 16-01-01 | 01 | 1 | SPAWN-08 | unit | `./build/linux/x86_64/release/TPTests "FormId routing"` | ❌ W0 | ⬜ pending |
| 16-01-02 | 01 | 1 | SPAWN-09 | replay | `./build/linux/x86_64/release/TPTests "Spawn pipeline"` | ❌ W0 | ⬜ pending |
| 16-02-01 | 02 | 1 | SPAWN-08 | unit | `./build/linux/x86_64/release/TPTests "TESNPC Create"` | ❌ W0 | ⬜ pending |
| 16-03-01 | 03 | 2 | SPAWN-10 | unit | `./build/linux/x86_64/release/TPTests "WorldStateSync"` | ❌ W0 | ⬜ pending |
| 16-03-02 | 03 | 2 | SPAWN-10 | replay | `./build/linux/x86_64/release/TPTests "Reconciliation"` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/spawn_routing_test.cpp` — stubs for SPAWN-08 (FormId routing logic)
- [ ] `Code/tests/replay/spawn_pipeline_test.cpp` — stubs for SPAWN-09 (spawn replay fixtures)
- [ ] `tests/fixtures/replay/spawn/` — NDJSON fixture directory for spawn scenarios
- [ ] `Code/tests/reconciliation_test.cpp` — stubs for SPAWN-10 (WorldStateSync reconciliation)

*Existing Catch2 + DualReplayHarness infrastructure covers framework needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Remote actor visible in-game | SPAWN-09 | Requires Skyrim + SKSE running under Wine/Proton | Host + client connect, verify actor spawns at correct position with correct race |
| AI suppression working | SPAWN-09 | Requires observing NPC behavior in-game | Spawned remote actor must not wander or engage in combat |
| Disconnect cleanup | SPAWN-10 | Requires live enet6 disconnect | Disconnect client, verify all remote actors removed from host's game |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 5s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
