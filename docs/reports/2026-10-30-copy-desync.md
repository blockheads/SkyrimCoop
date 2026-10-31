# V1 copy desync fixes, 2026-10-30

Worktree `.worktrees/v1-copy-desync`, branch `v1-copy-desync`, based on dev `6088c863`.
All changes are uncommitted; no merge into dev. Shared CHAT/DUO coordination remains in the main checkout.
Our games are stopped, verified exited and the reservation released before every offline investigation,
build, GameTests run, unit test and `scripts/check.sh`. The duo rule is also saved in the user's persistent
Codex instructions.

## Final verification

`scripts/check.sh` passes **618 C++ cases / 6,402 assertions**, harness lifecycle/comparison/baseline/ragdoll
Python checks, UI fixtures, Windows plugin and launcher (`build/replay-weapon-check.log`).
`git diff --check` passes. No routine net_check run.

The final combined Skyrim run completes **105 unique scenarios: 87 PASS / 18 FAIL**, in **1,441.8s**.
It covers the union of dialogue/combat/ai/animation/movement/death tags and the explicit name/pool/torch cases.
`party-travel-dead-guest` runs last. The runner restarts only our pair after two broken baselines:
mounted-guest travel and follower-hireling. All games are stopped and the duo released afterward.
This is not a claim that the complete native suite is green.

## Requested faults and fast regressions

| Fault | Native fact and correction | RED before correction | Final native result |
| --- | --- | --- | --- |
| NPC action replay | Native idle queries change flags1→2 without performing. Capture remembers the original query flag. Independent clips can leave different active states; refused unpaired replays load the accepted action's resulting owner graph. Native action aftermath is called once with its native ABI. | `[replay-query]`, `[replay-graph]`, capture/recovery units; a negative control still fails with graph recovery enabled. | Anchor switch/save/trade, host load, both Beast Forms and both kill-move replay cases pass. Intermittent all-zero marker from the older batch did not recur; it remains open. |
| NPC weapon state | The owner finishes sheathing, but moveStart still carries old weapon bits5 in State2 `0x64E10A8`; replay and graph loading restore5 on a copy whose current state is0. Preserve current copy weapon bits while applying the other historical flags. Live weapon-choice messages retain authority. | Fake learns actor-state replay and graph headers; `[replay-weapon]` fails **5 != 0** through accepted replay and graph recovery; `[replay-state]` units fail3 assertions. | The original recruitment case no longer reports NPC weapon-state differences. It still fails on the revived guest player's lifted skeleton. |
| Leveled NPC names | The selected face is sent correctly; the host composite's resolved name had no saved section. Save the full-name section explicitly, decode on a disposable NPC and apply a reference name; keep the selected plugin base attached. | `[npc-choice]` created/placed/refresh/ordinary identity; explicit save-mask units. Later lifetime smoke fails4 assertions and name-section unit fails before removing attached private name bases. | Spawned Bandit Chief and placed guard names match. The placed scenario retains a travel lock report. No NPC name mismatch remains in recruitment. |
| Onmund magicka | Both permanent pools175; owner TEMPORARY+30 was missing. Snapshot/live temporary modifiers and permanent/temporary/damage restoration reproduce205. The host retains live modifiers for later joiners too. | `[fortified-pools]`, `[actor-pools]`, `[fortified-record]`, owner-only host record units. | Dedicated205 check and original dialogue-question-answered-by-who-can pass. |
| Armor after the guest's claw hand-in | Native rebuild leaves a partial worn set although items remain carried; incremental equips fail. Reset/reapply the mismatched whole worn set, preserving magic and items. The precise initiating script call is still unknown. | `[outfit-rebuild]`, `[equipment-repair]`; queued-arrow snapshot scheduling and slot-selection flows/units fail before their corrections. | Full guest claw hand-in, both quest-gold turn-ins, trade and controlled Aela torch repair pass without diagnostic repair commands. |

The name fix first used attached private NPCs. Native mounted travel repeatedly crashed in GetObstacle
(`19564`, `2DD84C`). Graph capture OFF and equipment-reset OFF controls still crashed; main-DLL and
name-overlay OFF controls completed. Allocation/destructor probes identify Mudcrab's private base being
freed by ExtraLeveledCreature LoadGame (`172A20`), then read during actor GC. The copy now keeps its selected
plugin base; a disposable decoder never becomes an actor base. The correction passes name comparisons
and removes this crash. Final canonical crash logs predate the run (latest guest crash19:33; final run20:09–20:34).
Mounted travel still reports a separate player-body failure. Diagnostic probe source is removed.

## Smoke fidelity and observer corrections

Passing the old fake suite could miss the real failures: it replayed ordinary actions immediately, ignored
CopyPlayback and graph-job staging, and the actor observer omitted animations, NPC names and pools.

- `[replay-time]` and `[replay-staging]` fail before the fake uses the actual playback clock and shared production
  phase machine: variables, graph update, action, bounded retries and main-thread publication.
- `[replay-ready]` fails3 assertions before modeling attached3D with an unready graph and the native ten-update
  fallback for a ready graph without animation jobs.
- `[desync-checks]` fails on missing action/name/pool comparisons, then on independent head/pelvis readings,
  then on raw weapon state5 versus0. The observer now checks each; all carried/worn checks remain.
- The ordinary guest-talks flow scripts native query/actual gesture/stop through FakeDialogue. It compares all
  actors mid-conversation and before cleanup. Disabling graph recovery makes it fail on the old final gesture.
- Native camera control proves Lydia's head88/copy122 reading is a stale off-screen skeleton: looking at the
  owner changes head88→124 and pelvis52→71 with identical JumpLand and equal scale. Graph reload does not fix
  that reading. `[culled-pose]` and3 comparison units fail first. NPC bones compare only with visibility on both
  games; visible pose faults, off-screen action/name/pool/gear checks and player-body scope remain.
  Final metadata calibration confirms owner83/cull6/frustum=false versus copy122/cull7/frustum=true.
