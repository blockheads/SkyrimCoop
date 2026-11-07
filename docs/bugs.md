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

How the entries relate (shared causes, wrong checks, what to fix first): [bugs-map.md](bugs-map.md).

## Index

Each line gives a section's size. "waits" means another branch or plan comes first; "no duo" means no Skyrim is needed;
every
other section needs the duo.

**Copy-desync follow-ups**

- [A copy's interaction idle](#a-copys-interaction-idle): small
- [A seated owner's copy keeps an old action](#a-seated-owners-copy-keeps-an-old-action): small
- [A copy's graph after a turn or a pick](#a-copys-graph-after-a-turn-or-a-pick): small
- [Headtracking on copies](#headtracking-on-copies): medium
- [Players' idle variants](#players-idle-variants): small
- [A guest arriving mid-loop](#a-guest-arriving-mid-loop): small
- [Animation mods' random picks](#animation-mods-random-picks): small
- [Creature copies wobble](#creature-copies-wobble): small
- [A copy without its weapon](#a-copy-without-its-weapon): medium
- [End comparisons in towns](#end-comparisons-in-towns): medium
- [Trade markers](#trade-markers): small
- [Sigurd's registration](#sigurds-registration): small
- [Copy one-offs](#copy-one-offs): small
- [Remaining validation](#remaining-validation): small
- [A placed guard's trip home](#a-placed-guards-trip-home): small

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

- [The older crime scenarios](#the-older-crime-scenarios): medium
- [A guest's theft of a created item](#a-guests-theft-of-a-created-item): small

**Combat**

- [After the host dies](#after-the-host-dies): medium, caused by the next one
- [A still guest unseen up close](#a-still-guest-unseen-up-close): small, minor
- [An NPC copy's swing picks the nearest](#an-npc-copys-swing-picks-the-nearest): small
- [A guest's summon](#a-guests-summon): medium
- [A mod's rename at run time](#a-mods-rename-at-run-time): small
- [The baseline after a death](#the-baseline-after-a-death): small

## Copy-desync follow-ups

### A seated owner's copy keeps an old action

`party-buys-house-host`, 2026-11-06, seen twice in other shapes; the flow's crash and lock order are fixed (actor-ai-and-copies.md,
lock-order.md).

Agent task: read how a copy reused from an earlier visit gets its animation state when its owner already sits, then say what the
menu timeout is.

Size: small. Needs: IDA, the duo (`party-buys-house-host`, `--repeat`), then GameTests.

- **A held copy shows an action from an earlier visit** (1 of 7): Proventus `1A67D` placed in his chair on the host,
  `IdleChairEnterInstant`, which it never sent (`animation_sent` empty); his copy's last action was `IdleTalkAndUseItem`, 17 minutes
  old. Both report sitting, the copy with no furniture; live graphs apart 6.3 s. Whether a player sees it: unread.
- **The menu does not open** (1 of 15): `[host] the menu opens on 0001A67D`, 10 s after `Activate`, with no menu and Proventus not
  talking. Not seen again in 10 runs. Unread.

### A copy's interaction idle

A copy plays an idle its owner cancelled.

Agent task: find what cancels the idle on the owner within about 60 ms (unread), and why the copy's graph takes the
action and keeps it. Then either stop the copy the same way or do not send a cancelled action.

Size: small. Needs: IDA, then the duo (Riverwood), then GameTests.

- **Sigrid `13483`, measured** (trace `20261106-093521-mcp-sigrid`, t=41): the owner takes `IdlePickup_Ground` and drops
  it within about 60 ms, with no `PickUp` event; her copy plays the whole clip (`PickUp` at 42.7, `IdleStop` at 44.2). Where
  the owner's clip plays (t=52), the games agree. The live runs were 3.9 to 8 s.
- The fixed live check, 2026-11-06, Riverwood: no pick-up seen in 350 s of `graph_watch` on her (110 s, then 240 s; 3867
  samples, six actors in the first), so it is not every pick-up. It did report her once in another shape, 8.9 s: her owner in
  `MT_Standing_State` with `MT_Idle.HKX`, her copy in `MT_LocomotionType_State`. Same actor, not shown to be the same
  cause. `actors-walk-after-arrival`'s 12 s in Riverwood is too short: watch her by `ids`.
- Loading the owner's snapshot (tried and reverted) put the copy in it, and it left at once.

### A copy's graph after a turn or a pick

The live check (`copies' graphs, owner against copy:`, animation-replay-recovery.md, "Drift detector") still finds runs
of 2 s or more on some creatures and a few NPCs. Each is below; none has been read in IDA yet.

Agent task: take each shape in turn, read what moves that machine, and say whether a player could see it.

Size: small. Needs: IDA, the duo (`actors-walk-after-arrival`, `dive-host-player`), then GameTests.

- **A wolf's copy turning while its owner idles** (`9B0DC`, 2026-11-06, `actors-walk-after-arrival`): its copy in
  `StandingBehavior=LoopingTurnLeft` 4.3 s, then `LoopingTurnRight` 2.2 s, its owner in `StandingIdleState`. The reverse
  of what the old detector claimed, and the one a player could see most: a copy turning on the spot.
- **Canned turns and short turns** (a chicken, a cow): `DefaultBehavior=CannedTurnState` on one side only, 2.0-2.3 s, about
  the 2 s threshold.
- **A wolf `Attack1State` beside a player's death** (`FF000E05`, 13.8 s, `death-spectate-host`; `FF000826`, 15.4 s,
  `death-spectate-guest`; `FF000859`, 7.7 s, and `FF000D74`, 8.8 s, `party-travel-dead-guest`, until the actor stopped being
  read): its owner's `RootBehavior` stays in the attack, its copy in combat idle. May be the owner's graph
  held by the kill: [After the host dies](#after-the-host-dies).
- **Idle picks on creatures, a [graph pick](#a-guest-arriving-mid-loop) by the shape** (measured, not read): a cow's
  `BHR_Idle_TailWag` 2.2 s and 4.2 s, a mudcrab's `idle_MandibleBehavior` 2.2-3.2 s, a dog's standing idle 4.2 s (Riverwood
  `graph_watch`, 150 s, 14 actors, 4018 samples: six runs; rerun on the corrected verb, 15 actors, 3710 samples: five runs, none
  cut short: a hare's idle 3.3 s, chickens 2.2 s, a chair's exit against eating 2.3 s, a dog's locomotion against standing 4.4 s).
- **A counter idle's pick** (`13487`, `npc-counter-copy-falls-back-in`): `CounterBehaviorGraph=AnimObjectCounterRandom`
  on the owner, `AnimObjectCounterArm` on the copy, 9.0-21.2 s, in all four runs.

### Headtracking on copies

A copy's look target differs from its owner's, where the engine reads it. Step 1 is done: the check compares what the
engine reads (actor-ai-and-copies.md, "When the slots are read"); what stayed below are its real cases.

Agent task: find why a copy's slot holds a target its owner's does not, starting with the player's copy (the one that
shows now). If a copy's own write is the cause, the engine's lever is `Actor::headTrackingUpdateDelay` (`+0xEC`;
`EndDialogue` 37552 sets it to 1.0 to hold the pick back).

Size: medium. Needs: IDA, then the duo (`kill-moves-cross`), then GameTests.

Each case by what was measured with the check fixed, 2026-11-06 (30 flows, run twice; `ai.head_track_slots` shows the
raw slots, `ai.head_track_shut` what shuts the gate):

- **A player's copy looks at the other player, its owner at nothing: `kill-moves-cross`, 1 of 8** (6 of 6 passed on the corrected check). The guest as owner
  `[]`, in third person for the kill move, its copy on the host `['session 1']`, the host's own player (raw: slot 0).
  It lasted the check's whole window, though the hook pushes the owner's slots each frame and the ambient pick
  (`PickAmbientHeadtrackTarget` 38012) writes slot 0 only every 1.0-1.5 s, so the pick alone does not explain it: unread.
  The same copy's slot read so through the flows after it in the same boot, with its owner in first person and the check
  saying `unread`.
- **Faendal fighting the guest** (2026-11-06, before the fix): his copy's slot 1 held himself, his owner's nothing. Now the
  same reads as none on both, unless a lower slot of the owner's is what he looks at. Why the copy writes itself: unread.
- **Not seen again, each on its one flow:**
  - Belethor (`guest-menus-close-when-target-goes`): the copy held the guest's player in slot 1 while alive. The flow
    fails first on its [chest lid](#a-chest-lids-pose) both runs; no lasting raw difference on him.
  - Elk (`follower-guest-recruits`): the owner's slots 1 and 5 on the guest, the copy's none. The flow now fails first on
    `vanilla recruitment fills the shared follower alias`, with its watch off too; no lasting raw difference on it.
  - Proventus (`party-buys-house-host`): the owner's dialogue slot on the host's player, the copy's none. Not seen in 15 runs.
  - Chicken (`spec:guest-joins-elsewhere`): the copy's slot 0 on a player. Passed once, failed once on a facing.
- **A temporary form in a slot** (`FF000820`, `late-join-gets-calendar`): no id the other game shares, as with
  `combat_target`. Passes now because that slot was on a first-person player's owner, which nothing reads; an NPC's would
  still show. [bugs-map.md](bugs-map.md), cluster E.

Settled by the check, recorded so they are not filed again:

- **Dead or dying.** `KillImpl` clears all six (39477). Measured on a killed wolf: at dying, slot 1 becomes each game's own
  player, about 2 s later it moves to slot 0, the owner's clears at life state 2; the copy's clear-all ran before its local
  write, so its slot 0 stays on its own player. The gate is shut on both. What writes slot 1 at dying is not found (written
  inline at `+0x144`). `BSDirectAtModifier` (43657) reads the slots from the graph, outside the gate, but `KillImpl` stops a dead
  actor's graph. For a knocked or sleeping actor that reader is unread: if a head turns there, this check will not show it.
- **A player's slot on itself** (dives, fus-ro-dah): a self-target. Not one of `ProcessTracking`'s targets; that it turns
  no head is inferred, not measured.
- Live actors agree: a hunting wolf, 680 samples on both games, every one the same; a combat slot switch reached the copy in
  70 ms.

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

### A copy without its weapon

Some NPCs' and other players' copies show no weapon on a guest's game.

Agent task: Add a readout of the weapon's 3D on a copy, catch it empty-handed, then read what took the weapon. Make the
equipment snapshot compare the hands' weapons as it does their spells.

Size: medium. Needs: IDA, a harness readout, duo, then GameTests.

- **Seen by the user in play** (2026-11-06), not reproduced: party travel and one fight kept hands and worn items agreeing
  (bugs-map.md, "A copy's hands"). A move did strip a copy's armour until the 3 s reconcile.
- The snapshot compares worn items and hand spells, not hand weapons; a copy refuses the engine's equips.
- Lead: copy `13485`'s engine tried to equip about once a second for over 2 minutes in the 2026-11-04 playtest.

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
  - Also in the Bannered Mare (2026-11-06, the MCP's `expect agree all` while recording `dead-player-keeps-weapon`):
    Olava the Feeble (`1A699`) was removed from the host's actors ("Actor discovery: 1A699 removed") seconds after the
    scene loaded, and her copy stayed on the guest with no owner.

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

### A placed guard's trip home

Agent task: Make `leveled-placed-npc-is-hosts` pass its cleanup: find whether the guest's copy should exist at the
guard's home, then fix the game or the check.

Size: small. Needs: duo, then GameTests.

- **The placed guard's copy is missing on the guest after the guard goes home** (2026-11-05): the scenario's own checks
  pass; its cleanup (`send_home`, `scenarios/specs.py`) fails at "[client] its D8C56 goes home with the host's". The
  host's D8C56 is home in cell 39462 (worldspace 60, at 2452, -12693, -5413) with 3D; the guest has no copy (`client {}`).
  Failed the same way in four runs on 2026-11-05, one of them on dev's code and harness, so it predates the names work.
  Evidence: `build/coop-test/results/20261105-184243-343750/at-failure.json`.
  - Open question first: whether that cell is in the guest's loaded area. If not, no copy is right and the check
    (`copy_follows_host`) should accept it; if it is, the copy was removed or never respawned when the guard moved home.
  - Start: rerun the scenario and `observe` D8C56 on both games after `move_actor home`; the guest's log for its
    removal (`Actor removed`) or a spawn for its session id.
  - Test: the scenario itself; a GameTests flow for a host NPC moved home while the guest stays.

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
  Seen again 2026-11-06: `party-travel-dead-guest`, 1 of 7, the guest's own body and its copy 36 units under the floor.
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

### The older crime scenarios

The guard arrests have no passing run on 1.7.104, so nothing shows the arrest working.

Agent task: Find why these stop at their theft, then check the arrest with a guest's crime.

Size: medium. Needs: duo.

- **The guard and control scenarios stop before their point** (2026-11-05). `guest-guard-arrest-fine`, `-resist`, `-jail`
  and `host-guard-arrest-jail` time out on "the witnessed theft charges the party"; `guest-theft-host-witnesses`,
  `guest-assault-party-alarm` on "the host is unseen in the guest theft control"; `guest-pickpocket-party-alarm`
  passes or fails either way. `guest-crime-flags` passes.
  - Same messages on `fdabf1d9`, before the reaction hooks, for the four run there (fine, host jail, host-witnesses,
    pickpocket): not caused by them. `-resist`, `-jail` and `guest-assault` were not run on it.
  - Suspects: the two theft warnings (`crime.md`): `crime theft` passes no warning, but the menu path does; a held guest
    theft now judges 0.5 s later against the same 10 s wait.
  - Matters because a guest's alarm stays on the host's player so that the arrest works (`crime.md`): that is read from
    the code, not run.

### A guest's theft of a created item

A placed item has no session id for the request to carry.

Agent task: Give a created reference a session mapping in the crime request.

Size: small. Needs: GameTests, then duo.

- **A guest's theft of a created item is never judged** (2026-11-04). `CrimeBridge::Target` puts the stolen reference on
  the wire with `ToSession`, which has no id for a created form (`FF...`), so the host finds no target and `Judge`
  returns before its log line. Seen with a placed dagger owned by Hulda: picked up by the guest, no `Party crime` line.
  Plugin references are judged.
  - Test: a `CrimeFlows` case with a created owned item; the fix needs the created reference's session mapping.

## Combat

### After the host dies

The NPCs' target.

Size: medium. Needs: duo, IDA, then GameTests.

- **After the host died, NPCs half-stop fighting the guest** (2026-11-04). They drift around looking at the guest, hit
  only up close, and stop when the guest walks away (`docs/re/systems/combat-targets.md`, "The half-stop").
  - Fixed on the way: an attack gave the guest's copy a combat tree of its own, all through the playtest's wait
    (`HookStartCombat`; scenario `host-dies-guest-still-fought`, flow "After the host dies...").
  - Measured, not a copy's: the NPC's target moves to the copy; the copy's light and detection read as the real player's;
    the stall happens with the real player as target too. In the inn the engine's detection loses a still or retreating
    target (-5 to -10) and the fight goes to search. Inferred, not measured: with the host dead nothing keeps it going.
  - 2026-11-06 (`combat-targets.md`, "Measured 2026-11-06"): vanilla pushes the player back a few units a hit and
    the NPC stands while the gap is inside its approach radius (174); the host's player gets the same. In three clean
    host deaths Orgnar kept fighting the copy and his hits landed. Not reproduced as reported yet.
  - Co-op differences found: an NPC copy's swing hits whoever is nearest in front (copies have no combat target), so
    with another actor closer the guest is not hit; a guest unseen at the host's death is searched for, the NPC walking
    off. Next: the playtest's own setup (the guest moving, fighting back) with `console_watch` and `kill_move_check`.
  - Reproduced once (2026-11-06, the guest still, as the playtester saw): after the host's death Orgnar sat 196 from the
    copy for 20 s on `Advance` without moving or swinging; the host's player at the same gap was closed on at once
    (`combat-targets.md`, "A/B at 196 units"). Not seen again in 5 more rounds.
  - **The cause is the copy unseen at the switch** (2026-11-06, `host_death_watch`, 5 rounds each way, the inn,
    the survivor standing still): the host dies, the target moves to the guest's copy and the fight goes to search every
    time (2.5-7 s, at 44-67 units), twice walking 440-490 units off before coming back. The guest dies instead: the host's
    player is fought on, no search in 5 of 5. So this is [A still guest unseen up close](#a-still-guest-unseen-up-close).

- **Lock order inversion with the host's player dead** (2026-11-05, `host-dies-guest-still-fought`, 2 of 6 runs, with and
  without the fix): `ExtraAliasInstanceArray` 12720 (0x140187955) then the player's reference lock in `Actor::IsGhost`
  (0x1406756F4), against 0x140172A5B then 12719 (0x140187881); our code on the stack, which frame not read.

### A still guest unseen up close

Agent task: Find why an NPC fighting the host reads no line of sight to the guest's copy 74 units away (the cause of the
half-stop after the host dies). Do step 0 in IDA
first (the detection's line-of-sight request for a candidate), then compare with the host's player in the same place.

Size: small. Needs: duo, IDA, then GameTests.

- **An NPC loses a still guest standing next to it** (2026-11-06, `host-dies-guest-still-fought` setup by hand, one run):
  Orgnar fought the host with the guest 80 units off to his side; the host died, his target moved to the guest's copy and
  the fight went to search (state 2) for 13 s at 74 units. `combat_watch`: the copy's candidate read `line_of_sight`
  false, detection -6 to 15, until the copy was found again. A still guest makes no noise, so sight is all there is.
  - Compared (5 rounds each, `host_death_watch`): with the guest's copy left, search every time, twice walking 440-490
    units off; with the host's player left (`dies="guest"`), never. The copy is what isn't seen: this is the cause of
    [After the host dies](#after-the-host-dies). While the host lives, the copy's candidate read line of sight false and
    detection about -6, the host's 42 with line of sight.
  - Not the copy's combat refusal: with `CopyCombat::MayStart` switched off (dev's behaviour, a local build), search
    5 of 5 as well, 5-15 s, once 470 units off. Likely the engine's detection with a second player: a hook there.
  - Test: `host_death_watch` both ways is the scenario's base; a GameTests flow once the engine fact is known.

### An NPC copy's swing picks the nearest

Agent task: Make an NPC copy's swing on the guest's game hit the actor the host's NPC is fighting, not whoever is nearest
in front. Do step 0 in IDA first (`Actor_GetTarget` 38608, `Actor::sub_47272`), then a GameTests case.

Size: small. Needs: IDA, GameTests, then duo.

- **A copy's swing hits the nearest actor in front** (2026-11-06, read in IDA, seen once): a hit on the guest is judged on
  the guest's game by the NPC's copy; with no combat target (copies run no AI) the victim pick (47272) takes the nearest
  actor in the cone within reach. With the host alive and closer, the copy's swing picked the host's copy, not the guest
  (`combat-targets.md`, "Measured 2026-11-06"). Start: give the pick the owner's target (ExtraForcedTarget 0x9D is read
  first) or answer it in a hook.

### A guest's summon

A guest's Conjure Familiar makes no familiar on either game; the host's reaches the guest.

Agent task: Read what happens to the summon on the guest's game, then make a guest's actor reach the host the way a
drop does, one path with script-placed references (bugs-map.md, "Created forms").

Size: medium. Needs: IDA, duo, then GameTests.

- **A guest's familiar never appears** (2026-11-06, Riverwood, 640B6 aimed at the other player). Host's cast: announced
  (`FF00090D`), the guest spawned its copy. Guest's two casts (`abSuccess: true`): nothing announced on the host, and 8 s
  after the first the guest's only familiar was the host's copy.
- Suspect, read not observed: `CharacterService::RunActorDiscovery` deletes a temporary actor on a guest that is no copy
  and has no session id (a rule for saved NPCs). `DeleteActor` logs nothing.
- Test: a scenario where a guest casts it and both games show one familiar.

### A mod's rename at run time

Agent task: Make a script's `SetDisplayName` on an NPC, and `SetName` on a player's base, reach the other games. Do step 0 in IDA first, then add one
GameTests case for the rename.

Size: small. Needs: IDA, GameTests. Waits for: nothing.

- **A mod's rename doesn't reach an NPC's existing copy** (2026-11-05, read in IDA, not played): a script's `SetDisplayName`
  (SKSE) writes the reference's ExtraTextDisplayData directly, past the alias hooks, so only the next spawn or refresh
  carries it (`docs/re/systems/names.md`). Start: a hook on where SKSE's native lands, or relaying that native.
- **A mod's rename of a player's character isn't sent** (2026-11-05, read, not played): `PlayerService` sends a player's
  name when the race menu closes or a save loads; a script's `SetName` on the player's base raises neither, so the party's
  lists and copies keep the old name until one of them. Start: the same native, on the player's base.

### The baseline after a death

Harness only: the game is right.

Agent task: Make the runner's cleanup put a dead and respawned player back home.

Size: small. Needs: duo (harness only).

- **A scenario whose player died leaves the baseline check failing** (2026-11-04): `kill-moves-cross-late-death` ended
  with "cleanup: left behind: host: player at ..., baseline ...". The dead and respawned player isn't put back home,
  so the next flow starts off its baseline. Start: the runner's cleanup after a player's death (`reset_baseline`).
  - That scenario is not on dev (2026-11-05): stage a player who dies and respawns.
