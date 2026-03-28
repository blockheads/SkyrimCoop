# Phase 9: In-Game P2P Integration Test - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-28
**Phase:** 09-in-game-p2p-integration-test
**Areas discussed:** Test orchestration, Success criteria, Failure diagnostics, Scope boundary

---

## Test Orchestration

| Option | Description | Selected |
|--------|-------------|----------|
| Automated script | Shell script handles full sequence: INI inject, deploy DLL, launch Skyrim, wait for native, run fake_player, collect results | |
| Fully manual runbook | Step-by-step doc, run each command manually | |

**User's choice:** User noted that test_ingame_sync.sh already exists and asked if it could be fully automated. Confirmed it already launches Skyrim via Proton automatically.

**Follow-up: Script approach**

| Option | Description | Selected |
|--------|-------------|----------|
| Update existing script | Extend test_ingame_sync.sh: make --with-fake-player default, add GameServer readiness, add assertions | ✓ |
| Fresh script | New test_p2p_integration.sh from scratch | |

**User's choice:** User initially wasn't sure if the existing script works. Agreed to fix-and-validate approach — run it, fix what breaks iteratively.

| Option | Description | Selected |
|--------|-------------|----------|
| Fix-and-validate | Run existing script, identify breakage at each stage, fix iteratively | ✓ |
| Rewrite then validate | Rewrite from scratch based on Phase 7-8 behavior | |

---

## Success Criteria

| Option | Description | Selected |
|--------|-------------|----------|
| All 5 stages | DLL → relay → GameServer → fake_player auth → movement broadcast | ✓ |
| Stages 1-4 (auth only) | Skip movement verification | |
| Stages 1-3 (server starts) | Just prove GameServer boots | |

**User's choice:** All 5 stages must pass.

**Follow-up: /proc/pid/mem verification**

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, verify via /proc/pid/mem | Prove full loop: game state → hooks → server → fake_player reads memory | ✓ |
| No, network-level only | ServerReferencesMoveRequest receipt is sufficient | |
| You decide | Claude picks | |

**User's choice:** Yes — full /proc/pid/mem verification for ultimate proof.

---

## Failure Diagnostics

| Option | Description | Selected |
|--------|-------------|----------|
| Log collection to temp dir | Test run directory with redirected logs, stage-specific failure messages, --keep-logs option | ✓ |
| Inline stderr only | Natural stderr output, terminal is the diagnostic | |
| You decide | Claude picks | |

**User's choice:** Log collection to temp directory.

**Follow-up: Timeout**

| Option | Description | Selected |
|--------|-------------|----------|
| 5 minutes total | 120s boot + 90s server + 30s fake_player with buffer | |
| 10 minutes total | More generous for slow machines | |
| You decide | Claude picks based on existing stage timeouts | ✓ |

---

## Scope Boundary

| Option | Description | Selected |
|--------|-------------|----------|
| Host + fake_player only | One real Skyrim + scripted client | |
| Host + fake_player + second Skyrim | Two real instances + fake_player | ✓ |
| Just make the script pass | Whatever it takes for exit 0 | |

**User's choice:** Two real Skyrim instances plus fake_player. User noted "running two skyrims isn't hard, you just boot up two instances." Wants automated quick-start for two-client testing.

**Follow-up: Second client stack**

| Option | Description | Selected |
|--------|-------------|----------|
| Same stack, join mode | Second Skyrim also loads relay DLL → native process with --connect | ✓ |
| Vanilla + fake_player only | Second Skyrim just observes, fake_player does protocol work | |

**Follow-up: Instance configuration**

| Option | Description | Selected |
|--------|-------------|----------|
| Environment variable override | DLL reads SKYRIM_COOP_CONNECT env var per instance | |
| Separate Proton prefixes | Each instance gets own STEAM_COMPAT_DATA_PATH | |
| You decide | Claude picks simplest approach for Wine/Proton | ✓ |

---

## Claude's Discretion

- Total test timeout selection
- Host vs join instance configuration mechanism
- /proc/pid/mem assertion specifics
- Deploy step inclusion in script
- Log format and directory naming
- Two-Skyrim test: separate script or mode flag

## Deferred Ideas

None — discussion stayed within phase scope.
