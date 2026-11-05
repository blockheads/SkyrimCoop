# Open bugs

Every entry is a valid, open bug: none is accepted or shipped with. Each entry: what a player sees, the evidence, where to start, and the
test that shows it. Fixing one: a test in the fastest tier that can show it first (CLAUDE.md), then the fix, then
delete the entry and say where the evidence went. Unfinished work and unexplained behaviour stay in
`docs/progress.md` ("Loose ends"); ideas in `docs/ideas.md`. The fixes and regression evidence for feature 14's
visible actor bugs are in [small-bugs.md](design/small-bugs.md). Native bug fixes and regression evidence are in
[native-bugs.md](design/native-bugs.md). Fixes that landed after a playtest (S2's green flash, the
2026-11-01 dialogue fixes) keep their evidence in `playtests/` and a dated entry in `docs/progress.md`.

## Copy-desync follow-ups

All remaining findings from the
[copy-desync batch](reports/2026-10-30-copy-desync.md#retained-final-native-failures), including18 failing
scenarios and earlier intermittent observations. Existing assertions and evidence remain. The implemented fixes and fast regressions remain in place.

**Superseding investigation, 2026-10-31:** the three reopened risks are classified and corrected;
all other findings below stay open. [Evidence, regressions and exact native results](reports/2026-10-31-v1-three-risks.md).

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
- **Animations drift in dialogue after a while** (user, 2026-10-31). Fixed 2026-11-04: copies played clips before their
  files were in, and each game made its own random picks (animation-replay-recovery.md). One cause fixed earlier: copies never
  headtracked (progress.md, "copies look where their owners do"). Still open: an accepted replayed action can land
  in a different graph state, and nothing compares it (animation-replay-recovery.md); the proposed fix is to
  compare active states with the owner's snapshot after each replay and load it on a mismatch.
- **Players' idle variants can differ between games** (2026-11-04). Sync points (animation-replay-recovery.md, "A
  graph's random picks") are sent for NPCs only: a player's own character on the other game still makes its own random
  picks for gestures and idle variants no action starts.
  - Start: `AnimationSystem::Serialize` skips the player; a player's copy needs the same sync point, and a check that
    the player's first-person graph is not the one read.
  - Test: an `npc-counter-matches`-style scenario on a player's copy.
- **Animation mods' random picks are not synced** (2026-11-04). Open Animation Replacer's Random condition draws from its
  own generator, not the graph's, so each game can choose a different replacement clip.
  - Start: OAR's `RandomCondition` state data (`reference/plugins/OpenAnimationReplacer/src/Conditions.cpp`).
  - Test: a modded run through `npc-clips-match` first, to see what breaks.
- **A guest walking in on an NPC mid-loop is untested** (2026-11-04). `npc-counter-copy-falls-back-in` forces the copy
  out of step by loading the host's graph into it: the party tether brings the guest along, so a real late arrival
  cannot be staged. Sync points should cover it, unproven.
  - Test: a scenario where the guest's copy spawns while the owner is mid-loop (a guest joining or crossing a cell
    boundary later).
- **Creature copies wobble a little around their goal** (2026-11-05). A stepped wolf's or deer's copy on the guest's game
  turns back on itself up to 326 times in 25 s, its biggest steps 1.6 to 3.3 units (seen on far-off creatures, `9F361` and
  `8CACD` in Riverwood; owners do not). Not in furniture, so not the fixed vibration (actor-movement.md).
  - Start: copies' goals are whole units (`Vector3_NetQuantize` truncates), while a step settles to 0.05.
  - Test: `jitter` on a creature, then a ClientTests case on the goal it is given.
- **A town's world comparison finds ambient fish** (2026-11-05). `copy-leaves-furniture-steady` passes all its steps, then
  fails the end comparison: about 55 references (ACTI `10C3BA`, fish controllers) posed differently in the two games,
  as `faendal-letter`'s end comparison fails on things around it.
  - Test: that scenario, after its steps.
- **Hod's head height differs after he leaves furniture** (2026-11-05). One of three fixed runs of
  `copy-leaves-furniture-steady` ended with `pose.head` 117 (host) against 88 (copy). It may be the stale off-screen skeleton
  of [culled-npc-pose-readings.md](re/systems/culled-npc-pose-readings.md) (81-88 against 122) or a copy left seated by being
  placed, not stepped: not told apart.
  - Start: look at the copy with the camera, as that document did, and read `pose.head` again.
  - Test: that scenario's end comparison.
