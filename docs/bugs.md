# Known bugs (after v1.0.0)

Bugs we have seen and chose to ship with in 1.0. Each entry: what a player sees, the evidence, where to start, and the
test that shows it. Fixing one: a test in the fastest tier that can show it first (CLAUDE.md), then the fix, then
delete the entry and say where the evidence went. Unfinished work and unexplained behaviour stay in
`docs/progress.md` ("Loose ends"); ideas in `docs/ideas.md`. The fixes and regression evidence for feature 14's
visible actor bugs are in [small-bugs.md](design/small-bugs.md). Native bug fixes and regression evidence are in
[native-bugs.md](design/native-bugs.md).

## Copy-desync follow-ups

Deferred past v1 by the user on **2026-10-31**: all remaining findings from the
[copy-desync batch](reports/2026-10-30-copy-desync.md#retained-final-native-failures), including18 failing
scenarios and earlier intermittent observations. Existing assertions and evidence remain; deferral changes
release scope, not the recorded results. The implemented fixes and fast regressions remain in place.

**Superseding investigation, 2026-10-31:** the three reopened risks are classified and corrected;
all other findings below stay deferred. [Evidence, regressions and exact native results](reports/2026-10-31-v1-three-risks.md).

- **Lock-order reports:** border cell loading and checkpoint alias writes moved from our VM worker to
  between-frame tasks. Mounted/placed reports are native engine order incorrectly attributed through a
  pass-through wrapper; narrow frame attribution corrected, raw strict reports retained. No contention
  observed in the focused probe; this is not proof that an engine deadlock is impossible. All four affected
  scenarios pass the combined acceptance.
- **Enemy-copy death delay:** the scenario retained an unloaded pre-travel copy. The current copy receives
  death state in52ms and replays death in136ms in the original failing run. Reacquire its current copy;
  preserve the original five-second failure and unchanged timeout. Corrected full travel passes last in both
  acceptance batches. The historical103-unit cleanup discrepancy remains recorded separately.
- **Belethor scene/reopening:** real stale cached/effective owner scene after native scene-end cleanup,
  plus hidden guest conversation left open after barter. Active-scene refresh and close lifecycle corrected
  with failing GameTests/units first. Spawned/original host/guest controls all complete two transactions
  in the fresh5/5 run. Combined batch20/22 retains unrelated Sigurd registration and Belethor animation-marker
  failures; neither is exempted or silently closed.
- **Animations drift in dialogue after a while** (user, 2026-10-31). One cause fixed the same day: copies never
  headtracked (progress.md, "copies look where their owners do"). Still open: an accepted replayed action can land
  in a different graph state, and nothing compares it (animation-replay-recovery.md); the proposed fix is to
  compare active states with the owner's snapshot after each replay and load it on a mismatch.
- **Copy visuals/state.** Lydia facing26° differently during follower travel; an intermittent zero animation
  marker during trade (not reproduced in the final run); four kill-move and one HorseExit refusal. One earlier
  wolf refusal was an already-playing duplicate; the other refusals still need classification.
- **Player body/ragdoll.** Kneeling, sunken/lifted skeletons, unsettled death ragdolls and mounted zero-height
  readings. Several occur on the owner too. Revived-player kneeling was already outside v1 scope.
- **Remaining validation.** Owner gait needs a visible-skeleton control; the Greybeards scenario needs its
  documented story save. Combined-dev acceptance remains separate from these deferred bug investigations.

Full scenario names, observed values and ignored evidence paths are in the batch report. A future fix follows
the native scenario -> failing GameTests flow -> unit tests/fix -> passing gate workflow. Reassess priority if
normal play demonstrates a freeze, lost progress or blocked combat/dialogue.

## Party scripts

- **A reference a guest's own script creates is that game's alone** (2026-10-28, party-scripts-2). `PlaceAtMe` is not
  a world change to the router, so a guest's own script (its system quest's `VampireChange`: the change's hidden
  `XMarker`, placed and disabled) makes a reference only its game has; the world check sees it on the guest alone
  (`vampirism-is-each-players` fails on it). Sending it to the host would hand the guest's script None for the
  reference it goes on to use.
  - Start: whether a reference a guest's own script makes is its game's own (the world check and world sync leave it),
    or the host's made for it and sent back.
  - Test: a `vampirism_flows.cpp` case where `VampireChange` places a marker on the guest.

