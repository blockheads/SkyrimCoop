---
phase: 3
slug: client-dll-cross-compilation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-27
---

# Phase 3 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 + shell smoke tests |
| **Config file** | `Code/tests/xmake.lua` |
| **Quick run command** | `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient` |
| **Full suite command** | `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient && xmake build TPTests && xmake run TPTests` |
| **Estimated runtime** | ~120 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient`
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green + smoke test pass
- **Max feedback latency:** 120 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| 03-01-01 | 01 | 1 | BUILD-04 | build | `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient` | N/A | ⬜ pending |
| 03-01-02 | 01 | 1 | BUILD-04 | build | `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient` | N/A | ⬜ pending |
| 03-02-01 | 02 | 2 | BUILD-04 | build | `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient` | N/A | ⬜ pending |
| 03-02-02 | 02 | 2 | BUILD-04 | build | `xmake f -p mingw --mingw=/usr && xmake build SkyrimTogetherClient` | N/A | ⬜ pending |
| 03-03-01 | 03 | 3 | BUILD-05 | smoke | `./tests/smoke_test_client.sh` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/smoke_test_client.sh` — smoke test script for DLL load + server connect verification
- [ ] Build verification: `xmake build SkyrimTogetherClient` under MinGW must succeed before smoke testing

*Existing infrastructure covers unit test requirements (Catch2/TPTests from Phase 2).*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| DLL loads via SKSE under Proton | BUILD-05 | Requires Skyrim SE installation + Proton runtime | 1. Copy DLL to SKSE plugins dir, 2. Launch Skyrim via Proton, 3. Check SKSE log for plugin load, 4. Verify no crash within 30s |
| Client connects to local server | BUILD-05 | Requires running game instance | 1. Start SkyrimTogetherServer locally, 2. Launch Skyrim with client DLL, 3. Use connect command/UI, 4. Check server log for player connection |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 120s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
