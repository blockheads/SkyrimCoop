---
phase: 8
slug: p2p-embedded-gameserver
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-03-28
---

# Phase 8 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 (C++ unit tests) + bash integration tests |
| **Config file** | `Code/tests/xmake.lua` |
| **Quick run command** | `xmake build TPTests && xmake run TPTests` |
| **Full suite command** | `xmake build TPTests && xmake run TPTests && bash Code/tests/integration/test_ingame_sync.sh --with-fake-player` |
| **Estimated runtime** | ~30 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build TPTests && xmake run TPTests`
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 30 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 08-01-T1 | 08-01 | 1 | P2P-01, P2P-02 | Unit (Catch2) | `xmake build TPTests && xmake run TPTests "[Loopback]" -x` | Code/tests/loopback_transport_tests.cpp | ⬜ pending |
| 08-02-T1 | 08-02 | 2 | P2P-03 | Build | `xmake build SkyrimCoopNative` | Code/native_client/main.cpp | ⬜ pending |
| 08-02-T2 | 08-02 | 2 | P2P-04 | Build + Unit | `xmake build SkyrimCoopNative && xmake build TPTests && xmake run TPTests "[Loopback]" -x` | Code/native_client/Services/CharacterService.cpp | ⬜ pending |
| 08-03-T1 | 08-03 | 3 | P2P-01..04 | Build + Unit | `xmake build SkyrimCoopNative && xmake build fake_player && xmake run TPTests "[Loopback]" -x && xmake run TPTests` | Code/native_client/main.cpp | ⬜ pending |
| 08-03-T2 | 08-03 | 3 | P2P-01..04 | Manual (checkpoint) | Human runs fake_player against --host-only native process | N/A | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

*Existing infrastructure covers all phase requirements — fake_player, MockClient, and test_server_fixture.h already exist from Phase 5. The Catch2 test file (loopback_transport_tests.cpp) is created as part of 08-01-T1 (merged implementation + test task).*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| fake_player authenticates with embedded GameServer | P2P-01 (D-09 item 1) | Requires running native process + fake_player as separate processes | Run `--host-only`, connect fake_player, check "authentication accepted" |
| Movement broadcast between connections | P2P-02 (D-09 item 2) | Requires two enet6 connections active simultaneously | fake_player sends scripted movement, server broadcasts to loopback host |
| Host state visible to remote client | P2P-03 (D-09 item 3) | Requires game hook data or simulated host state | Verify fake_player receives player list / host character assignment |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 30s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** ready
