# Phase 5: Testing & CI Pipeline - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 05-testing-ci-pipeline
**Areas discussed:** Mock client scope, CI strategy, Fuzz testing, CI triggers, In-game verification, Server cleanup

---

## Mock Client Scope

| Option | Description | Selected |
|--------|-------------|----------|
| Test relay protocol only | DLL↔native TCP communication | |
| Test game server only | native↔embedded server networking | |
| Test both | Full pipeline: relay + game server | ✓ |

**User's choice:** Both — test the TCP relay protocol AND the game server networking
**Notes:** Both are part of the live pipeline and need validation

---

## CI Pipeline

| Option | Description | Selected |
|--------|-------------|----------|
| Full CI setup | GitHub Actions with DLL + native builds + tests | |
| Skip for now | Defer CI to later phase | ✓ |

**User's choice:** Skip — CI is not the priority, focus on automated testing
**Notes:** User explicitly said "we don't need CI yet"

---

## Fuzz Testing

| Option | Description | Selected |
|--------|-------------|----------|
| Include fuzz testing | Malformed messages, connection abuse | |
| Defer to later | Can do later | ✓ |

**User's choice:** Defer
**Notes:** "skip this for now, can do later"

---

## Standalone Server Removal

**User's choice:** Remove all standalone/dedicated server logic
**Notes:** "there should only be an embedded server now (host mode), let's remove any other logic. We will only support p2p, this is old code that hasn't been deleted yet."

---

## In-Game Verification Approach

| Option | Description | Selected |
|--------|-------------|----------|
| Manual verification | Human checks if things work | |
| Automated with fake player | Test harness as robot second player with memory verification | ✓ |
| Two real Skyrim instances | Not feasible for automation | |

**User's choice:** Automated fake player with controllable movements and auto-verification via /proc/pid/mem
**Notes:** "Ideally there would be some way to 'control' the other player, and to auto-verify their data lines up. I was hoping for something automated."

---

## Claude's Discretion

- Test framework choices for integration tests
- Whether fake player harness is a separate binary or mode of skyrim-coop
- Priority ordering of plans
- Test layering approach (lightweight first, then integration)

## Deferred Ideas

- CI pipeline — separate phase
- Fuzz testing — future phase
- Two real Skyrim instances — not feasible, fake player is the approach
