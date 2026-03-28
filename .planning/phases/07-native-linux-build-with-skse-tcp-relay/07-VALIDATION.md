---
phase: 07
slug: native-linux-build-with-skse-tcp-relay
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-28
---

# Phase 07 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 (existing TPTests) + bash smoke tests |
| **Config file** | `Code/tests/xmake.lua` |
| **Quick run command** | `xmake build TPTests && xmake run TPTests` |
| **Full suite command** | `xmake build TPTests && xmake run TPTests && bash tests/smoke_tcp_relay.sh` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build TPTests && xmake run TPTests`
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 07-01-01 | 01 | 1 | TCP protocol | unit | `xmake run TPTests "TCPRelay"` | ❌ W0 | ⬜ pending |
| 07-01-02 | 01 | 1 | Binary framing | unit | `xmake run TPTests "BinaryFrame"` | ❌ W0 | ⬜ pending |
| 07-02-01 | 02 | 1 | DLL hook trampoline | smoke | `bash tests/smoke_hook_dll.sh` | ❌ W0 | ⬜ pending |
| 07-03-01 | 03 | 2 | proc_mem reader | unit | `xmake run TPTests "ProcMem"` | ❌ W0 | ⬜ pending |
| 07-03-02 | 03 | 2 | ptrace SEIZE | smoke | `bash tests/smoke_ptrace.sh` | ❌ W0 | ⬜ pending |
| 07-04-01 | 04 | 3 | Service rewrite | integration | `xmake run TPTests "NativeServices"` | ❌ W0 | ⬜ pending |
| 07-05-01 | 05 | 4 | End-to-end relay | smoke | `bash tests/smoke_tcp_relay.sh` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/TCPRelayTests.cpp` — unit tests for binary framing protocol
- [ ] `Code/tests/ProcMemTests.cpp` — unit tests for /proc/pid/mem reader
- [ ] `tests/smoke_tcp_relay.sh` — end-to-end TCP relay smoke test
- [ ] `tests/smoke_ptrace.sh` — ptrace SEIZE validation on current kernel
- [ ] `tests/smoke_hook_dll.sh` — DLL hook trampoline compilation check

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| DLL loads in Skyrim under Proton | Runtime validation | Requires Skyrim + SKSE | Load skyrim_coop_hooks.dll via SKSE, check log for TCP server startup |
| Native process reads game memory | Requires live game | /proc/pid/mem needs Skyrim running | Start Skyrim, verify native process reads player position |
| Hook events forward over TCP | Requires live hooks | MinHook hooks need real game | Move player, verify CharacterService receives position updates |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
