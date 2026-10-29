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
| Save/load acceptance gate | The VM re-entry cause has a failing-before/passing-after regression; six repeated native cycles pass 30/30. The requested broader gate is still needed: quick/menu/door-auto saves; quick/menu/wipe loads; then 2–3 minutes of follower/combat/item/copy and frame checks. Existing host-load checks already require 180 more frames and a fresh actor spawn; the longer mixed-play check is additional coverage. |
| Party-dialogue acceptance | Implemented, including the party's tagged options and shared highlight. Latest reported reruns: follower trade 3/3, party options 3/3, picked-line response 2/3. Retest the combined build and retain the intermittent failures below. |
| Deleted-follower conversation safety | Confirm or rule out the suspected fixture teardown race. If ordinary play can delete/disable a conversation actor on this path, end the conversation safely and add a regression. It is now an explicit item in the v1 checklist. |
| Final fix-mod profile | All six pinned mods have been acquired and tested. Latest focused combined run is 14/15 without crashes; it does not replace the earlier full 27-case run or Windows acceptance. Resolve or explicitly accept retained failures before recommending a profile. [Matrix](mod-compatibility.md). |
| Real multiplayer acceptance | Separate Steam accounts/invites and 3–4 players, including quests/dungeons, scenes, combat/death, save/rejoin and guest-to-guest Fus Ro Dah. Two local games using the direct session transport do not cover these. |
| Clean player package | Build `test_harness=n`, include UI/assets/dependencies, the bundled creation room and Steam launcher, install without a source checkout, and update the README/setup instructions. `/bug` and `/export` instructions already exist in [playtest.md](playtest.md). |
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
| Arrival can displace the host | **Fixed 2026-10-29.** Reproduced on every main-menu rejoin (47-84 units): the kept record had lost its place (a guest's record replaced it whole, and the place was stored only at saves while connected), so the guest arrived on the save's player spot, the host's own, and its copy pushed the host along at ~40 units/s (`body_trace`). `CharacterRecords::Put` keeps the place, `HostRecords` stores it when a guest leaves, and a kept character without one is placed beside the host. GameTests `[join-placement]` fails without it; `guest-arrival-leaves-host-in-place` (new) 3/3. |
| Movement-record differences after vampire rejoining | The character's vampire state restores and rejoining completes. The final world check finds only movement-record flags on guest clutter `32AFF`, `32AFE`, `32B00` (base `319E5`). The final six-mod run therefore remains 14/15. Whether this is harmless native bookkeeping or a persistence/synchronization issue is not established; do not describe it as lost vampirism or inventory. Evidence: `v1-engine-fixes/build/graph-pose-combined-ussep-matrix-scenarios.log`. |
| Walking timeout after a shout | The cap-120 Display Tweaks run passes knockdown, displacement and getting up, then times out on walking; actual observed updates were about 71/s, not sustained 120 FPS. A similar failure occurred without extra fix mods. Normal two-direction Fus Ro Dah scenarios pass. Retain the higher-FPS result and distinguish geometry/control problems before assigning a cause. [Matrix findings](mod-compatibility.md#failures-requiring-follow-up). |
| Isolated training/gold setup failure | Adding 2,000 gold once left the party at 140 before training opened in the Bug Fixes profile. Training/barter later pass without extra fix mods and in the combined profile. This is an unreproduced setup/flow finding, not a confirmed ongoing Bug Fixes conflict. [Focused comparison](mod-compatibility.md#focused-comparison-without-the-extra-fix-mods). |

Evidence paths beginning `v1-engine-fixes/build/` mean the ignored build directory of
`.worktrees/v1-engine-fixes`, not a committed test artifact. The compatibility reports retain
the other original per-profile logs and failed batches.

## Already accepted or deferred

These remain filed in [bugs.md](bugs.md); they have not silently become additional v1 blockers:

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
