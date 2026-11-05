# Open bugs

Every entry is a valid, open bug: none is accepted or shipped with. Each entry: what a player sees, the evidence, where to start, and the
test that shows it. Fixing one: a test in the fastest tier that can show it first (CLAUDE.md), then the fix, then
delete the entry and say where the evidence went. Unfinished work and unexplained behaviour stay in
`docs/progress.md` ("Loose ends"); ideas in `docs/ideas.md`. The fixes and regression evidence for feature 14's
visible actor bugs are in [small-bugs.md](design/small-bugs.md). Native bug fixes and regression evidence are in
[native-bugs.md](design/native-bugs.md). Fixes that landed after a playtest (S2's green flash, the
2026-11-01 dialogue fixes) keep their evidence in `playtests/` and a dated entry in `docs/progress.md`.

Each `###` section is one task for one agent: a paste-ready "Agent task", its size (small: a day or less; medium: a few
days), what it needs, and what it waits for. Delete a section when its last entry goes, and its index line with it.

Where the evidence comes from:

- **The copy-desync batch:** [its report](reports/2026-10-30-copy-desync.md#retained-final-native-failures) has the full
  scenario names, observed values and evidence paths of its 18 failing scenarios. Its three reopened risks (lock-order
  reports, the enemy copy's death delay, Belethor's scene) are fixed:
  [evidence, regressions and native results](reports/2026-10-31-v1-three-risks.md).
- **The 2026-11-04 playtest:** the 18:11 `/bug` and the 18:13 `/export` of that session: the guest's
  `SkyrimCoop-Courier-2026-11-04-1813-e25.zip` (and the host's `reports/2026-11-04-1811-d3b/host-Dragonborn/`), both in
  `build/coop-test/prefix-client|prefix-host/.../My Games/Skyrim Special Edition/SkyrimCoop/`. Times in its entries are
  the logs' `mm:ss` from the host log's start (00:03:46 = 18:03:46 local). Those entries were triaged from logs and code,
  so their engine paths are still to be proved in step 0 (CLAUDE.md).
- **Physics and ragdolls:** the audit removed unsuccessful pose reconstruction; existing native checks and failed traces
  remain. Retained changes and their evidence are in [native-bugs.md](design/native-bugs.md). Party travel's fixes are
  there too. The entries dated 2026-11-01 come from the S1 work (`playtests/2026-10-31-fabien.md`;
  `docs/design/physics-sync.md`, "Steered holds" and "Matches a game without co-op").

## Index

Each line gives a section's size. "waits" means another branch or plan comes first; "no duo" means no Skyrim is needed;
every
other section needs the duo.

**Copy-desync follow-ups**

- [Replay lands in another state](#replay-lands-in-another-state): small
- [Headtracking on copies](#headtracking-on-copies): medium
- [Players' idle variants](#players-idle-variants): small
- [A guest arriving mid-loop](#a-guest-arriving-mid-loop): small
- [Animation mods' random picks](#animation-mods-random-picks): small
- [Creature copies wobble](#creature-copies-wobble): small
- [End comparisons in towns](#end-comparisons-in-towns): medium
- [A dead player's dropped weapon](#a-dead-players-dropped-weapon): small
- [Trade markers](#trade-markers): small
- [Sigurd's registration](#sigurds-registration): small
- [Copy one-offs](#copy-one-offs): small
- [Remaining validation](#remaining-validation): small

**Ragdolls and death poses**

- [Revive leaves the body wrong](#revive-leaves-the-body-wrong): medium
- [Copy ragdolls at rest](#copy-ragdolls-at-rest): medium
- [Get-up](#get-up): medium
- [A wolf's slow rest](#a-wolfs-slow-rest): small

**Physics**

- [A long roll jumps](#a-long-roll-jumps): small
- [A dropped sword](#a-dropped-sword): small
- [A chest lid's pose](#a-chest-lids-pose): small

**Dialogue**

- [Greeting and press timing](#greeting-and-press-timing): medium
- [The picked line overlaps](#the-picked-line-overlaps): small
- [A conversation's deleted NPC](#a-conversations-deleted-npc): small

**Party scripts**

- [References a guest's script creates](#references-a-guests-script-creates): medium, no duo
- [`SendAnimationEvent` on the player](#sendanimationevent-on-the-player): small, no duo
- [The werewolf's turn back](#the-werewolfs-turn-back): medium, no duo
- [Leaving a crafting station](#leaving-a-crafting-station): small

**Party travel**

- [The AI/quest lock pair](#the-aiquest-lock-pair): small
- [A follower stops following](#a-follower-stops-following): medium

**Saves**

- [The scratch save in the Load list](#the-scratch-save-in-the-load-list): small, no duo

**Plugin compatibility**

- [Quest start and stop events](#quest-start-and-stop-events): medium
- [Clutter flags after a vampire rejoin](#clutter-flags-after-a-vampire-rejoin): small

**Crime and aggro**

- [A guest's thefts](#a-guests-thefts): small, waits
- [A guest's aggro](#a-guests-aggro): medium, waits

**Combat**

- [After the host dies](#after-the-host-dies): medium
- [The host's copy has no name](#the-hosts-copy-has-no-name): small, waits
- [The baseline after a death](#the-baseline-after-a-death): small

## Copy-desync follow-ups

### Replay lands in another state

A copy accepts a replayed action and nothing checks where its graph ended up.

Agent task: Add a detector, not a fix: after each taken replay, compare the copy's active state names with the action's
`GraphState` and count and log a mismatch. Run it across a combined batch; build loading on a mismatch only if lasting
mismatches show up.

Size: small. Needs: GameTests (a forced mismatch), then a combined duo batch.

- **An accepted replayed action can land in a different graph state** (user, 2026-10-31: animations drift in dialogue
  after a while). Nothing compares it (animation-replay-recovery.md). Fixed so far: copies played clips before their
  files were in, each game made its own random picks (2026-11-04), and copies never headtracked (progress.md, "copies
  look where their owners do"). The proposed fix is to compare active states with the owner's snapshot after each
  replay and load it on a mismatch.
  - Measured 2026-11-05 (copy-state-sync, animation-replay-recovery.md "Does a copy's graph drift?"): Lucan on a fresh
    duo, active states sampled at 10/s on both games through `npc-counter-matches` and `npc-clips-match`: the copy
    follows every transition within 150-300 ms, no difference lasts over 1.1 s. One NPC: no lasting drift seen, not
    proof there is none. A graph save costs 62-112 us and about 7.5 KB.
  - Compare state names only (variables and clip times differ by design). Paired actions carry no snapshot.
  - Pitfall: `stage` with `ref` on a living NPC left the host's Lucan and Camilla in `JumpFall`/`JumpLand` for the
    session, which reads as drift. Measure on fresh, unstaged NPCs.

### Headtracking on copies

A copy's look target differs from its owner's: the 17 v1-acceptance failures and the kill moves' end comparisons.

Agent task: Make `ai_state.head_track` compare only what the engine reads (step 1), then chase the four failures that
remain (step 2). Read every reader of the slots first: if anything besides `ProcessTracking` reads a dead actor's
slots, step 1 changes.

Size: medium. Needs: IDA, a harness readout of the tracking gate, comparison units, a GameTests flow once the fake has
the gate; duo for step 2.

Findings, 2026-11-05 (IDA and duo; actor-ai-and-copies.md, "When the slots are read"):

- `Actor::UpdateAnimation` (37361) and `Character::ProcessTrackingIfAllowed` (37363) skip `ProcessTracking` (38009)
  for an actor dead or dying, in life state 3, knocked, in sit state 7, not high process, or with its controller out of
  range. Then nothing reads the slots (no head turns) and our hook neither pushes nor reports.
- `ProcessTracking` itself may write an ambient pick into slot 0 every 1.0-1.5 s, after our push
  (`Character::PickAmbientHeadtrackTarget` 38012). It skips a target that is the actor itself.
- `KillImpl` clears all six slots (39477).
- Live actors agree: a hunting wolf, 680 samples on both games, every one the same; a combat slot switch reached the
  copy in 70 ms.

The 17 failures sort by cause (shape of the reported values; one of each measured):

1. **Dead or dying (8: wolves, Valdr): a check of state nothing reads.** Measured on a killed wolf: at dying, slot 1
   becomes each game's own player, about 2 s later it moves to slot 0, the owner's clears at life state 2. The copy
   reached life state 2 first (its clear-all ran before its local write), so its slot 0 keeps its own player. The
   writer of slot 1 at dying is not found (written inline at `+0x144`, so xrefs to 39783 miss it). The kill-move
   failures are this.
2. **A player's slot on itself (4: dives, fus-ro-dah).** Measured: the host player's own shout sets slots 1 and 0 to
   itself; its copy reads none. A self-target turns no head. Why the owner's change is never sent is unread:
   `PlayerCharacter`'s update calls `ProcessTracking` only with camera state 9 or more (0x140745200; inferred).
3. **A temporary form (1: `late-join-gets-calendar`, `FF000820`):** no id the other game shares, as with
   `combat_target`.
4. **Unexplained, real (4): Belethor, Elk (`follower-guest-recruits`), Proventus (`party-buys-house-host`), Chicken
   (`guest-joins-elsewhere`).** Belethor's copy holds the guest's player in slot 1 while alive, where the push should
   have overwritten it. If a copy's own write is the cause, the engine's lever is `Actor::headTrackingUpdateDelay`
   (`+0xEC`; `EndDialogue` 37552 sets it to 1.0 to hold the pick back).


- **Headtracking left over** (2026-10-31, v1-acceptance: 17 scenarios fail `ai_state.head_track`). A copy's own
  ProcessTracking still picks an ambient look target (its game's player) over its owner's slots, and a player's
  default slot on itself reads as none on its copy.
- **Kill moves leave a copy looking at a player** (2026-11-04, still on 2026-11-05 after PR #16). The kill moves play;
  the scenarios then fail on `ai_state.head_track`: the guest's wolf copy `['session 2', None x5]` against its owner's
  `[None x6]`, now and then the reverse (owner `['session 1', ...]`). On PR #16's final build: `host-kill-move` and
  `client-kill-move` 0 of 3 each, failing at the end comparison only (blow and lag checks pass); `spectator-watches-kill-move`
  0 of 3, failing on it before it removes the wolf, so its later checks never run. `kill-moves-cross` hit it on the
  players too (2026-11-04). Most likely the "Headtracking left over" entry above, unproven.
  - Start: the copy's `ProcessTracking` after its kill move: which slot picks the player, and whether the copy's
    graph (`HeadTrackingOn` from the clip, `KillActor` handler) sets it where the owner's does not.
  - Test: `host-kill-move`, `client-kill-move`, `spectator-watches-kill-move`.

### Players' idle variants

Sync points cover NPCs only.

Agent task: Give a player's copy the same graph sync points NPCs have.

Size: small. Needs: duo, then GameTests. Cause read in code (2026-11-05): separate from the replay and headtracking
sections.

- **Players' idle variants can differ between games** (2026-11-04). Sync points (animation-replay-recovery.md, "A
  graph's random picks") are sent for NPCs only: a player's own character on the other game still makes its own random
  picks for gestures and idle variants no action starts.
  - Start: `AnimationSystem::Serialize` skips the player; a player's copy needs the same sync point, and a check that
    the player's first-person graph is not the one read.
  - Test: an `npc-counter-matches`-style scenario on a player's copy.

### A guest arriving mid-loop

Test only: sync points should already cover it.

Agent task: Write the scenario where a guest's copy spawns while its owner is mid-loop.

Size: small. Needs: duo. A test gap, not part of the replay section (2026-11-05).

- **A guest walking in on an NPC mid-loop is untested** (2026-11-04). `npc-counter-copy-falls-back-in` forces the copy
  out of step by loading the host's graph into it: the party tether brings the guest along, so a real late arrival
  cannot be staged. Sync points should cover it, unproven.
  - Test: a scenario where the guest's copy spawns while the owner is mid-loop (a guest joining or crossing a cell
    boundary later).

### Animation mods' random picks

Stands alone: a plugin's own generator.

Agent task: Run `npc-clips-match` on a modded profile and sync Open Animation Replacer's random picks if it differs.

Size: small. Needs: duo with an animation mod.

- **Animation mods' random picks are not synced** (2026-11-04). Open Animation Replacer's Random condition draws from its
  own generator, not the graph's, so each game can choose a different replacement clip.
  - Start: OAR's `RandomCondition` state data (`reference/plugins/OpenAnimationReplacer/src/Conditions.cpp`).
  - Test: a modded run through `npc-clips-match` first, to see what breaks.

### Creature copies wobble

Stands alone: a quadruped's capsule on its copy.

Agent task: Read whether the capsule tilt moves a creature's reported position and stop the wobble.

Size: small. Needs: IDA, duo (`jitter`), then a fast test.

- **Creature copies wobble a little around their goal** (2026-11-05). A stepped wolf's or deer's copy on the guest's game
  turns back on itself up to 326 times in 25 s, its biggest steps 1.6 to 3.3 units (far-off creatures in Riverwood,
  `9F361` and `8CACD`; owners do not). Not furniture: their sit state is 0 and placing copies in furniture did not change
  it.
  - Known from IDA: NPC capsules are rigid bodies whose place is written back after physics (actor-movement.md, "A copy in
    furniture"); these creatures' controllers carry `kQuadruped` and `kCanPitch`/`kCanRoll`, which tilt the capsule to the
    ground (`bhkCharacterController::sub_78292`). Not yet read: whether that tilt moves the reported position.
  - Test: `jitter` on a creature, then a fast test once the cause is read.

### End comparisons in towns

Ambient differences a Riverwood run's end comparison finds, a different one each run.

Agent task: Sort the differences `copy-leaves-furniture-steady` finds after its steps into real desyncs and fix or file
each.

Size: medium. Needs: duo.

- **A town's end comparison finds ambient differences** (2026-11-05). `copy-leaves-furniture-steady` passes its steps in
  Riverwood, then fails the runner's end comparison on a different thing each run: about 55 fish references (ACTI
  `10C3BA`) posed differently, a guest copy of a rabbit with no owner, the guest's player 19 units above the floor, the host player's copy 18 units
  into it (checked: that copy was stepped in 66 of 67 frames, the other its teleport, and stood at the floor when read again).
  `faendal-letter` fails its end comparison the same way. (Hod's `pose.head` 117 against 88, once, was a culled
  skeleton read as visible through the stale `was_in_frustum`: the observer now reads `pose_visibility.drawn`,
  culled-npc-pose-readings.md, 2026-11-05.)
  - Test: that scenario, after its steps.

### A dead player's dropped weapon

Stands alone: one created reference.

Agent task: Make the weapon a dead player drops the same reference on both games, so `kill-moves-cross` passes its end
comparison.

Size: small. Needs: duo, then GameTests.

- **A dead player's dropped weapon differs between games** (2026-11-05). A player killed with a weapon drawn drops it; the
  other game's created reference differs: flags 0x8 vs 0x100008, owner none vs the player, a different 3D (`IronLongSword01`
  vs `Weapon`). Cosmetic so far. `kill-moves-cross` fails its end comparison on it every run, after passing its action phase.

### Trade markers

An animation marker on a merchant during barter.

Agent task: Find why the merchant's copy reads another marker during trade.

Size: small. Needs: duo.

- **An animation marker differs or reads zero during trade.** `dialogue-guest-barter-earns-speech` failed before
  reopening in the combined batch of 2026-10-31: the original Belethor's owner on `turnStop`, action 612862, idle 612865,
  against his copy on `CyclicFreeze`, action 77826, idle 0 ([report](reports/2026-10-31-v1-three-risks.md#acceptance-and-retained-failures)).
  Passed in the fresh run after it (5 of 5). Also an intermittent zero marker during trade, not reproduced in the final
  run; animation-replay-recovery.md keeps the earlier all-zero marker open.
  - Test: `dialogue-guest-barter-earns-speech`, `--until-fail`.

### Sigurd's registration

Stands alone: a guest's actor marked its own.

Agent task: Find why a guest marks Sigurd its own after a checkpoint and a shop.

Size: small. Needs: duo, then GameTests.

- **A guest's Sigurd is marked its own, unregistered** (2026-10-31). `dialogue-host-barter-earns-speech` failed before
  reopening in the combined batch: the guest's Sigurd `CDD73` marked own with session 1, no `LocalComponent`, not
  registered. Seen in the earlier checkpoint-followed-by-shop run too; passed in the fresh run (5 of 5)
  ([report](reports/2026-10-31-v1-three-risks.md#acceptance-and-retained-failures)).
  - Test: a checkpoint, then `dialogue-host-barter-earns-speech`.

### Copy one-offs

Seen once or twice, with no known cause: rerun first.

Agent task: Rerun follower travel and `save-load-gate`, and file a test for whichever still shows.

Size: small. Needs: duo.

- **Lydia faces 26° differently during follower travel; one HorseExit refusal.** The rest of the "copy visuals/state"
  findings of the copy-desync batch. The kill-move refusals were the pair's other half coming back, now skipped
  (`docs/re/systems/kill-moves.md`).
- **The guest's copy of a follower once played another action** (2026-10-29, v1-gate). `save-load-gate`'s desync
  check after the door round's fight: Lydia's `animation.action` on the host 522467 vs the client's copy 297. Once.

### Remaining validation

Test work, not a bug in the game.

Agent task: Get the Greybeards scenario its story save.

Size: small. Needs: duo.

- **Remaining validation.** The Greybeards scenario needs its documented story save. Combined-dev acceptance remains
  separate from these bug investigations. (The owner gait's visible-skeleton control landed 2026-11-05: `GaitWatch`
  counts drawn steps only, actor-movement.md "A culled actor keeps its pose".)

## Ragdolls and death poses

### Revive leaves the body wrong

`PlayerCharacter::Revive` resets neither the knock state nor the graph.

Agent task: Read what Revive leaves behind and reset it, so a revived player neither stays ragdolled nor kneels.

Size: medium. Needs: IDA, duo, then a GameTests revive flow (the fake must keep knock and graph state).

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
- **Player body/ragdoll.** Kneeling, sunken/lifted skeletons, unsettled death ragdolls and mounted zero-height
  readings. Several occur on the owner too. Revived-player kneeling is the same bug.

### Copy ragdolls at rest

A dead copy's bodies against its owner's.

Agent task: Make a dead copy's ragdoll sleep with its owner's and check its pose stays on the owner's.

Size: medium. Needs: duo (`RagdollTrace`), then a fast test.

- **A dead copy's ragdoll never sleeps** (2026-11-05, every `kill-move-kills-guest` run on PR #16). Once its owner's
  ragdoll is at rest (all 18 bodies asleep, about 3-8 s after death), the copy's 18 bodies stay awake, moving about
  0.2-0.5 units a frame, until the trace ends. Not visible, and under `RagdollTrace`'s limits, so the scenario passes.
  Copy and owner lie within 8 units. Cause not found: candidates are the owner's rest arriving as frames the copy keeps
  steering towards (`BodyBridge::Rest` queues a frame for an actor's bodies, never puts them to sleep), or steering
  velocities never reaching zero (`BodyPrediction::Steer`).
  - Start: the copy's `body_trace` motion and awake bits after its owner's `BodyRestsRequest`; whether `CopyRagdollPhysics`
    steers a body already at its frame.
  - Test: a `RagdollTrace` check that a copy sleeps within a second of its owner (`kill-move-kills-guest`).
- **Dead guest's copy can have a displaced pose.** The original report was a 16-unit body offset with three bones
  displaced up to 17 units. The reduced build passes one complete guest death/revive and one kill-move scenario
  (`build/check/evidence-audit/native-physics.log`). Consecutive verification is incomplete.
  - Start: repeat `death-spectate-guest` and compare the owner/copy `RagdollTrace` if alignment fails.

### Get-up

The handoff from lying to standing, knock states 5 and 6.

Agent task: Compare owner and copy through a get-up in `knockdown-front-and-back` and `knockdown-lies-as-host` and fix
the timing, facing and bone snap.

Size: medium. Needs: duo, then GameTests.

- **Brief get-up timing or facing differences.** Basic owner timing, get-up preparation/action and iGetUpType
  synchronization remain. All six regular physics/death/revival scenarios pass once in the reduced build
  (`build/check/evidence-audit/native-physics.log`); this does not establish that the historical intermittent
  timing/facing differences are gone.
  - Start: compare native owner/copy action timing and get-up choice in `knockdown-front-and-back`. Earlier failed
    experiments and traces are retained; implementation-only cache tests did not establish native fidelity.
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
- **The copy turns away at its get-up** (2026-11-02, `knockdown-lies-as-host`): for about 0.3 s, 1 in 2 runs, and failing
  the same with the old copy placement (progress.md's Next until 2026-11-05; `red-flags.md` counted about 5 in 7).

### A wolf's slow rest

Stands alone: the host's own body, seen once.

Agent task: Repeat `client-melee-kill-credit` and read why the wolf's body took over 10 s to rest.

Size: small. Needs: duo.

- **A killed wolf's body once took over 10 s to come to rest on the host** (2026-10-28, adaptive playback run).
  `client-melee-kill-credit` timed out at "[host] the wolf's body comes to rest" once in 20 scenarios and passed on
  the rerun. The host's own body: the copy clock does not play it.
  - Start: repeat `client-melee-kill-credit` and read the host's `BodyService` rest log for the wolf.

## Physics

### A long roll jumps

Stands alone: the guest's playback of the host's frames.

Agent task: Read the trace at the jump in `physics-push-reaches-guest` and fix the late correction.

Size: small. Needs: duo, then GameTests.

- **The guest's playback of a long roll jumps once** (2026-11-01). `physics-push-reaches-guest` (1 in 4: a ladle
  rolling 17 s, a 9.2-unit jump 11.6 s in) and `guest-cast-moves-host` (1 in 4: a jump just over the 6-unit limit)
  fail `FlightTrace.check`; both pass on reruns. Physics-sync's notes had one such failure (0.4 over) on 2026-10-25.
  - Start: the trace at the jump (the samples folder the failure names): a late frame's correction, or the host's
    own 3D (its node left behind) when the frame was read.

### A dropped sword

Stands alone: may already be fixed.

Agent task: Rerun `guest-drops-item-stays` and fix or delete the entry.

Size: small. Needs: duo.

- **The host's copy of a dropped sword rests elsewhere** (2026-11-02). The host's copy of a sword the guest dropped rests
  44 to 67 units from the guest's (`guest-drops-item-stays`, 2 of 5). Filed under exclusive ownership, which it was
  expected to be covered by; not rerun since holds became exclusive.
  - Start: rerun it; delete this entry if it holds. Otherwise read who simulates the drop (`BodyBridge::Free`,
    physics-sync.md, "Exclusive holds").
  - Test: `guest-drops-item-stays`, `--until-fail`.

### A chest lid's pose

Stands alone: one node after an interrupted open.

Agent task: Replay the host's final open state so a chest's lid agrees after an interrupted open.

Size: small. Needs: duo, then GameTests.

- **A chest's lid can rest in another pose on the two games after its open is interrupted** (2026-10-28, live menus).
  The host saving while a guest's chest menu opens, or the chest disabled while open, leaves its `DoorLeft` node turned
  differently on the two games (the world check after `host-saves-with-guest-menu-open`, sometimes
  `guest-menus-close-when-target-goes`). The items and the open state agree; only the lid's pose differs.
  - Start: compare the lid's animation state on both games after the interruption (the chest's `BGSOpenCloseForm`
    open state, `SetOpenState` 14287 in form-state.md) and replay the host's final open state.

## Dialogue

### Greeting and press timing

A guest's greeting ending against the press that follows it.

Agent task: Rerun `dialogue-pick-reaches-host` on the silent device, then fix the queued greeting and the press lost in
the frame it ends.

Size: medium. Needs: duo.

- **Lydia's greeting can remain queued on the guest before any pick** (2026-11-01, Linux/Wine audio off).
  `dialogue-pick-reaches-host` fails at its existing voice-end precondition on a fresh duo as well as after Onmund.
  Capture: callback 3 / sound 162 / seconds -1, subtitle "Hey, slug-breath!"; the host's greeting has ended.
  Cause and regression status unknown; still open. [Evidence and limits](../playtests/2026-11-01-dialogue-fixes.md#remaining-voice-failure-and-limits).
  The duo had no audio driver then (voice lines queued, never started; a silent device since 2026-11-03,
  progress.md): rerun before deleting.
- **A guest's dialogue press in the frame its greeting ends is lost** (2026-10-29, v1 triage). The guest's engine shows
  its list again as its copy's greeting ends; the menu already shows the party's list, so that show is skipped
  (`DialogueBridge` `HookShowList`; running it instead resets the 750 ms pick delay, worse). A press in that same frame
  never reaches `TopicClicked` (the SWF stays `allow_progress` 1, state 1); a player presses again. 1 in ~5 runs while
  `dialogue-pick-reaches-host` pressed as the host's greeting ended; 20 of 20 since it waits for the client's greeting.
  - Found 2026-10-29: a one-frame window.
  - Start: what the engine does at the greeting's end beyond `ShowList` (52857) that the skip leaves out; whether
    vanilla drops a press in that frame too (`repro --vanilla`).
  - Test: `dialogue-pick-reaches-host` without its wait for the client's greeting, `--until-fail 20`.

### The picked line overlaps

Stands alone: small and visual.

Agent task: Stop `DialogueBridge::ShowPicked` duplicating the picked line in the claw conversation.

Size: small. Needs: duo (screenshots).

- **The picked line overlaps in the claw conversation** (2026-10-30, the user, playing the duo). In
  `dialogue-options-are-the-partys`, when an option is picked the dialogue text shows duplicated, overlapping. Not
  captured. Likely `DialogueBridge::ShowPicked` writing the picked line into `TextCopy_mc` where the menu already
  shows one (the claw topic lists two lines).
  - Found 2026-10-30: small and visual.
  - Start: a screenshot of both menus at the pick (`coop_test.py screenshot`) in that scenario.
  - Test: the scenario's pick, both screens.

### A conversation's deleted NPC

Stands alone: one crash, not reproduced.

Agent task: Read what 39372 reads at RCX and guard the path if a copy removed after a trade can reach it.

Size: small. Needs: IDA first.

- **A guest crashed once in a conversation whose NPC was deleted** (2026-10-29, party dialogue). In
  `follower-trade-to-picker` the guest crashed in `Actor::UpdateInDialogue` (37541 -> 39372, null RCX) on Lydia's
  copy, already flagged deleted (client prefix `SKSE/crash-2026-10-29-09-52-36.log`). Not reproduced since; the play
  path (`dialogue-npc-removed-mid-conversation`) passes and the scenario baseline now ends conversations first.
  - Not reproduced (2026-10-29).
  - Start: what 39372 reads at RCX; whether a copy removed after a trade (container menu closed, conversation still on
    the engine) is the path.
  - Test: a scenario that removes the NPC right after a follower trade, if it ever reproduces.

## Party scripts

### References a guest's script creates

Stands alone; needs a decision first.

Agent task: Decide whose a reference a guest's own script creates is, then make the world check and
`vampirism-is-each-players` agree.

Size: medium. Needs: code and GameTests; ask the user the decision.

- **A reference a guest's own script creates is that game's alone** (2026-10-28, party-scripts-2). `PlaceAtMe` is not
  a world change to the router, so a guest's own script (its system quest's `VampireChange`: the change's hidden
  `XMarker`, placed and disabled) makes a reference only its game has; the world check sees it on the guest alone
  (`vampirism-is-each-players` fails on it). Sending it to the host would hand the guest's script None for the
  reference it goes on to use.
  - Start: whether a reference a guest's own script makes is its game's own (the world check and world sync leave it),
    or the host's made for it and sent back.
  - Test: a `vampirism_flows.cpp` case where `VampireChange` places a marker on the guest.

### `SendAnimationEvent` on the player

Stands alone: the code reader, no Skyrim.

Agent task: Teach `CodeReach` to resolve a virtual call on an argument, so a story's `Debug.SendAnimationEvent` reaches
each player.

Size: small. Needs: code and fast tests only.

- **A story's `Debug.SendAnimationEvent` on the player plays on the host's player only** (2026-10-27,
  party-scripts-2). The Body row reads a native's calls and the functors it queues; this native's graph event is a
  virtual call (`IAnimationGraphManagerHolder` slot 1) on its argument's graph holder, which the code reader does not
  follow (it knows the self and this game's player only).
  - Start: `CodeReach`'s registers know the arguments' registers too (r9 on, for a global), and resolve a virtual
    call on an argument that is a reference against the player's graph holder vtable.
  - Test: `code_reach_tests.cpp` with `SendAnimationEvent`'s bytes (0x140A073F0), then a `cutscene_flows.cpp` case.

### The werewolf's turn back

Stands alone: a crash, not reproduced.

Agent task: Build the GameTests race switch with a copy mid-transform and see what the copy's graph holds.

Size: medium. Needs: GameTests (the fake must keep a copy's graph state).

- **The guest's game can crash as the host's werewolf turns back** (2026-10-28, party-scripts-2). In
  `host-beast-form-stays-host` the client crashed in its copy's graph update (hkbBehaviorGraph 58360, after the
  copy refused `IdleWerewolfTransformation`), once in the heap (bhkThreadMemorySource) on dev's DLL, and one run
  ended with the copy still on the transform's idle and the werewolf's values.
  - Not reproduced (2026-10-29): repeated transitions pass and the historical crash is unproven; a crash in play arrives
    as a crash bundle ([bug-reports.md](design/bug-reports.md)).
  - Follow-up: graph bounds/types, descriptor invalidation and owner-value restoration are implemented; concurrent animation-snapshot protection is also implemented. Repeated native transitions pass, but the historical crash remains open. See [stability follow-up](v1-session-items-2026-10-28.md#stability-follow-up).
  - Start: what the copy's graph holds when the owner's race changes back (the respawn from its new base data).
  - Test: a `GameTests` race switch back with the copy mid-transform, once the fake keeps a copy's graph state.

### Leaving a crafting station

Stands alone: one exit path.

Agent task: Make the copy finish its owner's exit from a crafting station instead of staying on sit state 4.

Size: small. Needs: duo, then GameTests.

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

## Party travel

### The AI/quest lock pair

Stands alone: may be gone since 2026-10-31.

Agent task: Rerun the lock audits of `party-loads-overlap` and `party-fast-travels`, then close the entry or classify
the pair in lock-order.md.

Size: small. Needs: duo.

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
  - 2026-11-05 audit: the 2026-10-31 fix moved guest quest writes to between frames, and its "checkpoint" family has the
    same addresses (`+0x3cdb45 > +0x182241`); no run has rechecked this pair since. Rerun `party-loads-overlap` and
    `party-fast-travels` first.

### A follower stops following

Stands alone: not reproduced on 1.7.104.

Agent task: Stage a follower left behind that catches up, then a fight, and watch for a stale cached follow target.

Size: medium. Needs: duo, IDA.

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

## Saves

### The scratch save in the Load list

Stands alone.

Agent task: Take `SkyrimCoopWipe` out of the Load list once its load is done.

Size: small. Needs: code and GameTests only.

- **A party reload's scratch save tops the Load list** (2026-11-04, Linux). `SkyrimCoopWipe` stays in the Saves folder
  after a reload and sorted above a newer quicksave, so "the newest save" loaded an older world: the slot held the
  previous run's Lydia. Seen in the harness, which now deletes the file first; a player sees the same list.
  - Start: whether the scratch save can leave the list once its load is done (`HostServiceReload.cpp`).
  - Test: a `host_load_flows.cpp` case.

## Plugin compatibility

### Quest start and stop events

Needs the intended consumer installed.

Agent task: Install Experience, find the native sources of `TESQuestStartStopEvent` and make a plugin see each stop
once.

Size: medium. Needs: duo with mods.

- **Quest start/stop notification parity is open (mod work).** Vanilla guest journal, stage and completion
  synchronization works, but a plugin consuming only TESQuestStartStopEvent can miss a stop. The audit removed
  the additional relay after it produced host stop 1 / guest stop 2 on a native turn-in
  (`build/check/evidence-audit/native-gameplay.log`). Experience and Skill Uncapper compatibility is not started.
  - Start: investigate actual native notification sources with the intended consumer installed. Do not synthesize
    another stop from flag changes or add a timing-based duplicate filter. Native counters remain available through
    `quest_fragments`; journal/reward tests keep their vanilla stage and completion assertions.

### Clutter flags after a vampire rejoin

Stands alone: seen once, six-mod profile.

Agent task: Run `unsynced` and `save-diff` after `vampirism-kept-across-rejoin` on the six-mod profile and find what
moved the clutter on the guest.

Size: small. Needs: duo with mods.

- **Movement-record flags on guest clutter after a vampire rejoin** (B8, 2026-10-29).
  With all six fix mods, `vampirism-kept-across-rejoin` passes its behavior checks, then the world check finds only
  movement-record change flags differing on three clutter references (`32AFF`, `32AFE`, `32B00`, base `319E5`), set
  only on the guest; positions agree within the check's tolerance. Once, not seen on vanilla (2026-10-29: the scenario
  passes with no world difference and `unsynced` reports every host change sent with a row). A guest's game refuses
  every save, so its records never persist. Evidence: [mod-compatibility.md](mod-compatibility.md),
  [lock-history follow-up](re/crashes/2026-10-29-lock-history-and-button-pose.md).
  - Start: `coop_test.py unsynced` and `save-diff` right after the scenario on the six-mod profile; if the host
    lacks a move the guest has, which engine path moved the clutter on the guest only.

## Crime and aggro

### A guest's thefts

Fixed by PR #17 if it merges: it reads the bounty at 1.7.104's offset.

Agent task: Take this section only if PR #17 closes without it.

Size: small. Needs: duo, then GameTests. Waits for: PR #17 (`theft-bounty-aggro`), which replaces this entry with
`crime.md` and files two smaller ones.

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

### A guest's aggro

Who an NPC attacks when a guest provokes it without damage.

Agent task: Find why witnesses of a guest's crime attack the host and decide with the user who is a fair target.

Size: medium. Needs: duo, then GameTests; ask the user the decision. Waits for: PR #17, which adds what it learned.

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

## Combat

### After the host dies

The NPCs' target.

Size: medium. Needs: duo, IDA, then GameTests.

- **After the host died, NPCs half-stop fighting the guest** (2026-11-04). They drift around looking at the guest, hit
  only up close, and stop when the guest walks away.
  - The host staying dead while the guest fights is the design (`death.md`: the respawn waits for the fight to end), not
    part of this bug.
  - Suspects: (1) the NPCs' target is still the dead host (`HostAggro` drops damage history for a dead victim, not
    the NPC's native target); a dead real player is unreachable for pursuit and attack. (2) The target is the guest's host-side copy, a created `Character` with no AI,
    which pursuit/attack code written for the `PlayerCharacter` cannot track (melee range works through the copy's
    collision and hearing; chase needs detection that is redirected only inside scoped crime). (3) The combat group's
    other-player candidate (the copy) never gets the native pick once the real player is dead.
  - Start: reproduce with the MCP: the host dies in a fight the guest is also in (`scene`, `fight`), then `observe`
    an NPC on the host: controller target, `combat_targets`, detection against the copy, package, whether
    `IsInCombat`. In IDA, read how the combat controller treats a dead target and a
    non-player target (selector 47191, validity 47196, target setter 33235).
  - Test: a scenario `host-dies-guest-still-fought` (the NPC must close in on a guest who steps back); a GameTests flow
    with a fake dead host player that checks the NPC's target is the guest.

### The host's copy has no name

Stands alone.

Agent task: Already running as `names-synced`.

Size: small. Needs: duo, then GameTests. Waits for: the `names-synced` worker (running).

- **The host's player copy on a guest has no name** (2026-11-04, seen through the MCP's `look`): both characters
  are "Prisoner" (`player_record`), the guest's copy on the host shows it, the host's copy on the guest shows "". Seen
  before and after a host load. Start: where a copy's display name comes from the owner's character record.

### The baseline after a death

Harness only: the game is right.

Agent task: Make the runner's cleanup put a dead and respawned player back home.

Size: small. Needs: duo (harness only).

- **A scenario whose player died leaves the baseline check failing** (2026-11-04): `kill-moves-cross-late-death` ended
  with "cleanup: left behind: host: player at ..., baseline ...". The dead and respawned player isn't put back home,
  so the next flow starts off its baseline. Start: the runner's cleanup after a player's death (`reset_baseline`).
  - That scenario is not on dev (2026-11-05): stage a player who dies and respawns.