- **The guest's game can crash as the host's werewolf turns back** (2026-10-28, party-scripts-2). In
  `host-beast-form-stays-host` the client crashed in its copy's graph update (hkbBehaviorGraph 58360, after the
  copy refused `IdleWerewolfTransformation`), once in the heap (bhkThreadMemorySource) on dev's DLL, and one run
  ended with the copy still on the transform's idle and the werewolf's values.
  - Out of v1 scope (2026-10-29): repeated transitions pass and the historical crash is unproven; a crash in play arrives
    as a crash bundle ([bug-reports.md](design/bug-reports.md)).
  - Follow-up: graph bounds/types, descriptor invalidation and owner-value restoration are implemented; concurrent animation-snapshot protection is also implemented. Repeated native transitions pass, but the historical crash remains open. See [stability follow-up](v1-session-items-2026-10-28.md#stability-follow-up).
  - Start: what the copy's graph holds when the owner's race changes back (the respawn from its new base data).
  - Test: a `GameTests` race switch back with the copy mid-transform, once the fake keeps a copy's graph state.

- **A story's `Debug.SendAnimationEvent` on the player plays on the host's player only** (2026-10-27,
  party-scripts-2). The Body row reads a native's calls and the functors it queues; this native's graph event is a
  virtual call (`IAnimationGraphManagerHolder` slot 1) on its argument's graph holder, which the code reader does not
  follow (it knows the self and this game's player only).
  - Start: `CodeReach`'s registers know the arguments' registers too (r9 on, for a global), and resolve a virtual
    call on an argument that is a reference against the player's graph holder vtable.
  - Test: `code_reach_tests.cpp` with `SendAnimationEvent`'s bytes (0x140A073F0), then a `cutscene_flows.cpp` case.

- **A guest's copy can stay "getting up" after the guest leaves a crafting station** (2026-10-27, party-scripts-2).
  After the client closed the cooking spit's Crafting Menu, its player went from sit state 4 (want to stand) to 0, while
  the host's copy went from 0 to 4 and stayed there (twice in four tries, both after the menu was closed with a box
  still up or mid-craft; `guest-craft-raises-story`'s own path passed twice clean). The baseline then refuses the next
  scenario ("state.sit_sleep: client (owner) 0 vs host copy 4"). `furniture-client-player` (moveto and activate out)
  passes, so it is the crafting menu's exit.
  - Reproduce: client at QASmoke's spit (0x101973), E, E, Tab while the "create this item?" box is up; read
    `sit_state` on both games.
  - Start: what the owner's graph sends on a crafting furniture's exit, and whether the copy's replay (copies run no
    AI) can finish an exit it began. A `copy_flows.cpp` case once the fake has the furniture states.

## Dialogue

- **A guest crashed once in a conversation whose NPC was deleted** (2026-10-29, party dialogue). In
  `follower-trade-to-picker` the guest crashed in `Actor::UpdateInDialogue` (37541 -> 39372, null RCX) on Lydia's
  copy, already flagged deleted (client prefix `SKSE/crash-2026-10-29-09-52-36.log`). Not reproduced since; the play
  path (`dialogue-npc-removed-mid-conversation`) passes and the scenario baseline now ends conversations first.
  - Out of v1 scope (2026-10-29).
  - Start: what 39372 reads at RCX; whether a copy removed after a trade (container menu closed, conversation still on
    the engine) is the path.
  - Test: a scenario that removes the NPC right after a follower trade, if it ever reproduces.

- **The picked line overlaps in the claw conversation** (2026-10-30, the user, playing the duo). In
  `dialogue-options-are-the-partys`, when an option is picked the dialogue text shows duplicated, overlapping. Not
  captured. Likely `DialogueBridge::ShowPicked` writing the picked line into `TextCopy_mc` where the menu already
  shows one (the claw topic lists two lines).
  - Out of v1 scope (2026-10-30, the user): small and visual.
  - Start: a screenshot of both menus at the pick (`coop_test.py screenshot`) in that scenario.
  - Test: the scenario's pick, both screens.

- **A guest's dialogue press in the frame its greeting ends is lost** (2026-10-29, v1 triage). The guest's engine shows
  its list again as its copy's greeting ends; the menu already shows the party's list, so that show is skipped
  (`DialogueBridge` `HookShowList`; running it instead resets the 750 ms pick delay, worse). A press in that same frame
  never reaches `TopicClicked` (the SWF stays `allow_progress` 1, state 1); a player presses again. 1 in ~5 runs while
  `dialogue-pick-reaches-host` pressed as the host's greeting ended; 20 of 20 since it waits for the client's greeting.
  - Out of v1 scope (2026-10-29): a one-frame window.
  - Start: what the engine does at the greeting's end beyond `ShowList` (52857) that the skip leaves out; whether
    vanilla drops a press in that frame too (`repro --vanilla`).
  - Test: `dialogue-pick-reaches-host` without its wait for the client's greeting, `--until-fail 20`.

## Party travel

Party travel was accepted for v1.0.0 testing by the user on 2026-10-27. Jail cleanup, horse packages, physics
carry/loss checks and the release-time physics lock ordering now have fixes and evidence in
[native-bugs.md](design/native-bugs.md). The separate AI/quest pair below remains deferred by the user (2026-10-28).

- **An AI/quest lock inversion was reported during travel** (2026-10-27, party-travel). The departure timing
  assertions pass, but `party-loads-overlap` fails the scenario's final lock audit. The reported path is native
  low-actor AI/package evaluation -> `TESQuest::sub_25066`, through the existing `HookUpdateAI`; the opposite
  pair is quest alias processing. No deadlock occurred in that run. Evidence:
  `.worktrees/party-travel/build/final-native-lock.log` and `build/regression-final-native.log`.
  - Start: reproduce the pair on unchanged dev and inspect quest/AI lock ordering; that baseline comparison
    has not been performed. This is not evidence that the new destination announcement itself holds these locks.
  - Baseline (2026-10-28, party-scripts-2): dev's own DLL (`e4085691`) reports the same pair in `dive-host-player`
    (`SkyrimSE.exe+0x1823d0 > +0x3d1507 vs +0x3cdf46 > +0x1820ef`), untagged because its first reporter had none of
    our frames. The pair: an AI job's package conditions (`ExtraAliasInstanceArray` 12721, then `GetIsAliasRef` and
    `TESQuest::sub_25066`) against the guest's quest sync forcing an alias on the main thread
    (`GuestEngine::ApplyFills` -> `TESQuest::ForceRefIntoAlias` 25052 -> `ExtraAliasInstanceArray` 12718). Whether it is
    tagged ours depends on which thread reports it first (`HookUpdateAI`, `HookCheckForNewPackage` count as ours).
  - 2026-10-28, adaptive playback run: `party-fast-travels` failed once on the client's lock audit with the same
    family (`SkyrimSE.exe+0x3cdb45 > +0x182241 vs +0x1823d0 > +0x3d1507`), then passed on the rerun.