- **Headtracking left over** (2026-10-31, v1-acceptance: 17 scenarios fail `ai_state.head_track`). A copy's own
  ProcessTracking still picks an ambient look target (its game's player) over its owner's slots, and a player's
  default slot on itself reads as none on its copy.
- **The host's own NPCs move without walking after arriving in Riverwood** (2026-11-04, seen in every
  `actors-walk-after-arrival` run on dialogue-anim-desync: 0 of 6, the same with and without its changes). Owners, not
  copies: Dorthe, Frodnar or Sigrid move 117 to 574 units in 2 s with their feet still (0 to 23), and in some runs copies
  too (Embry, Hod, 135 to 169 units, feet 3 to 40). Also listed under progress.md item 4, "failing the same with the old
  copy placement".
  - Start: whether owners teleport (package start or arrival placement) or slide while their graph stands; the
    scenario's `check_gait` names each actor and its distance.
  - Test: `actors-walk-after-arrival`.
- **Kill moves leave the guest's wolf copy looking at a player** (2026-11-04, `host-kill-move` and `client-kill-move`:
  0 of 6 each on dialogue-anim-desync, the same with and without its changes). The kill move itself plays; the
  scenario then fails its final comparison: `ai_state.head_track` host (owner) `[None x6]`, guest copy
  `['session 2', None x5]`. Most likely the "Headtracking left over" entry above, unproven. It hides any later
  kill-move failure, since the scenario stops there. One unexplained one-off besides: `host-kill-move` timed out on
  `[host] player's kill_move is True` before the comparison, not seen again in six runs (progress.md "S8 missed kill
  moves" may be the same). Evidence: dialogue-anim-desync worktree, `build/coop-test/results/20261104-012732-4d8ec7` and
  `20261104-012746-3e87e9`. Another agent (kill-move-review) works on kill moves.
  - Test: `host-kill-move`, `client-kill-move`.
- **Copy visuals/state.** Lydia facing26° differently during follower travel; an intermittent zero animation
  marker during trade (not reproduced in the final run); one HorseExit refusal. The kill-move refusals were
  the pair's other half coming back, now skipped (`docs/re/systems/kill-moves.md`).
- **Player body/ragdoll.** Kneeling, sunken/lifted skeletons, unsettled death ragdolls and mounted zero-height
  readings. Several occur on the owner too. Revived-player kneeling is the same bug.
- **Remaining validation.** Owner gait needs a visible-skeleton control; the Greybeards scenario needs its
  documented story save. Combined-dev acceptance remains separate from these bug investigations.

Full scenario names, observed values and ignored evidence paths are in the batch report. A future fix follows
the native scenario -> failing GameTests flow -> unit tests/fix -> passing gate workflow.

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
  - Not reproduced (2026-10-29): repeated transitions pass and the historical crash is unproven; a crash in play arrives
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

- **Lydia's greeting can remain queued on the guest before any pick** (2026-11-01, Linux/Wine audio off).
  `dialogue-pick-reaches-host` fails at its existing voice-end precondition on a fresh duo as well as after Onmund.
  Capture: callback 3 / sound 162 / seconds -1, subtitle "Hey, slug-breath!"; the host's greeting has ended.
  Cause and regression status unknown; still open. [Evidence and limits](../playtests/2026-11-01-dialogue-fixes.md#remaining-voice-failure-and-limits).
  The duo had no audio driver then (voice lines queued, never started; a silent device since 2026-11-03,
  progress.md): rerun before deleting.

- **A guest crashed once in a conversation whose NPC was deleted** (2026-10-29, party dialogue). In
  `follower-trade-to-picker` the guest crashed in `Actor::UpdateInDialogue` (37541 -> 39372, null RCX) on Lydia's
  copy, already flagged deleted (client prefix `SKSE/crash-2026-10-29-09-52-36.log`). Not reproduced since; the play
  path (`dialogue-npc-removed-mid-conversation`) passes and the scenario baseline now ends conversations first.
  - Not reproduced (2026-10-29).
  - Start: what 39372 reads at RCX; whether a copy removed after a trade (container menu closed, conversation still on
    the engine) is the path.
  - Test: a scenario that removes the NPC right after a follower trade, if it ever reproduces.

- **The picked line overlaps in the claw conversation** (2026-10-30, the user, playing the duo). In
  `dialogue-options-are-the-partys`, when an option is picked the dialogue text shows duplicated, overlapping. Not
  captured. Likely `DialogueBridge::ShowPicked` writing the picked line into `TextCopy_mc` where the menu already
  shows one (the claw topic lists two lines).
  - Found 2026-10-30: small and visual.
  - Start: a screenshot of both menus at the pick (`coop_test.py screenshot`) in that scenario.
  - Test: the scenario's pick, both screens.

- **A guest's dialogue press in the frame its greeting ends is lost** (2026-10-29, v1 triage). The guest's engine shows
  its list again as its copy's greeting ends; the menu already shows the party's list, so that show is skipped
  (`DialogueBridge` `HookShowList`; running it instead resets the 750 ms pick delay, worse). A press in that same frame
  never reaches `TopicClicked` (the SWF stays `allow_progress` 1, state 1); a player presses again. 1 in ~5 runs while
  `dialogue-pick-reaches-host` pressed as the host's greeting ended; 20 of 20 since it waits for the client's greeting.
  - Found 2026-10-29: a one-frame window.
  - Start: what the engine does at the greeting's end beyond `ShowList` (52857) that the skip leaves out; whether
    vanilla drops a press in that frame too (`repro --vanilla`).
  - Test: `dialogue-pick-reaches-host` without its wait for the client's greeting, `--until-fail 20`.

## Party travel

Party travel was ready for v1.0.0 testing on 2026-10-27. Jail cleanup, horse packages, physics
carry/loss checks and the release-time physics lock ordering now have fixes and evidence in
[native-bugs.md](design/native-bugs.md). The separate AI/quest pair below remains open (2026-10-28).

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
  - Not reproduced that way (2026-11-03, repro MCP, 1 of 1): recruited, the party away and back, a wolf on the guest: on
    the host she kept both targets. Read it on the host: a copy's `ai.*` is 0 by design.
  - Not reproduced with the Load-menu round either (2026-11-04, 1.7.104, 0 of 2): recruited by the guest, Load menu, a
    wolf, the door round with its `coc` and `move_actor`, the door's autosave loaded, a wolf. After each fight her
    targets read 0 while she ran her combat package, then returned to the guest's copy in 9-15 s. The entry's 3 of 3
    were on 1.6.1170 with a longer fight (`plays_on`, 60 s of healing); the targeting code has not changed since.
  - The 1,388 lead is closed: she stands by her anchor, not the host. In QASmoke she stood 92 units from the guest and
    1,364 from the host, targets set. Separately, she does not walk across QASmoke (1,500 units, guest or host anchor,
    so not our target); outdoors she caught up every time (both anchors, 1,500 units each way, about 9 s).
  - Also ruled out: the guest killed in the fight, respawning as a new copy while she fights. She turned to the host,
    then took the new copy about 12 s after the fight.
  - The entry's question, read in IDA: `BGSProcedureFollowExecState` caches the target at +8 and each update (28640)
    copies it to `followTarget` without 29846 ([followers.md](re/systems/followers.md)). So a stale cached handle
    would look like this entry; nothing has shown one yet.
- **The guest's copy of a follower once played another action** (2026-10-29, v1-gate). `save-load-gate`'s desync
  check after the door round's fight: Lydia's `animation.action` on the host 522467 vs the client's copy 297. Once.

## Saves

- **A party reload's scratch save tops the Load list** (2026-11-04, Linux). `SkyrimCoopWipe` stays in the Saves folder
  after a reload and sorted above a newer quicksave, so "the newest save" loaded an older world: the slot held the
  previous run's Lydia. Seen in the harness, which now deletes the file first; a player sees the same list.
  - Start: whether the scratch save can leave the list once its load is done (`HostServiceReload.cpp`).
  - Test: a `host_load_flows.cpp` case.

## Plugin compatibility

- **Quest start/stop notification parity is open (mod work).** Vanilla guest journal, stage and completion
  synchronization works, but a plugin consuming only TESQuestStartStopEvent can miss a stop. The audit removed
  the additional relay after it produced host stop 1 / guest stop 2 on a native turn-in
  (`build/check/evidence-audit/native-gameplay.log`). Experience and Skill Uncapper compatibility is not started.
  - Start: investigate actual native notification sources with the intended consumer installed. Do not synthesize
    another stop from flag changes or add a timing-based duplicate filter. Native counters remain available through
    `quest_fragments`; journal/reward tests keep their vanilla stage and completion assertions.

- **Movement-record flags on guest clutter after a vampire rejoin** (B8, 2026-10-29).
  With all six fix mods, `vampirism-kept-across-rejoin` passes its behavior checks, then the world check finds only
  movement-record change flags differing on three clutter references (`32AFF`, `32AFE`, `32B00`, base `319E5`), set
  only on the guest; positions agree within the check's tolerance. Once, not seen on vanilla (2026-10-29: the scenario
  passes with no world difference and `unsynced` reports every host change sent with a row). A guest's game refuses
  every save, so its records never persist. Evidence: [mod-compatibility.md](mod-compatibility.md),
  [lock-history follow-up](re/crashes/2026-10-29-lock-history-and-button-pose.md).
  - Start: `coop_test.py unsynced` and `save-diff` right after the scenario on the six-mod profile; if the host
    lacks a move the guest has, which engine path moved the clutter on the guest only.

## Combat

- **Crossed kill moves still kill a player, a second later** (2026-11-04, found through the repro MCP on PR #13's
  branch, `agent/kill-move-review`, with its fix). Both players, one hit from death, swing at each other at once; each
  game starts its own pair and both clear `kill_move` within 0.2 s, so PR #13's `kill_move_exchange` passes its checks
  (within `KILL_MOVE_CLASH`, 1.5 s). Then one player dies anyway: 3 of 3 runs (the host player twice, the guest once).
  - Trace (both games, seconds from the host's `pa_KillMove1HMStabUpFace`): 0.02 the guest's own
    `pa_KillMove1HMGrappleStab`; 0.16-0.22 `KillMoveStart` on both players in both games; 1.26 the host's `HitFrame`
    (its pair runs on); 1.32 the guest's `Ragdoll`, `life=1`, `KillMoveEnd`. So the clash ends neither pair's clips:
    whichever pair reaches its `HitFrame`/`KillActor` first kills.
  - Repro, recorded and exported through the MCP (needs PR #13's verbs, so it lands with that branch; it failed alone
    with `coop_test.py scenario`, 20 s):
    ```python
    ctx.name_players()
    ctx.console_on('', 'set KillMoveRandom to 100')
    ctx.console_on('client', 'set KillMoveRandom to 100')
    ctx.equip_weapon('', 0x12EB7)
    ctx.equip_weapon('client', 0x12EB7)
    ctx.kill_move_exchange({game: ctx.other_player(ctx.actors_on(game)) for game in ('', 'client')})
    ctx.expect('host_player', 'stays', value=False, field='state.dead', seconds=6.0)
    ctx.expect('guest_player', 'stays', value=False, field='state.dead', seconds=6.0)
    ```
  - Run on PR #13's final pair fix (`8c8c5115`, `7e6b2e1d` included). The player who died was the victim of the other
    game's half, which fits `kill-moves.md`, "Early deaths: the victim touched a wall" (the engine's own `KillActor` on
    ragdoll contact; QASmoke's staging is near walls). Start there: run the repro with that section's contact hook and
    see whether the death follows a wall contact; if it does, this is that deferred case, not the clash.
  - `kill-moves-cross` (now on dev) checks within 1.5 s; the `stays` expects above cover the later death.

- **The host's player copy on a guest has no name** (2026-11-04, seen through the MCP's `look`): both characters
  are "Prisoner" (`player_record`), the guest's copy on the host shows it, the host's copy on the guest shows "". Seen
  before and after a host load. Start: where a copy's display name comes from the owner's character record.
- **A scenario whose player died leaves the baseline check failing** (2026-11-04): `kill-moves-cross-late-death` ended
  with "cleanup: left behind: host: player at ..., baseline ...". The dead and respawned player isn't put back home,
  so the next flow starts off its baseline. Start: the runner's cleanup after a player's death (`reset_baseline`).

## Found in the 2026-11-04 playtest (triaged from the logs and code; IDA not reached)

The user's report, one entry per point. The evidence is the 18:11 `/bug` and the 18:13 `/export` of that session: the
guest's `SkyrimCoop-Courier-2026-11-04-1813-e25.zip` (and the host's `reports/2026-11-04-1811-d3b/host-Dragonborn/`),
both in `build/coop-test/prefix-client|prefix-host/.../My Games/Skyrim Special Edition/SkyrimCoop/`. Times below are the
logs' `mm:ss` from the host log's start (00:03:46 = 18:03:46 local). The IDA MCP's tools did not reach this session, so
the engine paths are read from code and still to be proved in step 0 (CLAUDE.md).

