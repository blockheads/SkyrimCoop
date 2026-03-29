---
phase: 9
slug: in-game-p2p-integration-test
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-28
---

# Phase 9 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Bash scripts + fake_player C++ binary + Catch2 unit tests |
| **Config file** | tests/ci_test.sh (CI), tests/test_ingame_sync.sh (golden), Code/tests/integration/xmake.lua |
| **Quick run command** | `xmake run fake_player --self-test && xmake run TPTests` |
| **Full suite command** | `tests/ci_test.sh` (headless) or `tests/test_ingame_sync.sh` (requires Skyrim) |
| **Estimated runtime** | ~30s (CI headless), ~300s (golden with Skyrim) |

---

## Sampling Rate

- **After every task commit:** Run `xmake run fake_player --self-test && xmake run TPTests`
- **After every plan wave:** Run `tests/ci_test.sh` (headless multi-bot + mock relay)
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds (CI), 300 seconds (golden)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 09-01-01 | 01 | 1 | D-02 | unit | `grep -q 'BotContext' Code/tests/integration/fake_player.cpp` | ❌ W0 | ⬜ pending |
| 09-01-02 | 01 | 1 | D-02 | integration | `xmake build fake_player && xmake run fake_player --self-test` | ✅ | ⬜ pending |
| 09-02-01 | 02 | 1 | D-03 | integration | `grep -q 'mock_skyrim_loader' Code/tests/integration/xmake.lua` | ❌ W0 | ⬜ pending |
| 09-03-01 | 03 | 2 | D-04 | integration | `grep -q 'wait_for_gameserver_port' tests/test_ingame_sync.sh` | ❌ W0 | ⬜ pending |
| 09-03-02 | 03 | 2 | D-15 | integration | `grep -q 'setup_test_logs' tests/test_ingame_sync.sh` | ❌ W0 | ⬜ pending |
| 09-03-03 | 03 | 2 | D-06 | code | `grep -q 'class JoinClient' Code/native_client/main.cpp` | ❌ W0 | ⬜ pending |
| 09-04-01 | 04 | 3 | D-08 | script | `test -x tests/launch_two_skyrim.sh` | ❌ W0 | ⬜ pending |
| 09-05-01 | 05 | 3 | D-05 | integration | `test -x tests/ci_test.sh` | ❌ W0 | ⬜ pending |
| 09-05-02 | 05 | 3 | D-07 | manual | `echo "requires Skyrim"` | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/integration/fake_player.cpp` — BotContext struct and RunMultiBot() for --bots mode
- [ ] `Code/tests/integration/mock_skyrim_loader.cpp` — New file: MinGW binary loading relay DLL
- [ ] `tests/test_ingame_sync.sh` — GameServer port readiness, log collection, default fake_player
- [ ] `Code/native_client/main.cpp` — JoinClient class for join mode
- [ ] `tests/launch_two_skyrim.sh` — New file: two-instance developer tool
- [ ] `tests/ci_test.sh` — New file: CI test runner

*Existing infrastructure covers unit test and self-test paths.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Full 5-stage pipeline with real Skyrim | D-07 | Requires game launch under Proton | Run test_ingame_sync.sh, all 5 stages PASS |
| Two Skyrim instances see each other | D-08 | Visual verification of remote player | Run launch_two_skyrim.sh, move host character, observe join window |
| Remote player animations sync | P2P-02 | Requires human perception | Walk, attack, crouch in both windows |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 30s (CI) / 300s (golden)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