- **A guest's follower can stop following after being teleported and a fight** (2026-10-29, v1-gate, `save-load-gate`).
  After the harness took the party to the inn and home with `coc` and put Lydia beside it with `move_actor` (she
  unloads and loads again), a wolf fighting the guest's copy left her on `PlayerFollowerPackage` (5C84B) with no
  package or follow target, never resolving it again (30 s+); she was never in combat herself. One
  `evaluatepackage` resolves the copy again (our `FollowerTargets` resolve hook) and she follows. 3 of 3 when the door
  round followed the Load-menu round, 0 of 2 started at the door, 1 of 1 recovered in a plain fight after one round
  trip. Players have no `coc`/`move_actor`; the gate now runs the door last, so it no longer stages her before a
  fight. A re-evaluation on her combat end did not help (no combat event of hers). Evidence: the gate4-6 and probe
  logs of the v1-gate session (`FollowersDiag` lines: resolves keep arriving and are substituted).
  - Start: a case without the harness teleport (a follower left behind that catches up, then a fight); then which
    engine path writes the package target without 29846.
- **The guest's copy of a follower once played another action** (2026-10-29, v1-gate). `save-load-gate`'s desync
  check after the door round's fight: Lydia's `animation.action` on the host 522467 vs the client's copy 297. Once.

## Plugin compatibility

- **Quest start/stop notification parity is deferred with mod work.** Vanilla guest journal, stage and completion
  synchronization works, but a plugin consuming only TESQuestStartStopEvent can miss a stop. The audit removed
  the additional relay after it produced host stop 1 / guest stop 2 on a native turn-in
  (`build/check/evidence-audit/native-gameplay.log`). Experience and Skill Uncapper are outside v1.0.0 scope.
  - Start: investigate actual native notification sources with the intended consumer installed. Do not synthesize
    another stop from flag changes or add a timing-based duplicate filter. Native counters remain available through
    `quest_fragments`; journal/reward tests keep their vanilla stage and completion assertions.

- **Movement-record flags on guest clutter after a vampire rejoin** (B8, deferred past v1 by the user 2026-10-29).
  With all six fix mods, `vampirism-kept-across-rejoin` passes its behavior checks, then the world check finds only
  movement-record change flags differing on three clutter references (`32AFF`, `32AFE`, `32B00`, base `319E5`), set
  only on the guest; positions agree within the check's tolerance. Once, not seen on vanilla (2026-10-29: the scenario
  passes with no world difference and `unsynced` reports every host change sent with a row). A guest's game refuses
  every save, so its records never persist. Evidence: [mod-compatibility.md](mod-compatibility.md),
  [lock-history follow-up](re/crashes/2026-10-29-lock-history-and-button-pose.md).
  - Start: `coop_test.py unsynced` and `save-diff` right after the scenario on the six-mod profile; if the host
    lacks a move the guest has, which engine path moved the clutter on the guest only.

## Physics and ragdolls

