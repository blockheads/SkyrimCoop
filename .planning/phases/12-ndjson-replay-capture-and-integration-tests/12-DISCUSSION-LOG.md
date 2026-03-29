# Phase 12: NDJSON Replay Capture and Integration Tests - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-29
**Phase:** 12-ndjson-replay-capture-and-integration-tests
**Areas discussed:** Replay harness design, Capture workflow, Integration test strategy, Regression detection

---

## Replay Harness Design

### Q1: How should the replay harness run?

| Option | Description | Selected |
|--------|-------------|----------|
| Replay mode in skyrim-coop | Add --replay flag to existing native client binary. Reuses DispatchHookEvent and services, skips TCP/GameReader init. | ✓ |
| Standalone replay binary | Separate binary (ndjson-replay) with its own main(). Cleaner separation but duplicates init code. | |
| Test-only (no standalone tool) | Replay logic only inside test binaries. Not usable for manual debugging. | |

**User's choice:** Replay mode in skyrim-coop
**Notes:** None

### Q2: How should replay timing work?

| Option | Description | Selected |
|--------|-------------|----------|
| Fast-forward by default | Blast through events as fast as possible. --real-time flag for debugging. | ✓ |
| Real-time by default | Respect recorded timestamps. --fast flag to skip timing. | |
| Configurable speed multiplier | Replay at Nx speed. | |

**User's choice:** Fast-forward by default
**Notes:** User initially asked "why fast forward by default?" — explained that CI/regression is the primary use case where speed matters. Real-time is the minority debugging case. User agreed after clarification.

### Q3: How should deltaTime be handled in replay?

| Option | Description | Selected |
|--------|-------------|----------|
| Synthetic fixed delta | Fixed 1/60s deltaTime for every Update() call. Deterministic. | ✓ |
| Recorded timestamps | Add timestamps to NDJSON, use actual time deltas. More realistic but non-deterministic. | |

**User's choice:** Synthetic fixed delta
**Notes:** None

---

## Capture Workflow

### Q4: How should captured NDJSON sessions be stored?

| Option | Description | Selected |
|--------|-------------|----------|
| tests/fixtures/ in repo | Curated golden files checked in. Small, focused captures. | ✓ |
| Generated on demand | No golden files. CI runs live capture then replays. | |
| External storage (git-lfs) | Large raw captures stored outside repo. | |

**User's choice:** tests/fixtures/ in repo
**Notes:** None

### Q5: How should raw recordings become curated fixtures?

| Option | Description | Selected |
|--------|-------------|----------|
| Manual trim + commit | Record live, trim with grep/head/tail, commit. Human judgment. | ✓ |
| Script-assisted curation | Helper script for filtering by op/time/formId. | |
| Hand-written fixtures | Write NDJSON by hand. Precise but tedious. | |

**User's choice:** Manual trim + commit
**Notes:** None

---

## Integration Test Strategy

### Q6: What should integration tests assert?

| Option | Description | Selected |
|--------|-------------|----------|
| Service state assertions | Check service/registry state after replay: entities, positions, counts. Catch2 test cases. | ✓ |
| Output capture comparison | Compare output NDJSON against baseline. Snapshot-testing style. | |
| Log-based assertions | Assert on spdlog output. Fragile but easy. | |

**User's choice:** Service state assertions
**Notes:** None

### Q7: How should replay be exposed to the test framework?

| Option | Description | Selected |
|--------|-------------|----------|
| ReplayHarness helper class | Class that sets up stubs, replays file, exposes registry for assertions. | ✓ |
| Free function API | Simple RunReplay(path) -> registry function. | |
| Reuse --replay from main binary | Shell out and check exit code. No programmatic access. | |

**User's choice:** ReplayHarness helper class
**Notes:** None

### Q8: Which services should have replay tests?

| Option | Description | Selected |
|--------|-------------|----------|
| Character + Position only | Start with CharacterService. Other services in future phases. | ✓ |
| All 8 services | Comprehensive but large scope. | |
| Character + Combat + Weather | Three most complex services. | |

**User's choice:** Character + Position only
**Notes:** None

---

## Regression Detection

### Q9: How should replay tests detect regressions?

| Option | Description | Selected |
|--------|-------------|----------|
| Hardcoded assertions | Explicit expected values in test cases. Regression = test failure. | ✓ |
| Snapshot comparison | Dump registry state, compare against baseline. | |
| Both | Assertions + snapshots. | |

**User's choice:** Hardcoded assertions
**Notes:** None

### Q10: Should replay tests run in CI as same or separate step?

| Option | Description | Selected |
|--------|-------------|----------|
| Same test binary | ReplayTests Catch2 binary alongside existing tests. One CI step. | ✓ |
| Separate CI step | Own CI job after main suite. | |

**User's choice:** Same test binary
**Notes:** None

---

## Claude's Discretion

- How to stub GameReader and PointerTable in replay mode
- ReplayHarness internal architecture
- Whether --replay and ReplayHarness share code
- Fixture file format conventions
- How to handle unknown/malformed ops in replay

## Deferred Ideas

None — discussion stayed within phase scope
