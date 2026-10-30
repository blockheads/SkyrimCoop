# V1 mod-profile investigation and merged-build acceptance

Current assessment: **131 pass / 24 fail / 0 not run** across all **155 required cases**
on the replacement DLL. The confirmed synchronous-save deadlock and late-save fixture
are fixed, but the six-mod profile is **not accepted for v1**. The original follower/package,
equipment, world-state and unresolved setup/lock failures remain visible. Final verification
passes 567 C++ cases / 6,102 assertions, nine late-baseline tests and the full build gate.
The controlled achievement-setting comparison below identifies one dependency interaction.

Worktree `.worktrees/v1-mod-profiles`, branch `v1-mod-profiles`, based on
`2f38f7779f138accd0a55868f5d9d8ea57ce6a71`. No merge or commit was authorized in this batch; merged into dev 2026-10-30
(three-way against that base, with the copy-package guard for the Lydia failure below).
The main checkout’s existing TODO edit is outside this worktree.

## Reproducibility and evidence

Builds, game installation and Proton prefixes are isolated under this worktree’s `build/`.
Only `build/coop-test/CHAT.md` and `DUO.md` link to the main checkout’s shared coordination files;
both resolve to `/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SkyrimCoop-build/coop-test/`.
The read-only compiler toolchain and engine references are reused.

