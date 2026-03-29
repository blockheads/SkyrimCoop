---
phase: 11
slug: json-wire-protocol
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-03-29
---

# Phase 11 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Catch2 2.13.9 (existing) + shell integration tests |
| **Config file** | Code/tests/CMakeLists.txt / xmake.lua |
| **Quick run command** | `xmake build TPTests && ./build/release/tests/TPTests` |
| **Full suite command** | `xmake build TPTests && ./build/release/tests/TPTests && bash tests/test_ingame_sync.sh` |
| **Estimated runtime** | ~15 seconds |

---

## Sampling Rate

- **After every task commit:** Run `xmake build TPTests && ./build/release/tests/TPTests`
- **After every plan wave:** Run full suite command
- **Before `/gsd:verify-work`:** Full suite must be green
- **Max feedback latency:** 15 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|-----------|-------------------|-------------|--------|
| TBD | TBD | TBD | TBD | TBD | TBD | TBD | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] JSON serialization/deserialization unit tests for each hook opcode
- [ ] NDJSON line framing unit tests (partial reads, multiple messages per recv)
- [ ] Dirty-check cache unit tests (position epsilon, cache miss on change)

*Existing Catch2 infrastructure covers framework needs.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| DLL-to-native JSON round-trip under Wine | Full protocol integration | Requires running Skyrim under Proton with relay DLL injected | Deploy DLL + native client, verify JSON messages flow via log inspection |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 15s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