- **A guest's thefts earn no bounty** (2026-11-04). The user took items (a book among them) in Whiterun with an NPC
  watching; neither game showed a bounty.
  - Evidence: the thefts reach the host and are judged. The host log has 16 `Party crime: actor FF00085D, action 0
    (Theft)` lines (05:34-06:13), among them the book `1060AD` at 06:07.191, 48 ms after the Book Menu's `Accept` in the
    guest's input log. That line comes after `CrimeBridge::Judge`'s cell and `owns` (20210) checks, so the request was
    valid, the target was in the actor's cell, and the host's native `RealSteal` (37422) then ran. `1060AD` is not
    deleted afterwards (the host's physics still rests it at 06:21). No `NotifyCrimeState` was ever sent before the
    wipe (`messages.txt`: one, at 11:38), so the party's bounty never changed. So the "request lost" suspects of the
    first triage (a deleted target, no session id) are out; the failure is inside the host's native alarm.
  - Suspects, in order: (1) the permission check uses the host's real player: `owns(target, player, true, false)`
    answers for the host's character, not for the guest's, so an item the host's character may take (its house, a
    faction it belongs to, a start choice from "Alternate Start") is no theft; the guest's own game reached
    `StealAlarm` because for its character the item is owned. (2) `RealSteal` runs with `aTotal` 0 and the native
    item path wants no value, but a witness is needed: the shopkeeper must detect the guest's host-side copy
    (`HookProcessKnowledge`/39515 redirect only inside `ScopedCriminal`); `guest-theft` proves it for a chest and a
    spawned witness, not for a townsperson across a room. (3) The alarm's owner argument (`target->GetOwner()`) is null
    for a ref whose owner is the cell or a faction, so `StealAlarm` has nobody to wrong.
  - Start: log, at each `Party crime`, `owns(...)` for the host's player and for the copy, `target->GetOwner()`, and the
    detection level of the nearest NPC against the copy (the `crime` harness action `detection` does it). In IDA, read
    `StealAlarm` 37422 and its caller `PlayerCharacter::PickUpObject` 37521 for what makes a pickup a crime
    (owner, value, witnesses), and record it in `docs/re/systems/` (no `crime.md` yet).
  - Test: a `guest_loose_theft` scenario (an owned loose item, a watching NPC, the guest takes it; the party's bounty
    must rise), then a `CrimeFlows` case where the host's character has permission the guest's lacks.