All seven cached dependency archives match the SHA-256 pins in
[mod compatibility](../mod-compatibility.md#acquisition-and-configuration). The extraction trees
were refreshed from those verified archives. `build/archive-verification.json` maps the pins to files.
The historical evidence survived worktree cleanup at
`/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SkyrimCoop-build-v1-engine-fixes/`;
the old relative worktree paths in earlier reports refer to that retained build directory.

Each new run records its own profile directory under `build/profiles/`, including DLL/PDB hashes,
installed files, exact settings, both active-master lists, load orders and logs. B6 observations
include requested/carried gold, native container events and HUD notifications. B9 observations
include frame counters, control masks, dispatched input, player/copy knock state and body traces.
Original failure state is captured before scenario cleanup; failure reports and adjacent logs
are retained separately from any later diagnostic movement probe.

## Verification status

Initial `scripts/check.sh` passes: 127 client, 187 authority, 29 encoding and 220 game cases,
6,087 assertions total, harness and ragdoll comparisons, UI fixtures, Windows plugin and launcher.
The loader and native game stand-in also build.

After a queued handoff, the shared duo was claimed at 15:39 for the Scrambled Bugs run. The explicit acceptance selection is saved in
`build/acceptance-plan.json`: 153 ordinary scenarios (the tag union plus the requested additional
flows), and two separate late-save scenarios. Restart cases in the union are included explicitly;
the normal tag selector omits them. Separate-account-only scenarios are outside this batch.
At that initial stage, no dependency conflict or accepted playtest profile had been established.

## Verified harness handoff

`v1-gate` handed off `scripts/coop_test.py`, `scripts/coop_v1.py` and
`scripts/coop_followers.py` after three passing native gate repetitions without the additional
fix mods. Those changes are copied into this isolated worktree; hashes are retained in
`build/gate-handoff-hashes.json`, and the handed-off baseline log is
`build/gate-handoff-baseline.log`. This is the other agent’s validation, not an additional
profile result from this batch. The production DLL source remains `2f38f777`.

The gate now recruits a follower first and checks it after every load and fight. Each repetition
runs quickload, Load menu, named load, party wipe and a load-door autosave, with a minute of
combat after each load. The door round runs last, and post-load enemies spawn beside the host
in its current cell. The handoff retains two follower observations after artificial teleport
staging followed by combat: an unresolved native follow target, and one animation-action
difference. These are not established dependency conflicts.

Primary repetitions keep normal action timing. Extra gold/greeting/training wait snapshots are
reserved for separately recorded detailed probes (`PROFILE_TRACE_STEPS=1`); they do not replace
the primary repetition results. Original failure status, reports, nearby collision rays and
body traces are captured before cleanup. Named saves and prefix settings are retained before
profile changes; each profile starts a new game with old non-late test saves removed.

## B1: Scrambled Bugs, first failed run

Evidence: `build/profiles/20261029-153928-scrambled/`, with primary log
`build/B1-scrambled-scenarios.log`. Result: **0 pass, 1 fail, 2 not run** under
`--until-fail 3`. All five loads completed and both games continued advancing; the
post-door `check_desync` failed on Cow `60B22`, Frodnar `1348B` and Faendal `1348C`
animation metadata, plus Frodnar’s copied scene. This is a failed scenario.

The original report is `2026-10-29-1546-791`; the normal outer-handler report is
`2026-10-29-1546-6f6`. Both host and guest parts, `at-failure.json`, plugin/SKSE logs,
settings and named saves are retained. The original snapshot precedes follower cleanup
and the return to QASmoke. Neither game crashed or froze. Final `memory_sites` reports
zero frozen/saving/loading updates, update overlaps and harness-poll overlaps in both games.

The original snapshots have matching graph booleans/integers for all three NPCs; Frodnar’s
entire graph matches. Their locations are close. The guest log records refused action
replays, leaving its last-taken action different from the owner’s. Frodnar’s copy answers
scene `5B1BA` while its owner answers zero. The owner last published `5B1BA` about six
seconds before the failure, with no later zero-scene publication in the retained log.
That is a lead concerning native scene-end notification timing, not a demonstrated cause
or a reason to remove assertions. A controlled run with the same DLL and no added fixes
is required before assigning dependency attribution.

The games were stopped, dependency files/load orders restored and the reservation released
at 15:48; zero Skyrim processes were verified. The other agent took its queued turn.
The no-extra-mod control began after its next release.

Coverage limitation under investigation: `plays_on` starts a wolf fight and checks both
games for a minute, but does not currently require that the wolf remains alive throughout.
A separate read-only observer records the baseline wolf’s health/life/combat state without
changing either combatant or suppressing any assertion. Until verified, describe this as
continued play after starting combat, not a full minute of sustained combat.

## Same-DLL control without additional fix mods

Evidence: `build/profiles/20261029-155504-baseline/`; the DLL/PDB is unchanged.
The gate is **0 pass / 1 fail / 1 not run** under `--until-fail 2`: after quickload,
Lydia’s owner package is `5C84B` while its copy answers `5C851`. Original report
`2026-10-29-1556-e69`, normal report `2026-10-29-1557-7cc`. No crash, freeze or
update overlap is observed. A read-only observer samples the wolf alive for 59.9 seconds,
with health 22 throughout; this run does sustain its enemy through the play interval.

A separate door-only diagnostic repeats the same follower/autosave/fight round without
the preceding four loads. It **fails** on Cow/Frodnar action/event differences without
extra fixes: original report `2026-10-29-1559-4df`, normal report `2026-10-29-1559-391`.
Those animation failures do not require Scrambled Bugs. This does not independently
explain the earlier stale scene field, or identify the exact production/harness cause.

The primary training control is **6 pass / 1 fail / 3 not run** under `--until-fail 10`.
Run seven completes gold setup and training behavior, then fails on a new inventory
lock-order report (`154dce > 154de0` versus the same callers in the opposite object order).
Original report `2026-10-29-1602-2c9`, normal report `2026-10-29-1602-b8c`. Both stacks
are retained in `inventory-lock-{raw,symbolized}.txt`: co-op spawn inventory replacement
through `SetActorInventory`/`RemoveAllItems`, versus native outfit inventory reset through
`ResetInventory`. This precise pair was not added to the known-pattern list. No deadlock
or gold loss was observed; the warning remains a failed check.

Two separately recorded detailed training probes pass: every player and copy carries
140 before the grant, 2,140 after adding 2,000, and 1,890 after the guest trains once
(cost 250, charged once). Native container events and HUD notifications are retained in
`B6-baseline-detailed-observations.ndjson`. These probes run with the previous lock report
already present and do not clear that failed primary batch.

Purse sharing, guest barter, host-to-guest Fus Ro Dah and guest-to-host Fus Ro Dah pass
**4/4** in `build/baseline-controls.log`. Shout input, original-state diagnostics and
body traces are retained; observed frame rates are in `baseline-shout-rates.json`.
This is one control per shout direction, not the repeated Display Tweaks matrix.

Cleanup restored dependencies/orders and stopped both owned games; pgrep was zero and
the duo was released at 16:08 for the next queued agent.

## B6: Bug Fixes SSE comparison

The pinned Bug Fixes SSE 10 DLL, PDB and stock JSON load correctly in both games, verified
by SKSE logs and live module maps. The same co-op DLL and input helpers complete
**10/10 primary training repetitions**, **2/2 detailed training probes**, and **2/2
purse/barter regressions**. Evidence: `build/profiles/20261029-162410-bugfixes/` and
`build/B6-bugfixes-{scenarios,detailed,regressions}.log`. Exact config, module maps,
DLL/PDB hashes, native container events, HUD messages and per-game gold are retained.

Detailed probes again show all players and copies at 140 → 2,140 → 1,890, with one
250-gold training charge. The historical grant that left everyone at 140 remains
unreproduced, rather than fixed by a speculative change. No dependency-specific production
or harness workaround is justified by this comparison. The earlier baseline inventory-lock
failure stays recorded; passing the mod profile does not prove that the mod fixes that warning.
Both owned games were stopped, settings/dependencies/orders restored and the duo released
at 16:29; pgrep was zero.

## B9: Display Tweaks, 60 FPS

Fresh host-to-guest run: **0 pass / 1 fail / 9 not run**. Knockdown, displacement,
get-up and walking complete; the postcheck reports the guest and its copy 31 units above
the floor. Original report `2026-10-29-1646-0f9`, outer report `1646-7da`, evidence
`build/profiles/20261029-164553-display60/`. The original input masks allow all controls.
After preserving the failure, supplementary movement travels 517 units forward, 288 back,
and 441 after placement on ray-checked ground. The floor gap has settled before that probe.
This does not reproduce lost movement; the body/floor failure remains an unresolved observation.

Fresh guest-to-host run: **9 pass / 1 fail / 0 not run**. The tenth attempt times out
waiting for 120 charge frames in five seconds, before the knockdown/get-up assertions.
Original report `2026-10-29-1650-1da`, outer `1650-e63`, evidence
`build/profiles/20261029-164754-display60/`. Both games log repeated frame stalls during
that attempt, while their plugin updates take roughly 0.5–1.6 ms. Whole-attempt measured
rates fall to about 27/25 FPS, versus mostly 58–60 in earlier attempts (one about 51/50).
Both games subsequently advance, with zero frozen/saving/loading/update-overlap counters.
No permanent freeze or dependency-specific cause is established. Raw input, native events,
control masks, original body traces, settings and module maps are retained.

The 60-FPS matrix therefore totals **9 pass / 2 fail / 9 not run**.

## B9: cap-120 and no-Display control

The cap-120 alternating matrix is **13 pass / 1 fail / 6 not run**: seven host-to-guest
passes and six guest-to-host passes before the seventh guest-to-host attempt times out
walking after recovery. Evidence: `build/profiles/20261029-165104-display120/`, original
report `2026-10-29-1654-e1f`, outer `1654-7b8`. Both control masks allow all controls;
the native input log shows Forward dispatched through PlayerControls. Knock state is zero.

Before any reposition, supplementary Forward travels only 0.9 units and Back 7.9 units.
The original collision rays hit `04026B79` immediately above the body. Parsing the owned
Dragonborn/Skyrim records identifies its base as `FarmTable01` (`C234D`); nearby rays also
hit `CommonTableSquare01`. After ray-checked placement away from the landing spot, Forward
travels 88.9 units. These observations support collision blockage at that landing, rather
than a persistent loss of controls. They do not establish that Display Tweaks causes it.
The original state, table lookup, collision rays, input events and per-game body traces remain
available separately from the supplementary probe. The nominal 120 cap produces **55–76
measured updates/s**, mostly around 72; sustained 120-FPS behavior was not tested.

A fresh no-Display control fails its first host-to-guest postcheck: the owner has recovered
and walked, but its copy stands 63 units higher. Evidence: the separate `baseline` profile
following the cap-120 run, `build/B9-baseline-scenarios.log`, report `2026-10-29-1656-819`
and its original report. This also occurs without the dependency. A later baseline reset
succeeds, but does not erase that failure. Earlier isolated no-mod shouts passed both
directions once; together these do not establish a dependency-specific conflict.

No assertions were relaxed, and no speculative production fix was added. The 60-FPS cap
remains the conservative candidate for combined testing; this batch has not certified a
clean shout matrix or sustained 120 FPS.

## Final harness handoff

At 16:57, the other agent handed off its final script revisions: the verified frame-held
key/race-selection helpers (its Breton batch 10/10), a fresh-spawn guest-copy check and a
comment wrap. Source hashes before/after are in `build/final-harness-handoff.json`.
The USSEP and subsequent gates use these revisions. Prior matched comparisons retain
their original recorded script hashes. The production DLL remains unchanged.

## B1: remaining fresh profiles

| Profile | Pass / fail / not run | Failure and evidence |
| --- | --- | --- |
| USSEP only | 0 / 1 / 2 | First quickload minute: Lydia `PlayerFollowerPackage` (`5C84B`) versus copied `PlayerFollowerCombatOverridePackage` (`5C851`). `build/profiles/20261029-165745-ussep/`; original `2026-10-29-1659-c58`, outer `1659-a57`. |
| Historical all fixes plus USSEP | 0 / 1 / 2 | All five loads complete; after the door autosave Frodnar has `ChairEatingStart` versus copied `IdleChairCHILDFrontEnter`, with action/idle/state differences. `build/profiles/20261029-170010-historical-combined/`; original `2026-10-29-1707-274`, outer `1707-1b1`. |
| Final playtest | 0 / 1 / 2 | All five loads complete; door postcheck finds Cow/Sigrid/Faendal action/event/idle differences. `build/profiles/20261029-170750-playtest/`; original `2026-10-29-1714-39f`, outer `1715-f8c`. |

All three completed profiles preserve original failures before scenario cleanup, both reports,
adjacent logs and saves. Neither crashes or freezes, and update-overlap/frozen/saving/loading
counters remain zero. These failures match classes also observed without the added fix mods;
they do not identify a dependency conflict. The follower package discrepancy has no explicit
user acceptance; the other agent confirms it is separate from its two earlier follower findings.

At 17:14 the other agent proposed a missing AI-state notification when the native follower
combat-override package ends. Its focused test failed to reach combat, so it established no
failing-before/passing-after result. That draft was reverted at 17:25; no production fix is
imported here. The exact USSEP trace had sent the correct owner package roughly a minute
before the failure, while the guest's running package later differs. The other agent is
tracing the guest-side native write in a separate diagnostic build. The user requested this
Lydia issue fixed for v1; it remains unresolved and failed. All earlier failures are retained.

The final-playtest games were stopped, dependency files and orders restored, pgrep verified
zero and the duo released at 17:15. Acceptance started after the other agent's next release,
using the unchanged production DLL and the final verified 16:57 scripts.

## Initial combined acceptance (original DLL)

The explicit selection is validated against the registered native scenario names. It includes
153 ordinary cases and two separate late-save scenes. Three separate-account Steam invite
cases are explicitly excluded as planned: `first-time-join-by-invite`, `guest-joins-by-invite`,
and `loaded-save-hosts-then-joins-friend`. Restart cases within scope are included by name.
Every attempt remains in `build/acceptance-ledger.ndjson`; failed cases are not silently
retried into passes, and subsequent unrun cases resume from fresh launches after failures.

The failed final-playtest gate above is included as a failed acceptance case, with its exact
original evidence and matching DLL/action-helper hashes. It is not discarded or counted as
passing. A separate fresh all-six launch, `build/profiles/20261029-172303-playtest/`, passes
`vampirism-kept-across-rejoin` (71 seconds). The historical three-clutter-reference failure
does not reproduce in this fresh case. It remains historical evidence; the other agent reports
that the user deferred B8 past v1.

Immediately afterward, `unsynced` reports that every host reference change had a row;
the live `world-diff` reports every reference in loaded cells matches. Outputs:
`build/B8-combined-unsynced.log` and `build/B8-combined-live-world-diff.log`.
The requested `save-diff` also ran, but times out after 60 seconds: the host ESS/co-save exist,
the guest has neither, and its chat confirms the intended host-only save rule. This is a
diagnostic-command limitation, not a failed vampire-retention case. The raw failure, host
save and guest refusal are retained in `build/B8-combined-save-diff.log` and the profile's
`B8-save-diff-end/`. No save restriction was bypassed to obtain a comparison.

At that point acceptance was **1 pass / 1 fail / 153 not run** (151 ordinary plus two late-save).
The games were stopped and profile restored, pgrep verified zero and duo released at 17:27
for the other agent's queued native package-write diagnostic. The next acceptance turn is
queued. Late-save boot preparation uses the host's required late save and a fresh guest's
full `join_world` flow; both scenes and this bootstrap still require native verification.

## Save-phase failure, fix and acceptance build boundary

The next combined bootstrap hangs on the guest's early activation save. Its watchdog and
targeted IDA inspection establish a native frozen-lock cycle between save serialization and
the concurrent VM update. This is distinct from the previously fixed co-op update re-entry.
The [save-phase investigation](../re/crashes/2026-10-29-synchronous-save-phase.md) records the
stacks, source comparison, hook signature check and exact evidence. No dependency cause is
assigned from this one observation.

The source now defers synchronous device-2 saves to the existing main-loop task boundary,
while preserving synchronous results for the join bridge already running there. The fast
regression fails under direct dispatch (2/4 failed cases, five assertions) and passes with the
new dispatcher (4/4, 15 assertions). Full `scripts/check.sh` passes 567 cases / 6,102 assertions,
comparisons, UI fixtures, the Windows DLL and launcher.

Before rebuilding, broad acceptance reaches **40 pass / 5 fail / 110 not run**, including
both unrun late scenes. Besides the retained gate failure, failed checks are the refusal UI's
strict read/write lock audit, dragonfly-spawner setup, the weather/calendar postcheck after
appearance changes, and opening the copied Alvor dialogue menu. Original failure reports and
native logs remain in their profile directories and the append-only acceptance ledger.

All old games are stopped and the duo released before replacing the DLL. Final acceptance
now requires SHA-256 `28ed9f11e7a62287b2aa37620d2744a4c846986878e37838afea28c432bc0bff`.
The matching DLL/PDB and changed source are retained in `build/acceptance-binaries/28ed9f11e7a62287/`.
A fresh combined boot completes both early activation saves, followed by **7/7 native
save/load/menu/retention/main-menu-rejoin regressions**. These actual results are imported
into the acceptance ledger with their evidence and matching hashes.

The replacement build's four fresh affected profile gates retain the same assertions:

| Profile | Pass / fail / not run | Failure and evidence |
| --- | --- | --- |
| Scrambled Bugs | 0 / 1 / 2 | Guest arrival-fade timeout after earlier load rounds; both games continue advancing, with zero frozen/saving/loading/update-overlap counters. `build/profiles/20261029-181010-scrambled/`; original `1815-67a`, outer `1816-c40`. |
| USSEP | 0 / 1 / 2 | All five loads complete; door postcheck finds Cow/Faendal animation differences and Alvor head/pelvis separation of 27/25 units. `build/profiles/20261029-181727-ussep/`; original `1824-029`, outer `1824-796`. |
| Historical all fixes plus USSEP | 0 / 1 / 2 | First quickload minute: Lydia owner package `5C84B`, copy `5C851`. `build/profiles/20261029-182520-historical-combined/`; outer `1827-a48` and original capture. |
| Final playtest | 0 / 1 / 2 | First quickload minute: the same Lydia package mismatch, host `FF000843`, session 2. `build/profiles/20261029-182922-playtest/`; outer `1831-a21` and original capture. |

Each starts fresh, records the replacement DLL/PDB and dependency configuration, and preserves
both reports and adjacent logs before cleanup. No save-phase hang recurs in these boots or gates.
The gate failures remain unresolved; the save fix does not claim to repair AI packages, animation
state or arrival timing. The other agent's standard-bootstrap diagnostic did not reproduce Lydia
in ten quickload/fight rounds. This batch saves both games' master lists before hosting/joining;
that bootstrap difference is recorded as a comparison lead, not an established cause.

Current replacement-build acceptance: **7 pass / 1 fail / 147 not run** (145 ordinary plus
two late scenes), before the resumed broad matrix. The final-playtest gate's actual failure is
imported with its source evidence and matching hashes. The old 40/5 results remain historical;
they are not converted to passes or mixed into the replacement build's count.


## Continued replacement-build matrix

The append-only ledger reaches **109 pass / 19 fail / 27 not run** before a pre-scenario
readiness failure (25 ordinary cases and the two separate late scenes remain). Both shout
directions, training, both beast forms, guest leave and both container-list change cases pass
on the replacement DLL. Failed checks include native locks, an enemy copy not dying, guest
setup/input timeouts, equipment/pose/marker differences and achievement routing. They remain
failed, with no assertion or lock-pattern suppression.

Two subsequent fresh starts finish their baseline but fail the runner's initial `read_status`
check before any scenario starts. Evidence: profiles `20261029-192529-playtest` and
`20261029-193032-playtest`, their original failure captures and continuation logs. The games'
logs continue advancing during cleanup, and the latter has both actual processes present.
This is a failed readiness check, not evidence of a process exit or a passing scenario.
The captures show long frame stalls. The evidence helper also repeatedly copied all historical
reports on every snapshot; future snapshots copy only reports created during their profile.
Earlier reports remain in their original evidence directories. This reduces repeated disk I/O
without changing native actions, readiness thresholds or scenario assertions. The next fresh
continuation uses the same production DLL and public harness sources.

The native-code audit finds one difference from the unmodified IDA reference:
`Game.AddAchievement` no longer reaches a per-player object in the loaded code. A separate
persuade scenario grants Speech experience but times out waiting for its achievement call on
the guest. The fetched Engine Fixes source patches `IsModded` (AE 441528, `SkyrimSE+1BA450`)
to return zero under its supplied `bEnableAchievementsWithMods=true` setting. The native TSV
attributes AddAchievement's player reach through exactly that function. This is a concrete
comparison lead; a same-DLL setting comparison is still required before assigning the cause.

The other agent identifies a historical critter fixture problem with rain. Both original
failures here already show host weather `0x81A` (clear), so rain does not explain them. No
weather-only workaround or assertion relaxation is applied to these failures.


### Harness status-file stall

The later append-log capture identifies the startup stall more precisely. The host watchdog
at **19:27:36** catches `TestHarness::ControlFiles::WriteStatus` in `ofstream::close` /
`fclose` / `_close_nolock`, called by harness `Poll` at the main-loop boundary. The next frame
resumes after **11,955 ms**. This is not the native VM save lock cycle, and no process exit is
established. The older event is present in later profile logs because those logs append;
its timestamp belongs to the `192529` launch, not the later follower-anchor case.

Raw watchdog output and matching-DLL main-stack symbols are retained in
`build/re/startup-stall-1927/{raw.txt,main-symbolized.txt}`. The capture comes from
`build/profiles/20261029-193558-playtest/final/host-plugin.log`. Disk I/O pressure is high
while the worktree and evidence live on the rotating disk. Repeated historical-report copying
is reduced as described above. For subsequent cold profiles, the setup helper temporarily
routes only this task's control directories through private NVMe cache directories. It records
those paths, archives their contents after stopping and restores the original directories.
No production or public harness API, DLL, scenario timeout or assertion changes are involved.
This is a test-environment workaround, not a claim that all preceding timeouts have this cause.


## Late-save harness correction

Both required late saves load, and a fresh guest receives the actual late world through full
save-transfer joining. The ordinary QASmoke baseline then proves invalid for these saves:
it forces clear noon over quest-driven weather, offsets the guest over a mountain edge and
deletes loaded story NPCs. These are preserved setup failures, not scene passes.

The harness now ray-places a late guest near the host, records existing actor/copy IDs, and
marks saved-story worlds as live. Their weather and clock must follow the host; the fixture
no longer forces a frozen weather/hour over a running quest. Loaded placed story actors are
preserved even when they finish arriving after the baseline record. New test-created actors
outside that record still require cleanup and fail the leftover check. Ordinary QASmoke
retains clear noon and its empty-actor fixture. Full actor, world and new-lock checks remain.

`scripts/test_late_baseline.py` exercises the actual setup/reset/check functions. Initial red:
**4/5 fail**; the additional live-world regressions fail **3/9** before that correction.
Final green: **9/9**, including a real set returned for NPC copies, serialization, saved
weather/time, unchanged ordinary setup, actor cleanup, live progression and rejection of
host/guest weather disagreement. The suite is now part of `scripts/check.sh`. An own set-to-JSON
serialization mistake during the first native rerun is corrected with sorted lists; both
failed boot logs are retained. No C++ DLL, production-interface or wire change accompanies this harness fix.

The corrected Elder Scroll case actually runs at `20:01` from `LateAfterElderKnowledge`:
both players move, the guest logs the mirrored `ObjectReference.MoveTo`, both reach the
destination and no mirrored guest move leads the host. Its final actor comparison then
fails on copied equipment, so the entire case remains **FAIL**. Evidence:
`build/profiles/20261029-200031-playtest/`, `build/late-run-20261029-200031.log`, original
capture and outer report `2026-10-29-2001-016`. The Greybeards attempt from
`LateAfterDragonRising` first stops before its scene on a missing host world snapshot.
The next Whiterun attempt reports guard-name and equipment mismatches before the scene;
those remain preserved. A separate fresh launch loads the same required save, then stages
the host in `HighHrothgar` (CELL `87764`, verified in the owned Skyrim master) before the
guest joins. The scenario actually starts there, sets MQ105 stage 270, and fails waiting
60 seconds for the host scene to cast its player. Evidence: profile
`build/profiles/20261029-200736-playtest/`, `build/late-run-20261029-200736.log`, original
capture and outer report `2026-10-29-2010-b76`. Guest scene-package behavior is unverified;
this does not establish a dependency cause. Earlier setup failures remain separate batch
errors in the ledger. The High Hrothgar baseline also reports book-pose and container-content
differences; its full warning is retained independently of the scene timeout.


## Final acceptance and recommendation

**No profile is recommended as accepted for v1 by this batch.** The tested default candidate
is all six pinned dependencies with their supplied configurations, Display Tweaks capped at
60 and the merged first-time-join assets enabled. Ordinary save/load and full rejoin checks
pass after the save fix, but the extended follower gate and required play flows retain
unaccepted failures. A configuration workaround for one achievement interaction does not
clear the rest of the matrix.

| Required selection | Pass | Fail | Not run |
| --- | ---: | ---: | ---: |
| Ordinary tag union plus explicit additions | 131 | 22 | 0 |
| Required late-save scenes, separately launched | 0 | 2 | 0 |
| Total, unique cases on DLL `28ed9f11e7a62287` | **131** | **24** | **0** |

These are unique-case results, not a sum of retries. Four later ordinary regressions all
pass after the harness correction (roster, host load, full kept-character rejoin, follower
trade), leaving the counts unchanged. The ledger retains **159 current-build attempts**,
including those four repeats, and **45 original-build attempts** as separate history.
Bootstrap/baseline errors are separately recorded; none is converted into a scenario pass.
The controlled profiles below are not imported as acceptance passes.

Explicit requested additions: vampire retention/rejoin, container extra-data, corpse loot,
Take All, tempered items, potion retention, guest ingredient knowledge, both enchantment
knowledge cases, guest leave and `guest-story-starts-no-quest` pass. Host ingredient knowledge
behavior completes but its cover-plate postcheck fails. Guest quit and host quit flows also
remain failed on their retained world/cleanup postchecks. Both shout directions, training,
barter Speech checks, both beast forms and the ordinary reload/character flows pass in the
combined matrix. This does not erase the failed repetition matrices or separately listed cases.

All 24 current failures are listed below. Evidence names are directories beneath this
worktree’s `build/profiles/`; each holds the manifest, commands/settings, original failure
capture, available original/outer reports and adjacent logs. Full errors and all 155 case
results are in `build/acceptance-summary.json` and `build/acceptance-ledger.ndjson`.

| Case | Classification and observation | Evidence profile |
| --- | --- | --- |
| `join-panel-shows-refusal` | **Unresolved native/co-op lock diagnostic.** Refusal UI completes; strict read/write lock pair `16d4cb > 182241` versus `1823d0 > 15ff00` fails. | `20261029-183335-playtest` |
| `guest-spawns-no-critters` | **Unresolved profile observation.** Host creates no dragonflies within 20 s, with clear weather already active. The fresh no-extra-mod control passes. | `20261029-183558-playtest` |
| `dialogue-options-are-the-partys` | **Observed co-op state mismatch.** Dialogue completes; host copy lacks the guest’s clothes/weapon. Fresh no-extra-mod control passes; dependency cause is not isolated. | `20261029-183843-playtest` |
| `dialogue-highlight-is-shared` | **Unresolved profile observation.** Menus never list identical topics within 10 s. Fresh no-extra-mod control passes; no individual dependency is established. | `20261029-184613-playtest` |
| `natives-reach-matches-ida` | **Confirmed dependency interaction.** Engine Fixes achievement patch removes the player-read path; `Game.AddAchievement` is classified differently. Narrow setting workaround verified below. | `20261029-184721-playtest` |
| `dialogue-picker-gets-speech-experience` | **Confirmed dependency interaction.** Speech XP arrives, but achievement routing does not. Both checks pass with only the achievement-enabling patch disabled. | `20261029-184828-playtest` |
| `party-travel-dead-guest` | **Observed co-op state mismatch.** Enemy dies on the host but its guest copy does not meet the death check within 5 s. | `20261029-184953-playtest` |
| `death-spectate-host` | **Unresolved setup/input timeout.** Host hands-ready setup times out before the intended death observation. | `20261029-185335-playtest` |
| `death-wipe-checkpoint` | **Unresolved equipment/cleanup observation.** Recovery completes; guest reset retains extra equipped item `12EB7`. | `20261029-185540-playtest` |
| `kill-move-kills-guest` | **Unresolved setup/input timeout.** Host hands-ready setup times out before the intended kill move. | `20261029-185728-playtest` |
| `statue-fires-at-client` | **Unresolved native/co-op lock diagnostic.** Strict pair `17a479 > e82682` versus `e85437 > 16b61d` fails. | `20261029-185910-playtest` |
| `relief-takes-client-relics` | **Observed co-op routing mismatch.** Host shows the guest action’s message box. | `20261029-190055-playtest` |
| `vampirism-is-each-players` | **Known world-state observation.** Guest-only created marker `STAT 3B` remains. This failed turn/cure case is separate from passing vampire retention/rejoin. | `20261029-190244-playtest` |
| `save-load-gate` | **Observed co-op AI mismatch.** Lydia owner `5C84B`, copy `5C851` after quickload; also reproduced without extra fixes in the original-DLL control. | `20261029-182922-playtest` |
| `guest-quits-to-main-menu` | **Unresolved world/cleanup observation.** Pose reset misses and clutter movement-record differences on `32AFE`, `32AFF`, `32B00`. | `20261029-190557-playtest` |
| `host-quits-to-main-menu` | **Visual world-state observation.** Seven `CoverPlate01` poses differ after the flow. No new acceptance is assumed. | `20261029-191148-playtest` |
| `spec:guest-joins-elsewhere` | **Unresolved native/co-op lock diagnostic.** Same strict `17a479/e82682` versus `e85437/16b61d` family. | `20261029-191343-playtest` |
| `quest-perk-turn-in-guest` | **Unresolved setup timeout.** Guest never meets the standing-on-ground precondition within 5 s. | `20261029-191723-playtest` |
| `quest-gold-turn-in-guest` | **Unresolved setup timeout.** Guest’s quest-giver dialogue does not open within 10 s. | `20261029-192348-playtest` |
| `follower-guest-recruits` | **Unresolved native/co-op lock diagnostic.** Recruitment behavior completes; same strict `17a479/e82682` versus `e85437/16b61d` family fails. | `20261029-193431-playtest` |
| `follower-switches-anchor` | **Unresolved harness/native observation.** Host quest-state request `750BA` gets no answer. | `20261029-193558-playtest` |
| `host-ingredient-knowledge-control` | **Visual world-state observation.** Knowledge assertions complete; seven cover-plate pose postchecks fail. | `20261029-193750-playtest` |
| `elder-scroll-vision-reaches-party` | **Observed co-op equipment mismatch.** Both native moves/arrival and no-guest-led-travel checks complete; final player/NPC equipment comparison fails. | `20261029-200031-playtest` |
| `greybeards-scene-casts-party` | **Unresolved scene setup/behavior.** Required save plus High Hrothgar staging; host scene never casts its player within 60 s. Guest package behavior is not validated. | `20261029-200736-playtest` |

The strict lock failures are not allowlisted, and setup failures are not assumed to be harmless
harness faults. The three no-extra-mod controls for critters, topic lists and party dialogue
pass, but changing five dependencies at once does not isolate their causes. Visual/marker
observations stay identifiable as failed checks; this batch introduces no new acceptance of them.
Historical invalid-pointer crashes are **unreproduced** in the new B1 gates. No originating
write is established for those historical corruptions. The demonstrated native VM save-lock
cycle is fixed separately; its verification does not claim to explain every earlier crash.

### Controlled comparisons and achievement setting

Same replacement DLL, fresh no-extra-mod profile `20261029-201347-baseline`: **5/5 pass**:
`natives-reach-matches-ida`, `dialogue-picker-gets-speech-experience`,
`guest-spawns-no-critters`, `dialogue-highlight-is-shared`, `dialogue-options-are-the-partys`.
Log: `build/comparison-baseline-20261029-201347-run.log`.

Fresh all-six profile `20261029-201557-playtest-no-achievements` changes only
`EngineFixes.toml` → `[Patches] bEnableAchievementsWithMods=false`. The co-op and dependency
DLL hashes are unchanged. Native reach audit and the entire persuade/Speech/achievement
scenario both pass **2/2**. Log: `build/comparison-playtest-no-achievements-20261029-201557-run.log`;
all results/configuration provenance remain in `build/comparison-ledger.ndjson`.

The [author’s patch](https://github.com/aers/EngineFixesSkyrim64/blob/b289e3deae71ce3915bb19c5faeeda8bf6c6a25c/src/patches/enable_achievements.h)
replaces `IsModded` (AE 441528, `SkyrimSE+1BA450`) with a zero return. The vanilla IDA table
attributes AddAchievement’s per-player reach through that function. Removing that read changes
the co-op classifier, so the achievement remains on the host rather than the guest picker.
The controlled toggle confirms this compatibility interaction. This is a narrow configuration
workaround, **not a completed NativeReach production fix**. It turns off Engine Fixes’ feature
that enables achievements with mods; actual Steam achievement awards are not validated here.
The full 155-case matrix still records the supplied/default setting as failed. A complete
matrix with the modified setting has not been run.

### Final verification and retained state

Final `scripts/check.sh` passes in **21 seconds**: **567 C++ cases / 6,102 assertions**, all
**9** late-baseline regressions, existing harness/ragdoll comparisons, UI fixtures and the
Windows DLL/launcher builds. Evidence: `build/final-check.log` and `build/check/`.
The built DLL remains exactly
`28ed9f11e7a62287b2aa37620d2744a4c846986878e37838afea28c432bc0bff`; matching PDB/C++ source are
retained in `build/acceptance-binaries/28ed9f11e7a62287/`. The source and harness fixes are
uncommitted. No merge into `dev` and no `scripts/net_check.sh` run occurred.

`build/profile-index.json` indexes **58** profile manifests. `build/acceptance-plan.json`
contains the exact selection, required saves, source build and separate-account exclusions.
Completed profiles record installed files, settings, active masters, DLL/PDB hashes and scenario
selection. Partial boot evidence retains the available data; a hung activation save cannot
supply a guest save-header master list. Late runs and follow-up ordinary checks record the corrected harness hash;
prior runs keep their original hashes. The historical combined and final playtest gates
remain separate launches despite matching dependency lists.

All task games are stopped, dependency files, load orders, prefix INIs and temporary control
symlinks restored, the task waiting entry removed and the duo released. Shared CHAT/DUO links
still resolve to the main shared files. Main source and the user’s TODO were not edited by
this task. Windows runtime, separate-account Steam, larger parties and player packaging remain
outside these acceptance claims. Sustained 120 FPS remains untested.
