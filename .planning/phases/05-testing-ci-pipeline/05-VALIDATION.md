---
phase: 5
slug: testing-ci-pipeline
status: draft
nyquist_compliant: true
wave_0_complete: true
created: 2026-03-28
---

# Phase 5 -- Validation Strategy

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
| 05-01-01 | 01 | 1 | TEST-01 | build | `xmake build SkyrimTogetherServer && xmake build TPTests && xmake run TPTests` | yes | pending |
| 05-01-02 | 01 | 1 | TEST-01 | build | `xmake build SkyrimTogetherServer && xmake build TPTests && xmake run TPTests` | yes | pending |
| 05-02-01 | 02 | 2 | TEST-02, TEST-03 | build | `xmake build TPTests` | W0 | pending |
| 05-02-02 | 02 | 2 | TEST-02, TEST-03 | integration | `xmake run TPTests "[NetworkIntegration],[NetworkRoundTrip]"` | W0 | pending |
| 05-02-03 | 02 | 2 | D-02 | integration | `xmake run TPTests "[RelayIntegration]"` | W0 | pending |
| 05-03-01 | 03 | 3 | D-03 | integration | `xmake build fake_player && ./build/linux/x86_64/release/fake_player --self-test` | W0 | pending |
| 05-03-02 | 03 | 3 | D-03 | integration | `tests/test_ingame_sync.sh --check-only` | W0 | pending |
| 05-03-03 | 03 | 3 | D-03 | checkpoint | Human verification of in-game sync | N/A | pending |

*Status: pending -- green -- red -- flaky*

---

## Wave 0 Requirements

- [ ] `Code/tests/mock_client.h` -- MockClient class for headless server connection (plan 05-02, task 1)
- [ ] `Code/tests/mock_client.cpp` -- MockClient implementation (plan 05-02, task 1)
- [ ] `Code/tests/server_roundtrip_tests.cpp` -- TEST-03 message round-trip tests, tag `[NetworkRoundTrip]` (plan 05-02, task 2)
- [ ] `Code/tests/mock_client_tests.cpp` -- TEST-02 headless handshake tests, tag `[NetworkIntegration]` (plan 05-02, task 2)
- [ ] `Code/tests/relay_integration_tests.cpp` -- D-02 relay integration tests, tag `[RelayIntegration]` (plan 05-02, task 3)
- [ ] Update `Code/tests/xmake.lua` -- add SkyrimTogetherServer dep and native_client includes (plan 05-02, task 1)
- [ ] `Code/tests/integration/fake_player.cpp` -- D-03 fake player binary (plan 05-03, task 1)
- [ ] `Code/tests/integration/xmake.lua` -- fake_player build target (plan 05-03, task 1)
- [ ] `tests/test_ingame_sync.sh` -- D-03 in-game sync test script (plan 05-03, task 2)

*Existing Catch2 framework and relay_protocol_tests.cpp cover unit test needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Fake player sees remote actor in Skyrim memory | D-03 (stretch) | Requires live Skyrim + Wine/Proton | Launch Skyrim via Proton, host game, run fake player harness, verify /proc/pid/mem reads |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references
- [x] No watch-mode flags
- [x] Feedback latency < 30s
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** pending execution