- **The guest's aggro goes to the host** (2026-11-04, user: "after a while I aggro'd the NPCs, which attacked the
  host at once"). Not found in the logs: the host log has no aggro line (the readouts that would show it, the
  `combat_targets` list, are test-build only), and the bug report was filed after the fights.
  - Code: `HostAggro` / `party-aggro.md` choose between players only after a party player damages the NPC, and only
    while the native selector already picks a party player. An NPC the guest aggroed any other way (seen, a theft,
    a shout, sneak broken) keeps Skyrim's own pick, which the design doc measured to be the visible host.
  - Start: ask the user how the guest aggroed them (QUESTION 1); reproduce with `fight` from the guest with the host
    nearer and no damage first.
  - Test: GameTests: the guest starts combat with no damage, the host nearer; assert the NPC's target (the fake
    selector must prefer the real player, as measured).

- **After the host died, NPCs half-stop fighting the guest** (2026-11-04). They drift around looking at the guest, hit
  only up close, and stop when the guest walks away.
  - Evidence: the host's player died at 07:09 (`PlayerDeathRequest`, then `NotifyPartyDeath`) and respawned at 07:19,
    died again at 07:57 and **stayed dead for 3.5 minutes** (no `NotifyPlayerRespawn`) until the guest died at 11:31
    and the party loaded `SkyrimCoopWipe.ess` at 11:35. `death.md`: the respawn waits for the fight to end, 10 s after
    the combat clears. So the NPCs stayed "in combat" the whole time without fighting the guest: the stuck
    state is one of the bug, not only of the movement.
  - Suspects: (1) the NPCs' target is still the dead host (`HostAggro` drops damage history for a dead victim, not
    the NPC's native target); a dead real player is unreachable for pursuit and attack, but they still count as
    "in combat" for `HostDeath`. (2) The target is the guest's host-side copy, a created `Character` with no AI,
    which pursuit/attack code written for the `PlayerCharacter` cannot track (melee range works through the copy's
    collision and hearing; chase needs detection that is redirected only inside scoped crime). (3) The combat group's
    other-player candidate (the copy) never gets the native pick once the real player is dead.
  - Start: reproduce with the MCP: the host dies in a fight the guest is also in (`scene`, `fight`), then `observe`
    an NPC on the host: controller target, `combat_targets`, detection against the copy, package, whether
    `IsInCombat`; and watch `HostDeath`'s timer. In IDA, read how the combat controller treats a dead target and a
    non-player target (selector 47191, validity 47196, target setter 33235).
  - Test: a scenario `host-dies-guest-still-fought` (the NPC must close in on a guest who steps back); a GameTests flow
    with a fake dead host player that checks the NPC's target is the guest and the respawn timer still runs.