The user accepted these three remaining reports for v1.0.0 on 2026-10-28. Further fixes are deferred.
The audit removed unsuccessful pose reconstruction; existing native checks and failed traces remain.
Retained changes and their evidence are in [native-bugs.md](design/native-bugs.md).

- **A respawned guest can stay ragdolled, sunk into the floor** (2026-10-29, v1 triage). `death-spectate-guest` once
  timed out on "the respawned player can walk again": the guest's player had its 18 ragdoll bodies awake and stood
  49 units into the floor after its revive. 0 of 14 runs since.
  - Out of v1 scope (2026-10-29).
  - Start: what Revive (`PlayerCharacter::Revive`) leaves of the knock state and ragdoll when the respawn moves a dead
    body; the open revive item in [death.md](design/death.md) (a revived player knocked down stays getting up).
  - Test: `death-spectate-guest --until-fail`, then a GameTests revive flow once the fake keeps knock state.

- **A revived guest's own body kneels in its next conversation** (2026-10-29, cause narrowed 2026-10-30). Once
  thought to follow a follower trade: it follows a death. `follower-anchor-dies` then `follower-anchor-saved`
  reproduces it: right after the revive the guest stands; at the next scenario's first conversation its own body
  kneels (pelvis ~29, head ~55, like the bleedout pose), then stands again later. Once its death ragdoll was left
  awake too (18 bodies). Only the guest sees it (third person); the host sees its copy standing. A later
  conversation with no death first does not show it.
  - Out of v1 scope (2026-10-30, the user): cosmetic, the guest's own view only, after a death.
  - Start: the behavior graph's state after `PlayerCharacter::Revive` (it resets neither the graph nor its state
    machine; the open revive item in [death.md](design/death.md)), and what the conversation sends the graph.
  - Test: `scenario follower-anchor-dies follower-anchor-saved` (its mid-conversation body check), then a GameTests
    revive flow once the fake keeps graph state.

- **Brief get-up timing or facing differences.** Basic owner timing, get-up preparation/action and iGetUpType
  synchronization remain. All six regular physics/death/revival scenarios pass once in the reduced build
  (`build/check/evidence-audit/native-physics.log`); this does not establish that the historical intermittent
  timing/facing differences are gone.
  - Start: compare native owner/copy action timing and get-up choice in `knockdown-front-and-back`. Earlier failed
    experiments and traces are retained; implementation-only cache tests did not establish native fidelity.
- **Dead guest's copy can have a displaced pose.** The original report was a 16-unit body offset with three bones
  displaced up to 17 units. The reduced build passes one complete guest death/revive and one kill-move scenario
  (`build/check/evidence-audit/native-physics.log`). Retained as accepted; consecutive verification is incomplete.
  - Start: repeat `death-spectate-guest` and compare the owner/copy `RagdollTrace` if alignment fails.
- **Occasional ragdoll bone jitter or get-up snap.** The reduced build passes regular lying, knockdown smoke and
  kill-move checks. The added front/back check still catches jitter, with about 11 units of visible owner/copy
  bone difference (`build/check/evidence-audit/native-gameplay.log`). The final smoke also catches a 13-unit
  skeleton snap for one frame at knock state 5->6 (`build/check/evidence-audit/shadow-native.log`).
  - Start: inspect the native knock-state 5/6 handoff and per-bone trace. Compare visible owner/copy poses as well
    as body alignment; the owner's own get-up bones can also leave its physics bodies.
  - 2026-10-28, adaptive playback ([copy-clock.md](design/copy-clock.md)): `knockdown-lies-as-host` fails most runs on
    the client's copy (a bone turning back 3-5 times in a second, 1-16 flickers, a bone up to 21 units off its body).
    It is not the new clock: 1 of 4 passed on it, 0 of 4 on a build with the old fixed 300 ms and 10/s timing
    (scenario samples under `build/coop-test/runs/`, 2026-10-28 22:1x).
- **A chest's lid can rest in another pose on the two games after its open is interrupted** (2026-10-28, live menus).
  The host saving while a guest's chest menu opens, or the chest disabled while open, leaves its `DoorLeft` node turned
  differently on the two games (the world check after `host-saves-with-guest-menu-open`, sometimes
  `guest-menus-close-when-target-goes`). The items and the open state agree; only the lid's pose differs.
  - Start: compare the lid's animation state on both games after the interruption (the chest's `BGSOpenCloseForm`
    open state, `SetOpenState` 14287 in form-state.md) and replay the host's final open state.
- **A killed wolf's body once took over 10 s to come to rest on the host** (2026-10-28, adaptive playback run).
  `client-melee-kill-credit` timed out at "[host] the wolf's body comes to rest" once in 20 scenarios and passed on
  the rerun. The host's own body: the copy clock does not play it.
  - Start: repeat `client-melee-kill-credit` and read the host's `BodyService` rest log for the wolf.
