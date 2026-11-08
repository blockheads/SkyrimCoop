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
- [After a guest rejoins](#after-a-guest-rejoins): small
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

**Party scripts**

- [References a guest's script creates](#references-a-guests-script-creates): medium, no duo
- [The werewolf's turn back](#the-werewolfs-turn-back): medium, no duo
- [Leaving a crafting station](#leaving-a-crafting-station): small

**Party travel**

- [The AI/quest lock pair](#the-aiquest-lock-pair): small
- [A follower stops following](#a-follower-stops-following): not reproducible on 1.7.104 (2026-11-07), not worked on
- [A guest's recruitment stalls](#a-guests-recruitment-stalls): small
- [A deleted copy in an AI job](#a-deleted-copy-in-an-ai-job): small

**Saves**

- [The scratch save in the Load list](#the-scratch-save-in-the-load-list): small, no duo

**Plugin compatibility**

- [Quest start and stop events](#quest-start-and-stop-events): medium
- [Clutter flags after a vampire rejoin](#clutter-flags-after-a-vampire-rejoin): small

**Crime and aggro**

- [The older crime scenarios](#the-older-crime-scenarios): medium

**Combat**

- [After the host dies](#after-the-host-dies): medium, caused by the next one
- [A still guest unseen up close](#a-still-guest-unseen-up-close): small, minor
- [An NPC copy's swing picks the nearest](#an-npc-copys-swing-picks-the-nearest): small
- [A guest's summon](#a-guests-summon): medium
- [A mod's rename at run time](#a-mods-rename-at-run-time): small
- [The baseline after a death](#the-baseline-after-a-death): small

**Found by the playthrough flow**

- [A looted corpse keeps items on the guest](#a-looted-corpse-keeps-items-on-the-guest): small
- [The host's copy after a door](#the-hosts-copy-after-a-door): small
- [Players stacked after a door](#players-stacked-after-a-door): small
- [The late save's boot](#the-late-saves-boot): small

**Test tools**

- [An old crash reported as new](#an-old-crash-reported-as-new): small, no duo

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
- **The menu does not open** (1 of 21): `[host] the menu opens on 0001A67D`, 10 s after `Activate`, with no menu and Proventus not
  talking. Not seen again in 16 runs. Unread. These ran from a worktree whose UI did not load, but this menu is the game's own.

### After a guest rejoins

The harness's `rejoin_client` joins again from a running game, keeping that game's world; a player leaves to the main menu and joins
with the host's save (`joins_with_world`). Two differences follow that path, at Lucan's in Riverwood, 2026-11-07. Whether either
reaches a player: unread.

Agent task: read whether a player can reach this path, then make a scenario for what can.

Size: small. Needs: the duo (Riverwood), then GameTests.

- **References differ** (4 of 4): 13 to 15 references, containers by the change flag `0x8000000` on the host only (CommonLib
  `kLeveledInventory`, not read here) and loose objects' poses. It does not depend on time away: 1 s did it.
- **A copy that looks at nobody** (1 of 4): Lucan's owner read `['session 1']`, his copy `[]`, through the check's window. Read, not
  measured: `AiStateService::ToFields` resolves a target's session id once, as the state arrives (`OnCharacterSpawn`,
  `OnNotifyAiState`), and nothing applies it again when the target's copy appears; the owner sends again only when its slots
  change (`AiStateEvents`, `seen`). So the order the spawns arrive in would decide it.
- **Also after a join with the host's save** (2 of 3): one hanging plant in the shop (`FLOR 38B0C`, `38BE6` or `38BE7`), its `b_Lower` bone
  turned differently. `released-copy-takes-up-its-ai` ends in the blank cell, so it does not fail on this.

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
of 2 s or more on some creatures and a few NPCs. Each is below. A refused action's graph writes now reach the copy
(2026-11-08, animation-replay-recovery.md, "A refused action still writes its graph variables"), the writer of the wolf's turn variables.

Agent task: take each shape in turn, read what moves that machine, and say whether a player could see it.

Size: small. Needs: IDA, the duo (`actors-walk-after-arrival`, `dive-host-player`), then GameTests.

- **A wolf `Attack1State` beside a player's death** (`FF000E05`, 13.8 s, `death-spectate-host`; `FF000826`, 15.4 s,
  `death-spectate-guest`; `FF000859`, 7.7 s, and `FF000D74`, 8.8 s, `party-travel-dead-guest`, until the actor stopped being
  read): its owner's `RootBehavior` stays in the attack, its copy in combat idle. May be the owner's graph
  held by the kill: [After the host dies](#after-the-host-dies).
- **Idle picks on creatures, a [graph pick](#a-guest-arriving-mid-loop) by the shape** (measured, not read): a cow's
  `BHR_Idle_TailWag` 2.2 s and 4.2 s, a mudcrab's `idle_MandibleBehavior` 2.2-3.2 s, a dog's standing idle 4.2 s (Riverwood
  `graph_watch`, 150 s, 14 actors, 4018 samples: six runs; rerun on the corrected verb, 15 actors, 3710 samples: five runs, none
  cut short: a hare's idle 3.3 s, chickens 2.2 s, a chair's exit against eating 2.3 s, a dog's locomotion against standing 4.4 s).
- **A wolf's copy kept turning after a refused turn's recovery** (`9B0DB`, 4.7 s and 4.5 s, `wolf_copy_turns_with_owner`,
  2026-11-08): its copy refused the owner's `TurnRight` 15 s after it spawned (`Copy 9B0DB recovered refused action 959FC
  'TurnRight' from its owner's graph`). It loaded the owner's graph state, in `StandingBehavior=LoopingTurnRight`, while
  its owner went on to `StandingIdleState`. The owner's `turnStop` was `refused by the engine, not sent`. So the recovery
  loads a state the owner leaves with no action. Possibly the same writer: that refused `turnStop` set `iSyncTurnState`
  on the owner only, and is now sent. Not shown: rerun the scenario and look for this shape.
- **A wolf's copy kept in `CannedTurnState` with no refused action** (`9B0DD`, 8.4 s, `wolf_copy_turns_with_owner`,
  2026-11-08, with refused actions' writes sent): its owner went on to `StandingIdleState`. The owner refused no action
  from 28 s before the run to 6 s into it, so this canned turn's end read something else. Not read: trace the copy's
  canned turn (its exit's `iSyncIdleLocomotion`, its clip's end).
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

- **A player's copy looks at the other player, its owner at nothing: `kill-moves-cross`, 1 of 12** (10 of 10 passed on the corrected check). The guest as owner
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
  `10C3BA`) posed differently, the guest's player 19 units above the floor, the host player's copy 18 units
  into it (checked: that copy was stepped in 66 of 67 frames, the other its teleport, and stood at the floor when read again).
  `faendal-letter` fails its end comparison the same way, and `copy-holds-spell-after-trip` in Whiterun (2026-11-07: 59
  references, containers with change flag `0x8000000` on the host only, loose food posed differently). (Hod's `pose.head` 117 against 88, once, was a culled
  skeleton read as visible through the stale `was_in_frustum`: the observer now reads `pose_visibility.drawn`,
  culled-npc-pose-readings.md, 2026-11-05.)
  - Test: that scenario, after its steps.
  - Containers in the Bannered Mare differ at the end too (2026-11-06): `changes 0x8000000` on the host's and none on
    the guest's, 39 references (`held-copy-goes-with-owner` run there).
  - The rabbit and Olava with no owner are fixed: [copy-lifecycle.md](re/systems/copy-lifecycle.md).

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

Stands alone: a guest's actor with 3D and no copy flag. Not shown to share the copy-lifecycle cause.

Agent task: Find why a guest shows Sigurd as its own after a checkpoint and a shop.

Size: small. Needs: duo, then GameTests.

- **A guest's Sigurd is its own, unregistered** (2026-10-31). `dialogue-host-barter-earns-speech` failed before
  reopening in the combined batch: the guest's Sigurd `CDD73` role own, no `LocalComponent`, not registered. "Session-1"
  in the report is most likely session id -1, the comparison's "never registered" (inferred from `compare_actors`).
  Seen in the earlier checkpoint-followed-by-shop run too; passed in the fresh run (5 of 5)
  ([report](reports/2026-10-31-v1-three-risks.md#acceptance-and-retained-failures)).
  - Not reproduced 2026-11-06: `death-wipe-checkpoint` then the shop, 4 of 4 passed; `save-load-gate` (stopped 80 s in by a
    wolf's `head_track`: wolf `FF000DD6`, owner `[None x6]`, the guest's copy `['session 3', None x5]`) then the shop passed
    too. That was before the head-track check was fixed: on PR #28's build the gate gets past it and fails later (the alias
    timeout once, Aela's equipment twice). Run that pair again once the gate can finish.
  - Open idea, untested: a reference the engine collected and reloaded comes back as a new form with no copy flag
    ([copy-lifecycle.md](re/systems/copy-lifecycle.md)), and the guest shows it as its own until the host spawns it.
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

**Not reproducible on 1.7.104** (marked 2026-11-07 after three tries, below): not worked on unless it shows again in
play. The entry stays for its engine reading ([followers.md](re/systems/followers.md)). If it returns, the untried
ordering is a respawned copy at the moment the follow package starts again after a fight while the follower still
counts as in combat (the restart skipped, no later package change).

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
  - Not reproduced (2026-11-07, repro MCP, the dog recruited by the guest since a guest's dialogue recruitment stalls, see
    [A guest's recruitment stalls](#a-guests-recruitment-stalls)): a party reload, the guest's death and respawn with
    no enemy near, the follower left in another cell then `move_actor`, a real load door, a wolf fight. Each ended on
    the guest's current copy. What does leave it targetless, read in IDA: the procedure ends on a dying, dead,
    deleted or other-interior target, its load drops a temporary target, and the package pick never restarts an
    unchanged package ([followers.md](re/systems/followers.md)). Staged NPCs are restrained (`stage`'s default
    `frozen`): a restrained follower reads no target until released, which looks like this entry.

### A guest's recruitment stalls

A guest picks "Follow me. I need your help." (topic `B0EE6`, info `B0EE9`) on a placed Lydia: the host logs
`Dialogue: player 2 picked topic B0EE6 (info B0EE9)` and then nothing, the guest's menu stays open and the host's Lydia
reads `ai.talking` (2026-11-07, 2 of 2: outdoors by `talk`, in QASmoke by `pick_with_keys`). She was in the follower
slot later, with her conversation still open on the host. `follower-guest-recruits` fails on the same step. Unread.

Agent task: read why the host's background conversation does not run the picked line (`HostConversation::OnPicked`).

Size: small. Needs: the duo, then GameTests.

### A deleted copy in an AI job

The guest crashed at `SkyrimSE.exe+6F6070` (39372+0x40, a null read at +0x10), from 37541 in an AI job, on its Lydia copy
just deleted (`DeleteActor FF000E0B` 15 ms before, flags `kDeleted`, no parent cell), 2026-11-07, while `scene_reset`
took the party from the Whiterun plains to QASmoke with a guest's conversation open on her. 1 of 1. Crash log:
`crash-2026-11-07-14-58-08.log`, not kept. Unread.

Agent task: read what 39372 and 37541 dereference and why a deleted copy is still in the job's list.

Size: small. Needs: IDA, then GameTests.

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

## Found by the playthrough flow

From `riverwood-day-one` (`docs/design/playthrough-flows.md`), runs of 2026-11-05: each was seen at every run that
reached it. Its summaries are in `build/coop-test/flows/<time>-riverwood-day-one/summary.md`; `--from` a step's
checkpoint replays it in under a minute.

### A looted corpse keeps items on the guest

Stands alone: one container's items.

Agent task: Make a loot taken from an NPC's corpse leave the corpse's copy on the other game as empty as the corpse.

Size: small. Needs: duo, then GameTests.

- **A looted corpse's copy still holds items** (5 of 5 runs, either player looting). After `take` from a bandit's
  corpse the host's corpse is empty, and the guest's copy still holds 2 each of 37C1B, 4F78D and 10E0DD (once 4DEE0
  too). Neither the looter nor its copy got them. A player who opens that corpse on the guest sees loot that isn't
  there. Start: what a take-all from a dead NPC sends (the container change rows), and what the guest applies to a
  dead copy.
  - Test: the flow's `lookout` and `loot` steps (`sees: same_items("enemy_1")`); then a scenario of one bandit killed
    and looted.

### The host's copy after a door

Copies of a player.

Agent task: Find why the host's copy on the guest stands sunk after the host goes through a load door and back.

Size: small. Needs: duo.

- **The host's copy is 26 units in the floor** (every run reaching `reward`). After the host goes into the Riverwood
  Trader and out (two load doors) and walks to Faendal, the guest sees the host's copy sunk 26 units. Start: how a
  copy is placed when its owner changes cell and comes back. (Its empty left hand was fixed 2026-11-07:
  copy-hand-spells.md, "Selected, not held".)
  - The 2026-11-06 run that reached `leave` (out of Embershard Mine) read every body sunk 21 to 25 units: both owners
    and both copies. So "sunk" may be the body check's floor reading at a door's arrival spot: check it against
    vanilla first.
  - Test: the flow's `reward` and `leave` steps (SEEN: `placement.position`).

### Players stacked after a door

Party travel.

Agent task: Make the player the tether brings through a door arrive beside the one who went through, not in them.

Size: small. Needs: duo.

- **A tether arrival lands on the other player** (every door in the flow). The player who goes through a load door
  arrives at the door's marker; the tether brings the other to the same spot, under a unit apart. So the two stand
  inside each other, and the one who didn't lead is in the doorway (the flow's `door` verb lets that player lead
  out). Start: where party travel places the player it brings (`docs/design/party-travel.md`), and the engine's
  free spot search near a marker.
  - Test: the flow's `trader` and `enter` steps; `together` could check a minimum spacing.

### The late save's boot

The duo.

Agent task: Find why the host stops booting the duo from `LateAfterDragonslayerBackinSkyrim`, then make it boot.

Size: small. Needs: duo.

- **The host freezes after loading the save** (2 of 2, 2026-11-07, `copy-state-riverwood` at `53c4dda2`): the boot
  times out at "host: hosting"; the host's log stops mid `HostWorldState: sent ... quests` lines, its status file stops
  with a `MessageBoxMenu` open (empty on screen, Throat of the World), and the harness gets no answer. The same build
  booted the default QASmoke and played Riverwood and Whiterun. It blocks every late-save scenario and
  `riverwood-day-one`. Runs: `build/coop-test/runs/20261107-142718-duo`, `20261107-143500-duo` in that worktree.
  - Test: `coop_test.py duo --save LateAfterDragonslayerBackinSkyrim`.

## Test tools

### An old crash reported as new

Harness only: the game is right.

Agent task: Make the MCP's crash check report only crashes of the running duo.

Size: small. Needs: no duo.

- **After a duo restart, an MCP verb fails on the last duo's crash** (2026-11-06, copy-lifecycle). A crash caused on
  purpose at 10:47:55, read by hand, failed `both_go` a minute later on a freshly booted duo, with "client crashed" and
  that crash's report. Moving the log aside cleared it; measured once.
  - Read: `KNOWN_CRASHES` is filled when the `Session` is made (`session.py`), and in the MCP that is once per server.
    Afterwards `check_for_crash` (`runtime.py`) adds to it when it reports a crash, and it runs only while a verb waits
    (`wait_for`). `duo_stop` and `duo_start` do not touch the list. So a crash no check saw waits for the next verb that waits.
  - Start: fill the list again when `duo_start` boots new games. A crash in the games a verb is using must still fail it.
  - Test: a unit test over a directory of crash logs, if the check can be split from the games.
