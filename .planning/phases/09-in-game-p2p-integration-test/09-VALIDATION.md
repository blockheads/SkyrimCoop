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
| **Config file** | tests/test_ingame_sync.sh (primary), Code/tests/integration/xmake.lua (fake_player) |
| **Quick run command** | `xmake run fake_player --self-test && xmake run TPTests` |
| **Full suite command** | `./tests/test_ingame_sync.sh` |
| **Estimated runtime** | ~180-300 seconds (requires Skyrim launch) |

---

## Sampling Rate

- **After every task commit:** Run `xmake run fake_player --self-test && xmake run TPTests`
- **After every plan wave:** Run `./tests/test_ingame_sync.sh`
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds (quick), 300 seconds (full)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 09-01-01 | 01 | 1 | D-02 | integration | `grep -q 'FAKE_PLAYER_DEFAULT=true' tests/test_ingame_sync.sh` | ✅ | ⬜ pending |
| 09-01-02 | 01 | 1 | D-04-3 | integration | `grep -q 'wait_for_gameserver_port' tests/test_ingame_sync.sh` | ❌ W0 | ⬜ pending |
| 09-01-03 | 01 | 1 | D-09/D-10 | integration | `grep -q 'TEST_RUN_DIR' tests/test_ingame_sync.sh` | ❌ W0 | ⬜ pending |
| 09-02-01 | 02 | 1 | D-05 | integration | `xmake run fake_player --self-test` | ❌ W0 | ⬜ pending |
| 09-03-01 | 03 | 2 | D-06 | integration | `test -f tests/test_two_player.sh` | ❌ W0 | ⬜ pending |
| 09-03-02 | 03 | 2 | D-06 | code | `grep -q 'JoinClient\|join_client' Code/native_client/main.cpp` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/test_ingame_sync.sh` — GameServer port readiness check function
- [ ] `tests/test_ingame_sync.sh` — Log collection to temp directory
- [ ] `Code/tests/integration/fake_player.cpp` — /proc/pid/mem verification in RunExternal()
- [ ] `tests/test_two_player.sh` — New script for two-instance testing
- [ ] `Code/native_client/main.cpp` — Join mode enet6 client implementation

*Existing infrastructure covers unit test and self-test paths.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Two Skyrim instances render remote player | D-06 | Requires visual inspection of game window | Launch two instances, verify both show remote player character in Riverwood |
| Host player movement visible to join client | D-06 | Game state observation | Move host character, observe join client's screen for corresponding movement |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 300s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