- **Freezes and slowness** (2026-11-04). The same export: 2845 frames over about 8 minutes, p50 17 ms, p95 32 ms, p99 71
  ms, worst 2.6 s (`snapshot.json` `perf`), and the plugin's own `Frame stalled` warnings: 70 on the host, 77 on the
  guest, about 6 a minute.
  - Our code is not the cause of most: in 62 of 70 host stalls "our last update" took 1-6 ms. The 8 where it did:
    1127 ms at 11:32 (the wipe's save load, `longest_frame_ms` 52 s), 499 ms at 11:37 (the new game after it), 275 ms
    (05:04), 154 ms (05:15) and a few more; the guest's 548 ms at 11:37 and 250 ms at 11:49 go with the load too.
  - The long stalls (1-2.6 s) hit **both games at the same instant**: 05:07.447 on the host (578 ms) and on the guest
    (578 ms), 05:10.434 (573 / 574), 06:42.136 (664 / 603), 10:46.77 (2402 / 2466): the two games run on one machine,
    so a shared cause (CPU/GPU, disk, Wine) stalls both. No kernel BUG in `journalctl -k` for the window; ext4
    `nvme0n1p6`. That the stalls often end with no work of ours fits P1 perf (`playtests/2026-10-31-fabien.md`).
  - Also logged: 17 `LockOrder: lock order inversion` errors on the host (04:42 on, three more at 11:37 right before
    the long stalls; several "[ours]"). Whether they cost time is not shown; they are real inversions to read.
  - Start: `scripts/coop_test.py profile --stacks` on this scene (Whiterun, both games) to see which threads wait at a
    shared stall; the two games' stalls lining up points at the machine, not the plugin; separate it with one game
    alone (`vanilla_start`) over the same route.
  - Test: the `profile` run with a frame-time budget; later a GameTests case if a stall turns out ours.

## Physics and ragdolls

### Found 2026-11-01, not yet triaged

From the S1 work (`playtests/2026-10-31-fabien.md`; `docs/design/physics-sync.md`, "Steered holds" and "Matches a
game without co-op").

- **A guest's held object can pass through a host NPC in the guest's own view.** The host now steers the hold, so the
  host and the other guests see it stop on her head, but the holder shows its own physics until the host's rest. Seen
  once (the first `guest-bucket-on-npc-head` run: the guest's bucket 50 units over Lydia's feet, her head at 124);
  later runs stopped on her copy's head (127 to 170).
  - Start: copies skip `Actor::ApplyMovement` (37356), which integrates their character controller, and a
    controller keeps bodies out as it integrates (object interactions, 79133). The copy's controller reads no
    `kSupport` or `kCanJump` (flags 40200000 against the host's NPC's 40240500: `docs/re/systems/physics.md`, "What
    stops a held body"). Find what made the first run differ, then whether the copy's controller pushes at all.
  - Test: `guest-bucket-on-npc-head --until-fail`, reading the client's lowest; then teach the fake (it already
    has copies push nothing: `FakeEngine::OutsideCapsules`).

- **A dragged object blinks in and out of existence** (the user, 2026-11-01). Not reproduced (2026-11-01): a ladle
  dragged left and right by either player, traced each frame on both games (`body_trace`, which now also gives its 3D's
  flags), was never hidden or culled (`kNotVisible`) and never left its body on the dragging game, with the host's
  node writes as they were then and as fixed.
  - Start: which game blinks (the holder's or another's). On the others the held object is played from the host's
    frames now (steered on the host, sent by HostPhysics); a frame missing or late, a rest placed while it is held,
    or its node written and then left behind (FollowBody, now only when left behind) could each show it elsewhere for
    a frame. The fireball scenarios' `FlightTrace` catches a jump or a flicker in a flight; nothing traces a hold.
  - Test: a scenario that drags an object across the room while both games trace it (`body_trace`), failing on a
    frame where its 3D leaves its path.

- **The guest's playback of a long roll jumps once** (2026-11-01). `physics-push-reaches-guest` (1 in 4: a ladle
  rolling 17 s, a 9.2-unit jump 11.6 s in) and `guest-cast-moves-host` (1 in 4: a jump just over the 6-unit limit)
  fail `FlightTrace.check`; both pass on reruns. Physics-sync's notes had one such failure (0.4 over) on 2026-10-25.
  - Start: the trace at the jump (the samples folder the failure names): a late frame's correction, or the host's
    own 3D (its node left behind) when the frame was read.

- **Host objects wake and rest every half second for minutes** (playtest timeline, 14:53-14:54: the host's
  `9E63A`-`9E63E` each sent 1 to 4 frames, then rested again, about twice a second). In the duo a bucket spawned with
  `placeatme` sometimes never came to rest (2 of about 10 setups, 15 to 30 s).
  - Likely the host re-placing every awake object's node (fixed 2026-11-01): with the old writes back, a ladle the
    host dragged never came to rest within 30 s (2 of 2), with the fix it rests (every run). Check a play session's
    log for the repeating `HostPhysics: sent N rests` before deleting this entry.

These three remaining reports are open.
The audit removed unsuccessful pose reconstruction; existing native checks and failed traces remain.
Retained changes and their evidence are in [native-bugs.md](design/native-bugs.md).

- **A respawned guest can stay ragdolled, sunk into the floor** (2026-10-29, v1 triage). `death-spectate-guest` once
  timed out on "the respawned player can walk again": the guest's player had its 18 ragdoll bodies awake and stood
  49 units into the floor after its revive. 0 of 14 runs since.
  - Found 2026-10-29.
  - Start: what Revive (`PlayerCharacter::Revive`) leaves of the knock state and ragdoll when the respawn moves a dead
    body; the open revive item in [death.md](design/death.md) (a revived player knocked down stays getting up).
  - Test: `death-spectate-guest --until-fail`, then a GameTests revive flow once the fake keeps knock state.

- **A revived guest's own body kneels in its next conversation** (2026-10-29, cause narrowed 2026-10-30). Once
  thought to follow a follower trade: it follows a death. `follower-anchor-dies` then `follower-anchor-saved`
  reproduces it: right after the revive the guest stands; at the next scenario's first conversation its own body
  kneels (pelvis ~29, head ~55, like the bleedout pose), then stands again later. Once its death ragdoll was left
  awake too (18 bodies). Only the guest sees it (third person); the host sees its copy standing. A later
  conversation with no death first does not show it.
  - Found 2026-10-30: cosmetic, the guest's own view only, after a death.
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
  (`build/check/evidence-audit/native-physics.log`). Consecutive verification is incomplete.
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
