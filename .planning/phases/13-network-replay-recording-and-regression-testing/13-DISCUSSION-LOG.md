# Phase 13: Network Replay Recording and Regression Testing - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-29
**Phase:** 13-network-replay-recording-and-regression-testing
**Areas discussed:** Service coverage scope, Regression workflow, Recording & fixtures, Assertion depth

---

## Service Coverage Scope

| Option | Description | Selected |
|--------|-------------|----------|
| All 7 remaining (Recommended) | Weather, Calendar, Quest, Combat, Inventory, ActorValue, Magic. Harness already instantiates them. | ✓ |
| High-value subset (3-4) | Focus on Combat, Inventory, ActorValue, Magic. Defer lower-risk services. | |
| Combat + Inventory only | Smallest useful expansion — most likely sync bug sources. | |

**User's choice:** All 7 remaining
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Extract from live session (Recommended) | Grep/filter the 45k-line recording for per-service fixtures. | ✓ |
| New focused recordings | Record new sessions per service. Requires game access. | |
| Both — extract now, record later | Extract for immediate coverage, record focused sessions as follow-up. | |

**User's choice:** Extract from live session
**Notes:** None

---

## Regression Workflow

| Option | Description | Selected |
|--------|-------------|----------|
| Hardcoded assertions (Recommended) | Explicit REQUIRE/CHECK with expected values. Regression = test failure. | ✓ |
| Snapshot baselines | State dump compared against baseline files. Regressions show as diffs. | |
| Hybrid | Hardcoded for critical invariants, snapshot for detailed state. | |

**User's choice:** Hardcoded assertions
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, gate CI (Recommended) | ReplayTests must pass. Failure blocks merges. | ✓ |
| Run but don't gate | CI runs tests, reports results, doesn't block. | |
| No CI yet | Local-only for now. | |

**User's choice:** Yes, gate CI
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Catch2 defaults (Recommended) | Standard failure messages with expected vs actual. | ✓ |
| Event-level trace on failure | Dump dispatched event sequence on failure. | |
| Full state dump on failure | Serialize entire registry to JSON on failure. | |

**User's choice:** Catch2 defaults
**Notes:** None

---

## Recording & Fixtures

| Option | Description | Selected |
|--------|-------------|----------|
| Keep both (Recommended) | Keep live_session.ndjson and per-service slices. Full file for integration tests. | ✓ |
| Slices only, remove live file | Extract needed data, delete original. | |
| Keep but don't test against it | Keep as reference, only test slices. | |

**User's choice:** Keep both
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Update fixtures with code (Recommended) | Update fixture files when protocol changes. Same PR. | |
| Version tag in fixtures | protocol_version field, harness adapts. | |
| Regenerate from game | Re-record when protocol changes. | |

**User's choice:** Other — "Fixtures should come from the game, doesn't make sense to modify them"
**Notes:** Fixtures are game recordings, not hand-written artifacts. They represent real game output.

| Option | Description | Selected |
|--------|-------------|----------|
| Re-record from game | Protocol changes = old recordings obsolete. Record new session. | |
| Harness handles both formats | Detect old vs new format. More code complexity. | |
| Accept test breakage as signal | Broken tests tell you which fixtures need re-recording. | ✓ |

**User's choice:** Accept test breakage as signal
**Notes:** None

---

## Assertion Depth

| Option | Description | Selected |
|--------|-------------|----------|
| Structural + key values (Recommended) | Entities with correct components + 1-2 key values per service. | ✓ |
| Existence only | Just verify entities/components created and cleaned up. | |
| Full state validation | Assert every field in every component. Maximum coverage, maximum brittleness. | |

**User's choice:** Structural + key values
**Notes:** None

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, one full-session test (Recommended) | Replay full recording, assert aggregate state. Catches cross-service bugs. | ✓ |
| No, per-service slices only | Keep tests focused and fast. | |
| Full session as optional/slow test | Tag as [slow] for separate CI run. | |

**User's choice:** Yes, one full-session test
**Notes:** None

---

## Claude's Discretion

- How to extract per-service fixtures from live_session.ndjson
- Which specific key values to assert per service
- Full-session test Catch2 tags
- ReplayHarness getter additions for per-service state
- Test file organization

## Deferred Ideas

- **Server-side ENet message recording and replay** — User explicitly requested as a new phase after Phase 13. Different data flow (GameServer ↔ client), different protocol, different harness.