- Native reference names use BSFixedString's case-insensitive pool. The fake learns first-pooled spelling;
  `[npc-name-pool]` and a Python unit fail before both observers use native name equality. A distinct
  Chief-versus-Outlaw regression still fails as intended. Tolerances and lock allowlists are unchanged.

The fake records supplied bone observations; it has no Havok/IK solver. It does not claim to model every
native failure below. Those remain open until their causal engine facts and failing gameplay flows exist.

## Retained final native failures

**2026-10-31 user decision: defer every remaining finding from this batch past v1.** The table and the
earlier intermittent zero marker/paired-refusal evidence remain recorded. These findings no longer block
v1 on their own; the recorded result stays87/105 and no assertions or allowlists are changed.
Post-v1 priorities are the lock-order reports and enemy-copy death delay, whose gameplay impact still
needs classification. A five-second death-check timeout does not establish a permanently surviving enemy;
the baseline position error is a cleanup symptom. The final run recorded no crash or deadlock.
The lingering scene warrants higher priority if it blocks ordinary dialogue/barter. Visual differences and
remaining diagnostic/fixture validation can stay lower priority. See the [backlog](../bugs.md#copy-desync-follow-ups).

| Scenario | Failure |
| --- | --- |
| mounted-guest-leaves | Strict lock17A479/E82682 versus E85437/16B61D. |
| party-travel-mounted-guest | Guest and its copy have zero-height body readings; baseline restart. No crash. |
| leveled-placed-npc-is-hosts | Name matches; strict lock17A479/E82682 versus E85437/16B61D. |
| actors-walk-after-arrival | Owner Dorthe moved100 units in2s; feet14. Skeleton gait still needs a visible-sample control. |
| guest-rides-across-border | Strict lock170402/1DDC3F versus61C130/61C249. |
| death-spectate-guest | Guest owner and copy both sunk22 units. |
| death-wipe-checkpoint | Strict lock3CDB45/182241 versus1823D0/3D1507. |
| kill-move-kills-guest | Guest owner/copy ragdolls do not rest; bone/body gaps. Separate kill-move replay cases pass. |
| greybeards-scene-casts-party | Setup requires the documented story save; this QASmoke batch cannot exercise it. |
| guest-cast-moves-host | Revived guest kneels, pelvis15 versus copy71. |
| follower-guest-recruits | Guest owner skeleton lifted about6954 units relative to its copy. NPC names/weapon states match. |
| follower-anchor-disconnect | Revived guest kneels, pelvis31 versus copy71. |
| follower-placed-anchor-saved | Revived guest kneels, pelvis34 versus copy71. |
| follower-housecarl | Revived guest kneels, pelvis38 versus copy71. |
| follower-hireling | Guest pose gap and kneeling cleanup; baseline restart. |
| follower-travels-with-anchor | Lydia facing differs26 degrees. |
| dialogue-guest-barter-earns-speech | Belethor scene owner0 versus copy817434. |
| party-travel-dead-guest | Enemy-copy death timeout5s; guest baseline position remains103 units away. Ran last. |

Revived-player body pose was previously excluded from v1 by the user; it remains visible in this report.
The Lydia facing and Belethor scene differences are still open. Earlier persistent zero-action evidence
is retained although it did not recur. Five terminal paired refusals remain logged: four kill moves and
HorseExit. An earlier wolf trace proves that duplicate half was already playing before its replay arrived;
it does not establish a blanket cause or closure for every paired refusal.

## Superseding three-risk investigation, 2026-10-31

The user reopened only lock-order reports, enemy-copy death delay and Belethor's lingering scene for v1.
The follow-up corrects worker load/alias scheduling, mounted-wrapper attribution, the stale death-test copy
handle, owner-scene refresh and guest barter close lifecycle. Gate629 cases/6462 assertions passes;
combined affected native acceptance20 PASS/2 FAIL401.5s and fresh barter/death5/5 PASS139.2s. The two
combined failures are retained separately (Sigurd registration, original Belethor animation marker).
All other deferred findings and the historical87/105 result above remain unchanged.
[Completed classifications, regressions and exact acceptance](2026-10-31-v1-three-risks.md).

## Evidence and integration scope

Final log: ignored `build/coop-test/final-weapon-105.log`; complete result strings:
`build/final-weapon-results.json`. Matching DLL/PDB and rotated plugin logs:
`build/final-weapon-native-evidence`. Exact comparison reads are in `build/coop-test/desync-samples.jsonl`;
additional later diagnostics in `magicka-at-failure.jsonl` can already have converged, so preserve the original failure.
The final evidence contains no later other-agent run at capture time.

Important RED logs: `npc-lifetime-red.log`, `npc-name-section-red.log`, `npc-pool-red.log`,
`npc-pool-comparison-red.log`, `replay-weapon-graph-red.log`, `replay-state-red.log` under build/.
Native lifetime probes and exact weapon samples: `build/clone-dtor-probe-evidence`,
`build/cleanup-probe-evidence`, `build/deer-replay-weapon-native.json`.

Earlier combined batches are historical:104 cases89/15; subsequent105 unique86/19; the name-reference build
completed90 cases80/10 before stopping for the weapon/pool facts. They do not replace the final105-case result.

Read-only harness additions capture template chains, value layers, worn stack flags, gait, visibility and
replay phase. Gait/runner lifecycle changes came from the other agent's shared harness work. Their ownership,
purse and party-item gameplay changes are not imported. Acceptance of the later combined dev build remains
necessary; this worktree's changes remain available for the user's review and commit.
