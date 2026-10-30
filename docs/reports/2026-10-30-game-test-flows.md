# Game-test flow refactor — 2026-10-30

Implemented in branch `test-flows`, worktree `.worktrees/test-flows`, from `c755e622`. No merge into `dev`.
The other agent's uncommitted main-checkout `copy_flows.cpp` and documents were left untouched. Unit-test
organization, production interfaces/behavior, snapshots, transport behavior and clocks are outside this batch.

## Result

- The Python CLI delegates to `coop_harness`. `Session` owns host/guest `Game` handles, fixtures, registry and failure
  context. Each game owns paths, display, held input and RPC connections. Scenario functions receive the session;
  they do not import the CLI or change a module-global current instance. Repository diagnostic/package callers
  use the new handles too.
- One registration carries name, tags, fixture, callable and description. Full and overlapping-tag discovery select
  each canonical flow once. The six equipment/draw/drift scenarios become three lifecycles without aliases.
- The runner owns setup, execution, comparison and cleanup phases. It saves failure evidence before restoration,
  preserves the original error alongside cleanup errors, and stops when baseline restoration fails, including a
  world difference left after cleanup. `--until-fail` retains snapshots/reports and skips batch restoration on failure.
- `Party.cpp` owns lifecycle/stepping/waiting, `Comparisons.cpp` owns comparisons, and `Fixtures` shares cell/player-copy
  lookup. Twenty-two identical `Plugins` implementations and all nine local `PlayerCopy` / four `PlayerCopies`
  implementations are replaced by shared helpers. Specialized fixtures keep their original cell IDs/state; twelve
  local `Plugins` helpers and the distinct title-world fixture remain.
- Fake scripts, objects, inventory, progress and physics implementation move into domain files without changing
  state or bridge behavior. Sequential inventory, cutscene and map-marker checkpoints eliminate repeated setups.
- All ten unchanged JSON specs are separate runtime-discovered Catch2 cases, with tags, timing and shard selection.
  Both runners attempt all cleanup steps after assertion failures. The Python runner also attempts every actor cleanup.
- [Testing](../design/testing.md) is the current guide. Historical narratives and the old twin table are preserved
  under [testing history](testing-history/2026-10-30-testing-guide-before-refactor.md).

## Coverage and measurements

[The ledger](2026-10-30-game-test-flow-coverage.json) maps every original source entry and every original Skyrim
registration. It distinguishes fake and native boundaries and records unmeasured native runtimes explicitly; this
batch did not run the full native suite.

| Equivalent workload | Before | After |
|---|---:|---:|
| Core C++ equipment/item cases | 10 cases | 7 cases |
| Party constructions in those cases | 17 | 7 |
| Core C++ runtime, median of five runs | 0.127 s | 0.063 s |
| Broader `[items],[equipment]` run, one sample | 1.405 s | 1.356 s |
| Skyrim equipment/draw/drift workload | 6 scenarios, 24.5 s | 3 flows, 18.0 s |
| Skyrim workload baseline restorations, including initial reset | 7 | 4 |
| Skyrim workload NPC spawns | 2 | 1 |
| Registered Skyrim flows | 295 | 292 |
| Discovered Catch2 game cases | 221 | 227 |

The C++ core workload is exactly the cases from `equipment_flows.cpp` and `item_flows.cpp`; setup counts follow
Catch2's original branch leaves versus sequential execution. Both versions were built from the task base/refactor
and run five times. The larger tagged workload also includes unchanged item cases, whose waits dominate its timing.
The case-count increase is nine additional individually discovered spec cases minus three consolidated C++ cases.

Native before/after runs used the same DLL and running duo. The before driver loaded the original scripts with
only paths redirected to this worktree; actions and assertions were unchanged. The after lifecycle adds explicit
owner equipment checks as well as the existing copy checks and retains input/animation waits.

## Verification

- `scripts/check.sh`: **PASS**, 577 C++ cases / 6,072 assertions, 30 Python harness/comparison/baseline checks,
  UI fixtures and Windows DLL/launcher builds. GameTests: 227 cases / 2,302 assertions, eight shards.
- Targeted combined fake domains: **40/40 PASS**, 166 assertions. Final inventory tag run: **19/19 PASS**, 82 assertions.
- Skyrim boot and original inventory workload: **6/6 PASS**. Consolidated inventory lifecycles: **3/3 PASS**.
- Skyrim retained smoke: **4/4 PASS**, 17.3 seconds: `session-roster`, `late-join-client`,
  `spec:leaver-cutscene-gives-back`, `spec:npc-disabled-copy-follows`.
- All discovery/tag selections are unique; removed scenario names are rejected. Discovery and spec dry runs work
  without games. `affected --check` covers all 1,487 source files, including 42 previously unmapped sources.
- Python ownership checks cover independent game paths/connections/input, saved-story fixture rejection, custom
  package prefixes, stale status, duplicate registration/selection, checkpoint evidence, phase attribution, original
  error preservation, cleanup failures, `--until-fail`, and stopping an unrestorable batch.
- A normalized audit covers 1,590 original wait/check/failure sites. Every gameplay expression survives; two changed
  infrastructure expressions identify unknown names through the registry and missing games through a handle.
  Twenty-one pure functions match the original AST exactly; `is_close` retains the original renamed function body.
  Actor sampling is separated from the unchanged pairing/comparison logic.
- Negative control: disabling the existing `InventoryService::CarryForEquip` fix makes the retained outfit regression
  fail; restoring it passes. No production diff remains.
- Negative control: disabling the inventory-delta comparison fix makes the existing world comparison check fail;
  restoring it passes.
- A temporary JSON spec added after the binary build is discovered without rebuilding. Its fatal action plus two
  fatal cleanup steps all appear as three failures, proving later cleanup still executes. The temporary spec is removed.

The initial copied main-checkout binary produced two failures in an uncommitted placed-NPC deletion section absent
from the task base. Those raw logs are retained, but are not used as the controlled baseline. Early refactor gates
also caught my shared-cell extraction mistake in the title-world fixture; its original `0x30A4F` is restored through
an explicit fixture parameter. No failure was silenced or comparison relaxed. The final gate and targeted native
runs above are green. Broader Skyrim gameplay was intentionally not rerun.

Duo reserved 11:35–11:38 on displays `:99` / `:100`, with isolated prefixes and individually linked shared CHAT/DUO
files. Own games stopped, zero Skyrim processes verified, reservation released, own waiting entry removed.
No `net_check.sh` or full native suite was run.

## Evidence

Compact review artifacts are in [the evidence directory](2026-10-30-test-flows-evidence/validation.json): gate output,
core timing samples/case names, native summaries, assertion audit, discovery and regression-control output. Complete
logs remain in `.worktrees/test-flows/build/coop-test/` (`native-*.log`, `*-control-*.log`, `check-final.log`,
`inventory-core-*.xml`, `timing-before.xml`). The coverage ledger retains original source locations and check text.
