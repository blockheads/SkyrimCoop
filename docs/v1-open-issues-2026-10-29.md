# V1 remaining work and recent bug index

Status checked on 2026-10-29. `party-dialogue` reports that review and integration into dev's
uncommitted working tree are complete and `scripts/check.sh` passes; the user commits dev.
This indexes the recent stability, compatibility and dialogue
findings; the older backlog remains in [progress](progress.md#loose-ends) and
[accepted bugs](bugs.md). A failed scenario is evidence to triage, not automatically a distinct
production bug or a conflict with whichever mod was installed.

## What remains before playtesting

| Work | Current state / completion check |
| --- | --- |
| Validate the reviewed combined build | Review/integration is reported complete and the fast/build gate passes. Earlier worktree Skyrim passes do not validate the final merged DLL, including the review's shared `NiAVObject::Update` helper. |
| First-time joining from the main menu | Implemented and verified in the isolated `first-time-join` worktree, awaiting authorized integration: bundled creation, automatic joining, kept-character rejoin, native host-loss input, and real two-account running/launch invites. See [validation report](reports/2026-10-29-first-time-join.md). [Design](design/join-flow.md#first-time-join-bundled-start-2026-10-29-v1). |
| Save/load acceptance gate | Built: `save-load-gate` (quick, Load menu, named, wipe, door autosave; a follower kept through all of them; a minute of fighting after each) passes 3/3 on vanilla (2026-10-29, v1-gate). Still to run on the fix-mod profiles where the reload crashes were seen. |
| Party-dialogue acceptance | Implemented, including the party's tagged options and shared highlight. Latest reported reruns: follower trade 3/3, party options 3/3, picked-line response 2/3. Retest the combined build and retain the intermittent failures below. |
| Deleted-follower conversation safety | Confirm or rule out the suspected fixture teardown race. If ordinary play can delete/disable a conversation actor on this path, end the conversation safely and add a regression. It is now an explicit item in the v1 checklist. |
| Final fix-mod profile | 2026-10-29 acceptance on the merged save fix (synchronous device-2 saves, [save-phase investigation](re/crashes/2026-10-29-synchronous-save-phase.md)): 131 pass / 24 fail / 0 not run of 155; the six-mod profile is not accepted for v1 ([report](reports/2026-10-29-v1-mod-profiles.md)). Earlier: all six pinned mods have been acquired and tested. Latest focused combined run is 14/15 without crashes (the one failure, B8, is deferred to [bugs.md](bugs.md#plugin-compatibility)); it does not replace the earlier full 27-case run or Windows acceptance. Resolve or explicitly accept retained failures before recommending a profile. [Matrix](mod-compatibility.md). |
| Real multiplayer acceptance | Separate Steam accounts/invites and 3–4 players, including quests/dungeons, scenes, combat/death, save/rejoin and guest-to-guest Fus Ro Dah. Two local games using the direct session transport do not cover these. |
| Clean player package | `scripts/package.sh` builds it (see the [todo](v1-playtest-todo.md)); [install.md](install.md) has the steps; `scripts/package_check.py` installs it into a clean folder and prefix and boots it (PASS 2026-10-29). Remaining: a joined session of two packaged games, and Windows. |
| Easy playtest modlist installation | Still a publishing-stage task, after functionality and acceptance. Pin the tested files/settings and provide the agreed easy install for friends. |

The actionable checklist remains [v1-playtest-todo.md](v1-playtest-todo.md). Unchecked entries
can mean implemented but awaiting acceptance; they do not all mean missing features.

## Triage on the merged DLL (2026-10-29)

`42273179` plus the fixes below, vanilla duo, the first round of the triage set (the run stopped at the vampire
rejoin, which hit the arrival push). Not run yet: the playtest mod profile, B8/B9. The new `save-load-gate`
(quickload, Load menu, named load, party wipe, each then a minute of fighting) passes 4 of 4 on vanilla.

- Pass: dialogue pick, follower trade, training, both Fus Ro Dah, host loads and quickloads, the frozen-VM save.
- Arrival push and combat after death: below (fixed).
- **New, out of v1 scope ([bugs.md](bugs.md)): a respawned guest can stay ragdolled.** `death-spectate-guest` once timed out on "the respawned player can
  walk again": its player's 18 ragdoll bodies awake, sunk 49 units into the floor. 0 of 6 since; the same family as
  the open revive item in [death.md](design/death.md) (a revived player knocked down later stays getting up) and
  the walking timeout after a shout. Kept under `--until-fail`.
- **The lost dialogue press reproduced** (`dialogue-pick-reaches-host`, 1 of the first 2 runs on the fixed build):
  the client's log has no dialogue pick at all for the E press (the SWF never called `TopicClicked`), and its input
  trace has no line for that press. The scenario now prints the menu's SWF state before the press and at the failure,
  and the client's last dialogue lines. `--until-fail` (failed on run 4, report `2026-10-29-1129-bc3`): the press
  is dispatched with the Dialogue Menu open, but the SWF never takes it (allow_progress 1, state 1 before and after).
  Every conversation lists step 1 twice: at its opening and ~5 s later as the greeting ends (the host's engine
  rebuilds; the guest receives the same 47-byte list again). The working runs pressed 0.8 s after the second list,
  the failed one 50 ms after it: the press races a re-show of the list. Two candidates, not yet told apart: the
  guest re-showing on the host's identical second list, or the guest's own engine showing its list after the
  greeting over the party list we showed early (which also makes `menu_takes_pick` true too soon). Next: the
  dialogue event trace (plan H2: list shown by us, engine `ShowList`/refill, voice-ready, `TopicClicked`), then
  the fix (likely one show per step, and the host not resending an unchanged list).
- **New, out of v1 scope ([bugs.md](bugs.md)): owners kneel after a follower trade.** `follower-trade-to-picker` failed its afterwards check once:
  both players' own bodies read kneeling (pelvis 32 and 44) while their copies stand, both in first person. Either an
  idle the copies do not replay, or the body check misreading a first-person player; not yet told apart.

## New NPC desync checks (2026-10-30, v1-gate)

Scenarios removed their spawned NPCs before the runner's desync check, so no spawned NPC was ever compared with its
copy. `remove_host_npc` now checks every copy first, and `pick_with_keys` checks mid-conversation (one player talks).
Fixed: a copy is given an item its owner equips without carrying it (an outfit put back on after `removeallitems`
left Lydia's copy unarmored; GameTests `[equipment]`), and a copy's running package stays its owner's
(`HookInitFromPackage`, ClientTests `[CopyPackage]`). Found, open:

- Lydia's dialogue idle differs mid-conversation (`animation.idle` 1105592 on the host vs 1105590 on the copy).
  **Cause and fast fix verified 2026-10-30 (`v1-copy-desync`):** the hook published an idle query after Skyrim
  changed its flags from 1 to 2; the copy performed the query before the owner's actual gesture, then refused
  the actual event or kept the queried idle. The fake models the native query and graph transitions;
  GameTests `[replay-query]` fails before, passes after remembering the initial query flag. ClientTests
  `[replay-capture]` and full `scripts/check.sh` pass. Focused native acceptance now passes anchor switch,
  saved anchor, trade and host load. Both Beast Form and both kill-move scenarios pass in the next batch.
  The final 105-scenario tag union finishes 87 PASS/18 FAIL; both transformations and both kill-move replay cases pass.
  Intermittent trade copy latest-action zero remains open; no blanket closure of animation/movement checks. [Evidence and engine path](re/systems/animation-action-queries.md).
  Remaining matched traces show independent clip transitions leaving different active graph states. A second red
  GameTests `[replay-graph]` and recovery units cover loading the accepted action's resulting owner graph after
  refusal. Unrecovered unpaired refusals disappear in the focused batch; raw refused attempts that recover are logged separately. A wolf-half refusal is already playing
  with its local player, as the trace establishes; [recovery evidence and remaining timing/pose findings](re/systems/animation-replay-recovery.md).
- Outdoors: an Imperial Soldier's turn (`TurnRight` vs `turnStop`) and an Elk's `weapon_drawn` differ.
- Guest crash, twice, loading the host's save in `follower-anchor-saved` (`crash-2026-10-30-09-25-02`, `09-34-28`):
  the harness cleanup had deleted the world's NPCs, the guest marked its copies deleted, and the load handed one
  to a freed cell. Fixed 2026-10-30 without a reproduction (not reproduced in vanilla, the duo or the follower tag
  again): a guest only disables a plugin's reference, and the cleanup deletes only what a scenario created
  ([2026-10-30-deleted-placed-ref-load.md](re/crashes/2026-10-30-deleted-placed-ref-load.md)).
- The guest's player "stays low" after it died and was brought back: `follower-anchor-dies` then
  `follower-anchor-saved` reproduces it (2026-10-30). Right after the revive it stands; at the next scenario's first
  conversation its own body kneels (pelvis ~29, head ~55, like the bleedout pose) and later stands again; once its
  death ragdoll was left awake too (18 bodies). The host sees its copy standing. Likely the open revive item in
  [death.md](design/death.md) (the behavior graph is not reset by a revive). A later conversation without a death
  first does not show it. Not the trade. Out of v1 scope (the user, 2026-10-30): in [bugs.md](bugs.md).
- Found 2026-10-30 (merged build): after the guest hands Lucan the claw (`dialogue-options-are-the-partys`, its
  script run for the guest), the host's copy of the guest's player wears only one item: its armor and weapon gone
  (`copy lacks 12E46, 12E4B, 12E4D, 13790`). **Fast fix verified (`v1-copy-desync`):** the native
  outfit/model rebuild leaves a stale partial worn set; items remain carried, and incremental equips fail.
  Explicit UnequipAll plus an unchanged owner snapshot restores all five. The fake learns that boundary;
  `[outfit-rebuild]` fails before and passes after resetting/reapplying a mismatched whole worn set.
  `[equipment-repair]` units fail six assertions before, pass after; matching sets stay untouched, spells
  and item counts survive repair. Full `scripts/check.sh` passes;
  **Focused native acceptance passes:** the full claw hand-in passes with normal assertions and no diagnostic
  repair commands. **Closed for the requested claw failure:** the full claw scenario and both quest-gold
  turn-ins also pass in the final105-scenario combined batch. The native script's precise initiating rebuild call remains unknown.
  [Evidence](re/systems/copy-equipment-after-rebuild.md).
- Found 2026-10-30 (merged build): `guest-cannot-load` failed once on a lock-order report with our code on the stack
  (`+0x3cdb45 > +0x1823d0 vs +0x182e91 > +0x3d1507`, client). **Recorded 2026-10-30:** these are the engine's own
  orders, with no product change. In `ClearRefAlias`, which our guest quest-fill apply calls the same way Papyrus
  `Alias.Clear()` does, the quest's lock comes before the actor's aliases. A load or the harness's console `coc`
  takes them the other way round. The `+0x17a479 > +0xe82682` pattern (`party-changes-cells`,
  `follower-guest-recruits`) is a cell detach against the physics job's trigger event, with only pass-through hooks
  of ours on the stack. The checker no longer counts our pass-through `HookUpdateAI` and `HookCheckForNewPackage`
  as ours; the boot alone had 14 such AI reports
  ([2026-10-30-lock-orders-ours-on-stack.md](re/crashes/2026-10-30-lock-orders-ours-on-stack.md)).
  `--until-fail 20` did not reproduce it. Run 17 failed differently: the guest's chat did not explain its refused
  menu load within 10 s (bug report `2026-10-30-1256-8a5`). Open.
- Found 2026-10-30 (the user, then the harness): actors slide or walk in slow motion, often right after the party
  arrives somewhere. The harness's `gait` readout (`TestHarness/GaitWatch`, `check_gait`) compares how far each
  humanoid travels with how far its feet move against its body. `actors-walk-after-arrival` (Riverwood and the
  Sleeping Giant, 12 s each) fails 3 of 3: guest copies slide while their owners walk (Faendal, Sven, Dorthe,
  Frodnar, Alvor), and the host's own NPCs slide for a few seconds after arrival too (Embry, Sigrid, Gerdur), with no
  vanilla A/B yet. v1-copy-desync took it. Open.
- Found 2026-10-30: after `party-buys-horse-host`/`-guest` (ride and dismount), the rider's copy was left lifted
  off its feet on the other game (pelvis ~100 above the floor); the runner booted the duo again. Open.
- Found 2026-10-30: "[ours]" lock tags still come through the pass-through script `MoveTo` chain (`HookMoveRun` ->
  `HookMoveTo` -> `HookForceState`) for engine-only orders (`1823d0 > 15ff00`). `HookUpdateAI` and
  `HookCheckForNewPackage` are ignored since today
  ([2026-10-30-lock-orders-ours-on-stack.md](re/crashes/2026-10-30-lock-orders-ours-on-stack.md)). Open.
- Harness, closed 2026-10-30: one scenario leaving the baseline broken stopped every later one ("NOT RUN: the games
  could not be reset"; `party-travel-dead-guest` and `follower-anchor-dies` blocked 13-68 each). The runner now boots
  the duo again with its recorded arguments (`boot.json`) and goes on, naming the scenario that broke it
  (`--no-restart` keeps the old stop; testing.md, `scripts/test_coop_harness.py`).
- Dialogue's harness check found and the fixes closed: each menu its own claw line, a list a step behind after a
  pick, a guest's menu blank while the NPC answered. The picked line overlapping in the claw conversation is in
  bugs.md (out of v1).
- Copies of leveled NPCs show different names: placed guards in QASmoke ("Whiterun Guard" vs "Imperial
  Soldier") and a spawned "Bandit Chief" vs "Bandit Outlaw" (2026-10-30).
  **Fixed, native name comparisons pass (`v1-copy-desync`):** the host's composite base has Chief's name and Outlaw's
  selected face. The face reaches the copy, but the resolved name had no saved section (flags 0, empty snapshot).
  Announcements include the native full-name section. Copies decode it on a disposable NPC and apply
  a reference name (ExtraTextDisplayData), preserving the selected shared base. An attached private name base
  proved unsafe: native leveled-data loading freed it before actor GC. That regression now has a RED4
  lifetime smoke and a fix; the final mounted-travel sequence no longer crashes.
  GameTests `[npc-choice]` fails before for created and placed copies, passes after; `[npc-snapshot]` units and
  full `scripts/check.sh` pass. A native acceptance regression (ordinary named NPCs saving non-name runtime
  sections) now has its own red/green flow and unit; those snapshots retain the original identity base.
  Native spawned/placed name comparisons now pass; the placed scenario retained a separate travel lock-order
  report. **Closed for resolved names:** spawned and placed name comparisons pass in the final 105-case
  batch; the placed scenario still reports the independent travel lock. Native names use BSFixedString
  case-insensitive equality; a pooled-name smoke and comparison unit fail first, while Chief/Outlaw remains rejected. [Evidence](re/systems/leveled-npc-copy-names.md).
- Onmund's magicka: owner 205, copy 175 (`dialogue-question-answered-by-who-can`).
  **Cause established (`v1-copy-desync`):** both permanent values are 175; the owner's active fortify effect
  adds 30 through TEMPORARY, absent from the pool snapshot. The fake now respects the native DAMAGE healing
  limit and `[fortified-pools]` reproduces 175 versus 205. Snapshot/live temporary-modifier synchronization
  and ordered restoration pass `[fortified-pools]` and `[actor-pools]` after both failed first.
  **Fixed, focused Skyrim acceptance passes:** both report permanent 175, TEMPORARY 30, current 205,
  with the same identity base. **Closed for this pool mismatch:** dedicated Onmund and the original
  dialogue-question-answered-by-who-can both pass in the final combined 105. Full `scripts/check.sh` passes. [Evidence](re/systems/fortified-copy-pools.md).
- Lydia's mid-trade missing worn item (`10E2DE`) is her 12-arrow stack. **Fixed, native trade passes:** queued
  equips emit no equipment event; the following actual bow equip now schedules a full snapshot on the next
  update, rather than waiting up to three seconds. `[queued-ammo]` and snapshot scheduler units both failed
  before and pass after. The Vigilant's similar temporary magicka gap is covered by the generic pool fix.

The user's smoke audit exposed a second observer gap: `Party::ActorDifferences` compared neither animation
markers, NPC names nor pools. New `[desync-checks]` fails on all three before adding those generic checks.
The ordinary guest-talks flow now performs actual query/gesture/stop actions and checks all actors during
conversation and before cleanup; disabling recovery makes it fail on the stuck final gesture. `[replay-time]`
and `[replay-staging]` fail before fixing the fake's immediate replay: it now obeys the playback clock and shares
the native graph-job phase machine. `[replay-ready]` also fails three assertions before teaching graph readiness and the native ten-update fallback. These improvements
do not establish the cause of Skyrim's intermittent zero marker. A follow-up observer test also fails on
head/pelvis gaps despite identical action markers. Supplied pose readings are now checked with the native
20-unit tolerance; the fake has no IK solver. A subsequent native camera control establishes a stale off-screen skeleton:
owner head88→124 just by looking, same JumpLand, scale1 on both. `[culled-pose]` and three comparison units
fail first and pass after qualifying NPC bone readings by visibility; visible pose faults and all other actor
checks remain. Native metadata calibration confirms owner83/cull6/frustum=false versus copy122/cull7/frustum=true; the qualified dialogue comparison passes. [Evidence](re/systems/culled-npc-pose-readings.md).

Aela's `host-loads-party` failure in the combined batch is a missing **torch `1D4EC`**, not an animation-zero report.
`[torch-repair]` and `[repair-slot]` fail first; only weapons now use explicit hand slots, other types use their
native default slot. The final-build controlled `npc-torch-repairs-in-default-slot` passes: Aela actually wears1D4EC,
her copy equips it, and an unchanged owner snapshot repairs deliberate drift. The torch fault is closed.

A further native Deer trace catches current weapon state0 on the owner and5 on the copy, with the same
moveStart marker carrying historical State2 `0x64E10A8` (weapon bits5). The fake previously ignored those
replayed state bits and its observer compared only hand direction. `[replay-weapon]` now fails **5 != 0**
through both ordinary replay and graph recovery; the observer's raw-state check fails first too.
`[replay-state]` units fail three assertions. Historical action/graph flags now preserve the copy's current
weapon bits; live owner weapon-choice messages retain control. The final recruitment case reports no NPC
weapon/name mismatch. [Native fact and fast regression](re/systems/animation-replay-recovery.md).

Latest complete `v1-copy-desync` gate: **618 C++ cases, 6,402 assertions**, Python
harness checks (including17 comparison tests), UI fixtures, plugin and launcher pass (`build/replay-weapon-check.log`).
All changes remain uncommitted in `.worktrees/v1-copy-desync`; no merge into dev.
The final105-case combined run completes **87 PASS/18 FAIL**, 1,441.8s, with party-travel-dead-guest last.
Names, Onmund, full guest claw hand-in, anchor switch/save/trade, host load, controlled torch, both Beast Forms
and both kill-move replay cases pass their requested checks. The placed name case retains a lock report.
The private-name-base cleanup crash does not recur. Own games stop and DUO is released before offline work.
Remaining failures include owner gait, revived-player body/pose, four strict lock reports, Lydia facing26°,
Belethor's copy retaining a scene, a missing story-save fixture, and the dead-guest enemy-copy death timeout.
Intermittent all-zero animation from the earlier batch remains open and did not recur. Five refused paired
attempts remain logged (four kill moves and HorseExit); the earlier wolf trace establishes an already-playing
duplicate, not a blanket explanation for every paired refusal. No lock allowlist or tolerance increases.
[Full final results, open failures and smoke evidence](reports/2026-10-30-copy-desync.md).

**Scope decision, 2026-10-31 (user): all remaining findings from this copy-desync batch are deferred past v1.**
This includes the intermittent zero marker, facing/scene differences, paired refusals, death/baseline and
body/ragdoll failures, gait investigation, lock reports and missing story-save validation. They remain open
in the [post-v1 backlog](bugs.md#copy-desync-follow-ups), with their evidence and failing checks preserved.
The 18 failing scenarios are accepted deferred findings for this batch; their results are not changed to passes.
The lock reports and enemy-copy death delay deserve earlier follow-up because their gameplay impact remains
uncertain. This decision does not change the disposition of unrelated findings elsewhere in this document.

**Superseding investigation, 2026-10-31:** the three reopened risks are classified and corrected.
Worker cell loads/quest writes now run between frames; the mounted native-engine order retains its raw
report with corrected wrapper attribution. Death timeout was a stale pre-travel copy handle (five-second
bound unchanged). Belethor had a real stale effective scene cache and hidden guest conversation after
barter; both have regressions and native two-transaction controls. Gate **629 cases / 6,462 assertions**
plus Python/UI/Windows passes. Combined affected acceptance **20 PASS / 2 FAIL**,401.5s; fresh four barter
controls plus dead-guest last **5/5 PASS**,139.2s. The combined failures (Sigurd registration and original
Belethor animation marker) remain recorded; all other copy-desync deferrals above remain.
[Classifications, evidence and exact results](reports/2026-10-31-v1-three-risks.md).

## Recent unresolved findings

These entries consolidate observations that were previously scattered across reports and chat.
They retain failures that later passing runs did not reproduce.

| Finding | Evidence and current treatment |
| --- | --- |
| Historical save/reload corruption or freeze | The confirmed frozen-VM re-entry is fixed; corrected lock-history attribution also has a fast regression. Six Engine Fixes cycles pass 30/30, and the final combined profile does not crash. The originating write for every historical crash was not caught. Keep the broader save/load gate and merged-build acceptance open. [VM investigation](re/crashes/2026-10-29-save-vm-reentry.md), [follow-up](re/crashes/2026-10-29-lock-history-and-button-pose.md). |
| Picked dialogue line sometimes does not play | `dialogue-pick-reaches-host` still times out waiting for the picked line in one of the latest three reruns. Record separately from whether the option list/highlight works. Latest handoff: `build/coop-test/CHAT.md`, `party-dialogue`'s 10:20 report. Reproduce on the combined DLL. |
| One dialogue worker crash involving deleted Lydia | Out of v1 scope (2026-10-29), in [bugs.md](bugs.md#dialogue). Not reproduced. The play path passes: `dialogue-npc-removed-mid-conversation` (new, tag `v1`) removes the NPC on the host mid-conversation; the guest's menu closes, both games end it, no crash. The scenario baseline now ends conversations before deleting NPCs, the fixture race the lead pointed at. Crash log `SKSE/crash-2026-10-29-09-52-36.log`; [progress](progress.md#loose-ends). |
| Equipment differences after dialogue or reload | The earlier USSEP dialogue check found the host's copy missing the guest's clothes/weapon; an earlier technical-profile reload left an extra iron sword (`12EB7`). The later dialogue tag also reported equipment desync after an earlier failed scenario; the latest party-options reruns pass 3/3. Keep the observations; establish which reproduce from a fresh combined session before assuming one common cause. [Matrix findings](mod-compatibility.md#failures-requiring-follow-up), shared chat. |
| Combat targeting after host death | **Harness bug, fixed 2026-10-29.** The death scenarios froze the enemy (AI off) before `startcombat` on the standing player, so whether it took the new target was timing; the failure message read the target from a readout without AI fields (`host_npc_state`). The enemy now turns on the standing player before it is frozen; `death-spectate-guest`/`-host` pass 3 of 3 each. |
| `first-time-join-breton` race press | ~1 in 3 runs lost an Up in the RaceSex menu. Cause 1 (fixed in the harness): a tapped key's press and release in one frame, which a menu's movie never takes ([input.md](re/systems/input.md)); `tap_key`/`press_key` hold a frame. Then 8 of 9 passed; run 9 lost a press held 84 ms with the Fader Menu up. Cause 2 (the scenario): it counted a press as taken only when the engine wore the new race within 5 s; the step now waits for the list's highlight to move, then up to 15 s for the engine's race (it loads the body behind the fade). 10/10 on vanilla (2026-10-29, v1-gate). |
| Arrival can displace the host | **Fixed 2026-10-29.** Reproduced on every main-menu rejoin (47-84 units): the kept record had lost its place (a guest's record replaced it whole, and the place was stored only at saves while connected), so the guest arrived on the save's player spot, the host's own, and its copy pushed the host along at ~40 units/s (`body_trace`). `CharacterRecords::Put` keeps the place, `HostRecords` stores it when a guest leaves, and a kept character without one is placed beside the host. GameTests `[join-placement]` fails without it; `guest-arrival-leaves-host-in-place` (new) 3/3. |
| Walking timeout after a shout | The cap-120 Display Tweaks run passes knockdown, displacement and getting up, then times out on walking; actual observed updates were about 71/s, not sustained 120 FPS. A similar failure occurred without extra fix mods. Normal two-direction Fus Ro Dah scenarios pass. Retain the higher-FPS result and distinguish geometry/control problems before assigning a cause. [Matrix findings](mod-compatibility.md#failures-requiring-follow-up). |
| Isolated training/gold setup failure | Adding 2,000 gold once left the party at 140 before training opened in the Bug Fixes profile. Training/barter later pass without extra fix mods and in the combined profile. This is an unreproduced setup/flow finding, not a confirmed ongoing Bug Fixes conflict. [Focused comparison](mod-compatibility.md#focused-comparison-without-the-extra-fix-mods). |

Evidence paths beginning `v1-engine-fixes/build/` mean the ignored build directory of
`.worktrees/v1-engine-fixes`, not a committed test artifact. The compatibility reports retain
the other original per-profile logs and failed batches.

## Already accepted or deferred

These remain filed in [bugs.md](bugs.md); they have not silently become additional v1 blockers:

- Movement-record flags on guest clutter after a six-mod vampire rejoin (B8): deferred past v1 by the user
  (2026-10-29); positions agree, guest-only, not on vanilla. The six-mod run's 14/15 counts it.
- Historical intermittent werewolf graph/heap crashes: currently marked out of v1 scope there.
  This follow-up completes 14 transformations/reversions without a crash; longer play and crash
  bundles remain useful. Do not claim the historical faults are all explained by the VM fix.
- Ragdoll jitter, get-up timing/facing/snap and displaced dead-copy poses; an interrupted chest
  lid can also differ, and a wolf body once exceeded the rest timeout.
- The guest-only `VampireChange` marker, a crafting-exit copy left in the getting-up state,
  and story `Debug.SendAnimationEvent` playing only on the host's player.
- The separate AI/quest travel lock family and mod-only quest start/stop event parity.
  These are distinct from the corrected read/write provenance in the save warning.

Reproduce and reassess any of these if a standard playtest flow hangs, loses character state
or becomes unplayable. Their accepted visual or diagnostic imperfections alone do not imply
another feature must be built before testing.

## Fixed reports that must not inflate the open count

- The joined game's Dwemer-button covers: local transforms restored, world transforms stale.
  Corrected by publishing the restored scene transforms and applying equal graph rows. Full
  main-menu join, puzzle rings, furniture and potion rejoin pass in the final combined profile.
- The confirmed frozen-VM world-update overlap and the lock checker's mixed reader/writer
  evidence. Their regressions pass; the broader historical reload acceptance remains above.
- Animation bounds/types/project descriptors, graph ownership/destruction, casting/projectile
  guards and actor-spawn readiness are implemented. They need combined-build validation, not
  another item saying they have not been built.
- Both two-player real Fus Ro Dah knockdown tests are already implemented and passing.

The reviewing agent has corrected the old cover-plate loose end and crafted-potion checklist
note. Older progress paragraphs still require status triage: in particular, the Engine Fixes
"untested" note and any earlier potion-rejoin cover failure are superseded by the current
reports. Keep historical failed runs as evidence instead of counting them as new open bugs.
