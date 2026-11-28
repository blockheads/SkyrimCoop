# Open bugs

Every entry is a valid, open bug: none is accepted or shipped with. Each entry: what a player sees, the evidence, where
to start, and the test that shows it. Fixing one: a test in the fastest tier that can show it first (CLAUDE.md), then
the fix, then delete the entry. Unfinished work and unexplained behaviour are in `docs/progress.md` ("Loose ends");
ideas in `docs/ideas.md`.

Each `###` section is one task for one agent: a paste-ready "Agent task", its size (small: a day or less; medium: a few
days), what it needs, and what it waits for. Delete a section when its last entry goes, and its index line with it.

Design notes (`docs/design`, `docs/research`, `docs/reports`) were removed on 2026-11-26. A pointer to one, here or in a
code comment, reads from history: `git show b7b43a3bd:docs/design/<name>.md`. Evidence paths under `build/` are from
the runs that found each bug and may be gone.

## Index

Each line gives a section's size. "waits" means another branch or plan comes first; "no duo" means no Skyrim is needed;
every other section needs the duo.

**Copy-desync follow-ups**

- [A seated owner's copy keeps an old action](#a-seated-owners-copy-keeps-an-old-action): small
- [A re-leveled copy leaves its old temporary NPC](#a-re-leveled-copy-leaves-its-old-temporary-npc): small
- [A speaker's copy faces apart after a guest's conversation](#a-speakers-copy-faces-apart-after-a-guests-conversation): small
- [Headtracking on copies](#headtracking-on-copies): medium
- [Animation mods' random picks](#animation-mods-random-picks): small
- [Creature copies wobble](#creature-copies-wobble): small
- [A copy without its weapon](#a-copy-without-its-weapon): medium
- [A refused rider left crouched](#a-refused-rider-left-crouched): small
- [The Greybeards scene never casts the host's player](#the-greybeards-scene-never-casts-the-hosts-player): small

**Ragdolls and death poses**

- [A ragdolled copy holds its pose between updates](#a-ragdolled-copy-holds-its-pose-between-updates): small
- [A script's reference added to a body is not on the guest's copy](#a-scripts-reference-added-to-a-body-is-not-on-the-guests-copy): small
- [Revive leaves the body wrong](#revive-leaves-the-body-wrong): medium

**Physics**

- [Physics against vanilla](#physics-against-vanilla): medium

**Party scripts**

- [The werewolf's turn back](#the-werewolfs-turn-back): medium, no duo

**Party travel**

- [A guest's recruitment stalls](#a-guests-recruitment-stalls): small
- [A deleted copy in an AI job](#a-deleted-copy-in-an-ai-job): small
- [A passenger jumps 57 units as it takes the reins](#a-passenger-jumps-57-units-as-it-takes-the-reins): small

**Plugin compatibility**

- [Quest start and stop events](#quest-start-and-stop-events): medium

**Crime and aggro**

- [The older crime scenarios](#the-older-crime-scenarios): medium

**Combat**

- [A guest's game stalls on its first swing](#a-guests-game-stalls-on-its-first-swing): small
- [A guest's horse in a trap](#a-guests-horse-in-a-trap): small, no duo first
- [After the host loads a save, a guest's moved objects stay](#after-the-host-loads-a-save-a-guests-moved-objects-stay): medium

**Found by the playthrough flow**

- [The host's copy after a door](#the-hosts-copy-after-a-door): small

**From the 2026-11-08 playtest**

- [A guest's crash loading a wipe's save](#a-guests-crash-loading-a-wipes-save): small
- [A guest crash at the main menu](#a-guest-crash-at-the-main-menu): small, no duo

**Found by the full runs of 2026-11-11 and 2026-11-12**

- [A guest's store of an item held in a list](#a-guests-store-of-an-item-held-in-a-list): small
- [A party item removed without its tag](#a-party-item-removed-without-its-tag): small
- [The guest's highlight with a still mouse](#the-guests-highlight-with-a-still-mouse): small
- [A refused join shows nothing](#a-refused-join-shows-nothing): small
- [A guest crashed loading a copy's idle clips](#a-guest-crashed-loading-a-copys-idle-clips): small
- [Cover plates after the host leaves](#cover-plates-after-the-host-leaves): small
- [A follower's party ahead of its copies](#a-followers-party-ahead-of-its-copies): small
- [Failures only deep in a long run](#failures-only-deep-in-a-long-run): medium

**Seen in the final review's duo runs (2026-11-12)**

- [No kill move on a giant, sabre cat or bear](#no-kill-move-on-a-giant-sabre-cat-or-bear): small
- [A victim the host removes while the guest holds it stays on the guest](#a-victim-the-host-removes-while-the-guest-holds-it-stays-on-the-guest): small
- [A guest crash in the garbage collector's job](#a-guest-crash-in-the-garbage-collectors-job): medium
- [A kill move's dead copy awake or lying apart](#a-kill-moves-dead-copy-awake-or-lying-apart): small
- [A claimed kill's victim holds its pose for the host's replay](#a-claimed-kills-victim-holds-its-pose-for-the-hosts-replay): small
- [Sync points asked for over and over](#sync-points-asked-for-over-and-over): medium

**Found by the full run of 2026-11-16**

- [A guest that never arrives after a fast travel](#a-guest-that-never-arrives-after-a-fast-travel): small

**Found by the full run of 2026-11-22**

- [A host 980 units from its saved place after a load](#a-host-980-units-from-its-saved-place-after-a-load): small
- [A copy at Sheathed while its owner draws](#a-copy-at-sheathed-while-its-owner-draws): small
- [The party's sword unnamed at a merchant](#the-partys-sword-unnamed-at-a-merchant): small
- [A dragged corpse's copy apart from its owner](#a-dragged-corpses-copy-apart-from-its-owner): small
- [A fighter deleted mid-fight holds the party's respawn](#a-fighter-deleted-mid-fight-holds-the-partys-respawn): small
- [Seen once in the run of 2026-11-22](#seen-once-in-the-run-of-2026-11-22): small

**Found by the full run of 2026-11-28**

- [A guest crash in `unequipall` after a reload](#a-guest-crash-in-unequipall-after-a-reload): small
- [A host crash in the attack cone hook](#a-host-crash-in-the-attack-cone-hook): small
- [A player's combat flag on, its copy's off](#a-players-combat-flag-on-its-copys-off): small
- [A merchant's barter line never offered to a guest](#a-merchants-barter-line-never-offered-to-a-guest): small
- [The guest's chat misses a refused start](#the-guests-chat-misses-a-refused-start): small
- [The guest's Load list never opens](#the-guests-load-list-never-opens): small
- [A new guest's race menu](#a-new-guests-race-menu): small
- [A guest's rejoin from the host's menu load](#a-guests-rejoin-from-the-hosts-menu-load): small
- [Seen once in the run of 2026-11-28](#seen-once-in-the-run-of-2026-11-28): small

**Test tools**

- [The gate fails on GameTests now and then](#the-gate-fails-on-gametests-now-and-then): medium to large, no duo
- [The end comparison's ambient differences](#the-end-comparisons-ambient-differences): medium
- [Scenario checks that misjudge](#scenario-checks-that-misjudge): small each
- [A scenario stages its speaker frozen](#a-scenario-stages-its-speaker-frozen): small
- [A staging spot 1500 units off with no floor](#a-staging-spot-1500-units-off-with-no-floor): small

## Copy-desync follow-ups

### A re-leveled copy leaves its old temporary NPC

PR #114 review, 2026-11-23 ([leveled-copy-base.md](re/systems/leveled-copy-base.md)): `RelevelActor` builds a new
temporary NPC (14375) and sets it; the displaced temporary is not collected, so each correction after a guest re-roll
leaves one NPC form behind. It is left on purpose: a queued 3D load may still read it, unread when its last reader ends.

Agent task: read when the background loader (QueuedCharacter 13108 -> 37177) lets go of an actor's base, then hand the
displaced temporary to the collector (36460) after that; GameTests `[copy-level]` checks it is retired.

Size: small. Needs: IDA, then GameTests.


### A seated owner's copy keeps an old action

**Unclear (triage 2026-11-28):** seen once (2026-11-06, 1 of 7), before the graph input stream; not seen on Proventus
since (2026-11-22: 2 of 2 failed on other shapes). `party-buys-house-host --repeat 6` on dev settles it.

Agent task: read how a copy reused from an earlier visit gets its animation state when its owner already sits.

Size: small. Needs: IDA, the duo (`party-buys-house-host`, `--repeat`), then GameTests.

- **A held copy shows an action from an earlier visit** (1 of 7): Proventus `1A67D` placed in his chair on the host,
  `IdleChairEnterInstant`, which it never sent (`animation_sent` empty); his copy's last action was `IdleTalkAndUseItem`, 17 minutes
  old. Both report sitting, the copy with no furniture; live graphs apart 6.3 s. Whether a player sees it: unread.

### A speaker's copy faces apart after a guest's conversation

After the guest's conversation with Lucan ended (his goodbye, the host's E waited out), the scenario's closing
comparison found his copy facing 34 degrees off his owner: host `[0, 0, 3.139]`, guest copy `[0, 0, 3.734]`
(`dialogue-host-talks-during-guest-turn-in`, 1 of 2 runs, 2026-11-27, `agent/dialogue-interrupted-reset`). Every step before
it passed. Not read: whether the owner's turn back after the conversation is sent, or the copy keeps the turn to the
guest's character (`HookShouldRotateToTrack`, dialogue.md "Who the speaker looks at").

Agent task: rerun `dialogue-host-talks-during-guest-turn-in`, read Lucan's rotation on both games through the
conversation's end (`observe`), and find which side stops following.

Size: small. Needs: the duo.

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
  - Full run of 2026-11-11: the same reading (guest `[]`, its copy on the host `['session 1']`) failed the end comparison
    of `friendly-fire-kill`, `kill-move-kills-guest`, `kill-move-runs-full-length` and `kill-moves-cross`, one after the
    other about 50 minutes into the boot. Each passed alone on both builds, so something earlier in a boot leaves it.
  - The reverse, on dev's plugin (`party-travel-mounted-guest`, 1 of 10, 2026-11-11): while riding, the guest as owner
    `['session 1']`, its copy on the host `[]`.
- **Faendal fighting the guest** (2026-11-06, before the fix): his copy's slot 1 held himself, his owner's nothing. Now the
  same reads as none on both, unless a lower slot of the owner's is what he looks at. Why the copy writes itself: unread.
- **Not seen again, each on its one flow:**
  - Belethor (`guest-menus-close-when-target-goes`): the copy held the guest's player in slot 1 while alive. The flow
    failed first on its chest lid both runs (fixed 2026-11-18, progress.md); no lasting raw difference on him.
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
- 2026-11-22, dev `aa723513`: `kill-moves-cross` and the kill-move four passed. Two other NPCs now: Hulda
  (`dead-player-keeps-weapon`, Whiterun: owner `['session 1']`, copy `[]` at the end) and Lucan
  (`guest-rejoins-while-joined`: owner `['session 5', 'session 5']`, copy `['session 5']`, 7.1 s into a 10 s hold).

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

Some NPCs' and other players' copies show no weapon on a guest's game (the user, in play, 2026-11-06), not reproduced
since.

Agent task: Add a readout of the weapon's 3D on a copy, catch it empty-handed, then read what took the weapon.

Size: medium. Needs: IDA, a harness readout, duo, then GameTests.

- Fixed meanwhile, so not these: the engine's outfit reset emptying a copy's hands (GameTests "A copy whose hands the
  engine's outfit reset emptied holds its owner's weapon again") and a bow left in one hand (PR #108, GameTests "A
  copy left holding its bow in one hand holds it in both again"; `re/systems/hand-slots.md`).
- Lead: copy `13485`'s engine tried to equip about once a second for over 2 minutes in the 2026-11-04 playtest.
- The Winterhold guards' torches (2026-11-22 playtest) did not reproduce (2026-11-23, 30 s at night); a `/bug` on one
  would show its hands and worn marks.
- Still open: the `[0, bow]` writer is unread (39811 sets one hand per call: outfit reset, MoveToHigh, disarm;
  `re/systems/actor-state.md`), and a weapon's 3D missing in play is not observed: the 3D readout is the next step.

### A refused rider left crouched

`two-players-race-for-horse`, when the host wins the race: the guest's ride is refused and its player taken off
(`OnNotifyMountRefused`, `PlayerStopRiding` -> `StopRidingQuick`), but sometimes it is left with its pelvis at its feet
and sit state 0 on its own game while the host's copy of it stays at sit state 2 (getting on). A player would see the
loser of a race for a horse stuck crouched, or getting on, on the other game.

Agent task: read what `StopRidingQuick` does to a player still in its get-on clip (the mount interaction, 37905's
package, the graph's state), and whether the refusal's arrival time within that clip decides it.

Size: small. Needs: IDA, the duo.

- **Measured** (2026-11-20, `agent/piece-4b`, action claims): 3 of 7 runs (`fe9a7048` and the next build); 0 of 3 later
  runs where the host also won and "Ride on 1C refused ... our player gets off" logged; 0 of 2 runs before the claim
  refactor (those failed on Riverwood's drift). Not read yet whether the refactor changed the refusal's timing.
- 2026-11-22, dev `aa723513`: 1 of 4 runs passed; the three failures were other shapes (a rabbit's and a fox's copy off
  their owners, the mill's log: fixed by piece F, 2026-11-22), none crouched. Not enough
  runs to say the crouch is gone.
- **Unclear (triage 2026-11-28):** 0 crouched in the 9 runs since 2026-11-20. `two-players-race-for-horse --repeat 8`
  with the host winning settles it; none crouched there, delete this.

### The Greybeards scene never casts the host's player

`greybeards-scene-casts-party` runs from its story save now (`duo --save LateAfterDragonRising`, from
`scripts/fetch_late_saves.py`; the save booted in 37 s on 2026-11-16) and fails at its first step: after `setstage
MQ105 270` on the host, "[host] a scene casts its player" timed out after 60 s, 1 of 1. The players stood at
(-389, 2731, 105) and (-282, 2731, 105), first person, no menu. Not read: whether the stage ran its fragment, and whether
the scene's actors (the Greybeards) are loaded where the party stands.

Agent task: read MQ105's stage 270 fragment and its scene's start conditions, then rerun with the party where the
scene plays.

Size: small. Needs: the duo with the late save.
- 2026-11-22, dev `aa723513` (`duo --save LateAfterDragonRising`): 1 of 1 again, the same step.
- **Unclear (triage 2026-11-28):** whether a player at High Hrothgar would see it or the scenario stages the party
  where the scene's actors are not loaded is not told apart; reading the stage 270 fragment settles which.

## Ragdolls and death poses

### A script's reference added to a body is not on the guest's copy

Papyrus `AddItem` of a reference into dead Arvel (2026-11-23, 1 of 1): the host's body held it, and the guest's Take
All found nothing. Not traced: whether the guest's copy lacked the item, or the take missed it. A chest works
(`alias-key-taken-guest`).

Agent task: read the guest's copy's items after the call, then what raises an NPC's items part for it.

Size: small. Needs: duo, then GameTests.

### A ragdolled copy holds its pose between updates

Read, not measured (2026-11-23, `re/systems/actor-movement.md`, "A copy's pose between its owner's updates"): in a frame
where none of its owner's updates are due, the real 63587 does not run on a copy, so it is not registered for the
ragdoll passes (63761), and with its ragdoll in the world 63589 writes no skeleton either. A copy knocked down or
getting up holds its pose for that frame; the blend (`PoseTimeline`) reaches it only in the frames with an update.

Agent task: measure it (held frames of a copy getting up), then register the graph in frames without an update as 63587
does when a far graph's countdown has not run out, without stepping it; teach the fake the ragdoll passes first.

Size: small. Needs: duo (knockdown scenarios), IDA (63587's countdown path).

### Revive leaves the body wrong

A revived player's body, now and then, stays in its ragdoll.

Agent task: find what puts a revived player's ragdoll back in the world, sunk into the floor, from a failing run's
ragdoll trace.

Size: medium. Needs: duo (`revived-guest-gets-up --until-fail`, `party-travel-dead-guest`), then a GameTests revive
flow.

- **A respawned guest can stay ragdolled, sunk into the floor** (2026-10-29, v1 triage). `death-spectate-guest` once
  timed out on "the respawned player can walk again": the guest's player had its 18 ragdoll bodies awake and stood
  49 units into the floor after its revive. Seen again 2026-11-06: `party-travel-dead-guest`, 1 of 7, the guest's
  own body and its copy 36 units under the floor. Not seen in 2026-11-18's runs: `party-travel-dead-guest` 2 of 2,
  `revived-guest-gets-up` (it checks the revived ragdoll is out of the world) and `kill-move-kills-guest`'s respawn
  check passed every run.
  - Read in IDA 2026-11-18 (`docs/re/systems/physics.md`, "Resurrect and the knock state"): `Actor::Resurrect`
    (37321) zeroes the knock state; it plays `kActionResetAnimationGraph` while the actor is still dead;
    `Actor::DoReset3D` (40255) rebuilds the worn models, not the graph. `PlayerCharacter::Revive` takes the ragdoll
    out once (`RemoveRagdollFromWorld`); what puts it back is not known.
  - Start: a failing run's `RagdollTrace` of the guest after its revive: when its bodies go back in the world, and
    which graph event (`AddRagdollToWorld`, 42873) or death action came just before.
- 2026-11-22, dev `aa723513`: `revived-guest-gets-up`, `party-travel-dead-guest`, `death-spectate-guest` and
  `kill-move-kills-guest` passed 1 of 1 each.
- **Unclear (triage 2026-11-28):** not seen since 2026-11-06 (every revive scenario passed on 2026-11-18 and
  2026-11-22). `revived-guest-gets-up --until-fail` (20 runs) settles it; clean there, delete this.

## Physics

### Physics against vanilla

What the vanilla baseline still fails on (2026-11-15, `docs/design/physics-sync.md`, "One simulator per pile"). Still
valid 2026-11-18: rerun on `agent/bugs-physics` (dev `2423b5de` plus a chest lid fix that touches no physics), 4 of the 8
pass; every failure is a shape below.

Agent task: rerun the eight `*-like-vanilla` scenarios (`collision_suite`), then trace whichever item below still shows.

Size: medium. Needs: the duo, then GameTests.

- **A body the guest's physics will not keep at the host's rest** (a ladle leaning, a pot in a pile): after two asks
  that both answer the rest it already had (`GuestHands::kAsksAtMost`), the guest leaves it where it lies, so the two
  games can disagree by a few units on that one object (3.1 units in one round). Before the cap it was put back and
  fell again every 1.2 s for good. Not traced: why that body does not stay where the host's lies.
- **A body that creeps off its rest on the guest is put back in one frame** (a wine bottle drifted 11 units at 4
  units/s, was asked about, and the host's answer placed it back at once). A glide back on a second delivery of the
  same rest was tried and reverted: the host resends rests every couple of seconds, so it restarted the glide each
  time and the objects never slept (a wheel shook 233 times).
- **A guest standing in objects the host's game then places** (staging: items spawned at the players' feet, then set
  on the lane by the console). The host takes each back (`HostHands::OnHostPlaced`), yet seconds later the guest's
  log had its player pushing one from 184 units away. Not traced. The trials place the players before making
  objects, so they no longer stand among them.
- **Shaking in a pile on the guest while the host carries into it, in some rounds** (turns over vanilla's highest).
- **A snap of 6 to 13 units now and then in a pile, on the guest** (2026-11-16): the host's answer to an ask is held
  at once. Tried and reverted: gliding back before the hold (16 to 19 s of motion), putting the whole island to sleep
  when all of it is still (the guest's own carry jumped 75 units), gliding onto the answer (the host's carry left
  rests 8 and 16 units apart), a third ask (no change), and the guest's pose taken as the rest after two asks (the
  games then chattered: the host re-sent a pile's rests every 0.3 s and the carry never settled).
- **Pile rests 2 to 15 units apart on the two games now and then** (tavern, carry): asked twice, the guest leaves a
  body where its physics put it (`GuestHands::kAsksAtMost`). One case (2026-11-16): a body the guest's player pushed
  up onto another rested there, its rest went out, then it slid down in the guest's game and was taken for the host's.
  Claiming its own rest moving again within 3 s as its own ("settles") made the suite worse (4 of 8: piles never
  settled, staging timed out); reverted.
- **2026-11-18, reruns of the 8:** `guest-push`, `host-push`, `host-push-mixed`, `host-tavern` pass;
  `guest-push-mixed` once passed, once timed out staging (a cheese wheel not resting on the lane); `guest-tavern` a rest
  2.2 units apart (round 2, object 64B33); `guest-carries-into-pile` timed out staging both runs (a cabbage still
  moving, a bucket resting off the lane); `host-carries-into-pile` the carried object moving 18.3 s on the guest in the
  middle round (vanilla 1.9 to 10.5), a shape not listed before: after the carry, the guest's copy of it keeps
  moving. Run in batches: the duo's 15-minute cap takes a longer run's games when someone waits.
- **2026-11-16, dev's full run:** 5 of the 8 passed; `guest-carries-into-pile-like-vanilla` (a rest 3.4 units apart),
  `host-carries-into-pile-like-vanilla` (staging timed out) and `host-push-mixed-like-vanilla` (a pot turning 7 and 11
  times where vanilla turns up to 6), each a shape below.
- **Staging times out now and then** (`stage_objects`, "[host] ... rests on the lane"): a staged object rests, then
  moves again with nobody near, past the 20 s wait. Seen before and after the pile changes; not traced.
- **`guest_drops_wheel_on_wheels_like_vanilla` not rerun** since its cause was fixed (2026-11-23, a guest's touch overrode
  a host's placement 2300 units away; GameTests `[placed]`): passing in the fast tests only.
- **Physics scenarios of the authority change, not passing or not rerun** (`agent/physics-audit`): on an earlier build
  `guest-grabs-orgnar-bottle`, `guest-push-like-vanilla` and
  `host-push-mixed-like-vanilla` passed once each. On an earlier build failed and not rerun: `guest-drags-host-corpse`
  (the grab broke at its first frame, so the corpse echo's fix is unproven in Skyrim; it has no fast test),
  `guest_drops_on_bottles_like_vanilla` (108.5 units, vanilla from 109.2), `host-tavern-like-vanilla` (9 turns, vanilla up
  to 8). None compared with dev.
- **A mask set on Orgnar's back snapped on the guest** (the 2026-11-23 playtest), not reproduced: `RestPlacer::kKeep`
  (`physics-sync.md`) is shown by fast tests only. Start: a real placement by a player's grab, both games traced.
- **Orgnar's copy leaning on his bar is posed 2 to 4 units higher, hands and feet 5 to 13 off** (measured 2026-11-23,
  before dev's pose blending, `physics.md`). Not read why.
- **2026-11-22, a guest's drop onto a pile** (`agent/physics-piles-drop`, final build; `physics-sync.md`, "A drop onto
  a pile"). Guest walker, 3 rounds a run: `drop_wheel_on_wheels` 3 runs of 3 as vanilla, `drop_on_bottles` 1 of 1,
  `pile_carry_release`, `tavern`, `push_mixed` 1 of 1, `carry_into_pile` 1 of 2 (the other a staging timeout, below).
  Still open: **`drop_on_pile` (a bucket onto nine tavern items) settles at the top of vanilla's range on the guest**:
  the middle round's seconds 5.8, 5.0 and 6.4 against vanilla's 2.5 to 5.8 (the let go bucket's 2.7 to 5.0) in 3 runs
  of 4, so `guest-drops-on-pile-like-vanilla` fails about as often as it passes; rests agree, no jump, no shake. Not
  traced: which bodies move last (the host's rests of the pile coming back after the guest's went out is the guess).
  After the review's changes (one run each): `carry_into_pile`, `drop_wheel_on_wheels` and `drop_on_bottles` as
  vanilla, `drop_on_pile` a staging timeout.
  Seen once and not traced: the host's copy of the wheel pile moving 13 s where the guest's settled in 6. The host's
  own drops onto a pile were not run.
- **2026-11-22, dev `aa723513`:** the full run passed `guest-push`, `host-push`, `guest-push-mixed`, `guest-tavern`;
  `host-tavern` (a jump 5.2, vanilla's up to 4.8) and `host-push-mixed` (staging: "no physics world, or this frame's
  picks are spent") passed in the rerun; `guest-carries-into-pile` 2 of 2 (jumps 7.6, 6.3), `host-carries-into-pile`
  2 of 2 (staging, then a rest 3.2 units apart and a jump 4.9). `guest_pile_release_falls` 2 of 2 (rests 2.2-5.7 units
  apart, then a let-go object put to sleep while it moved).
- **2026-11-28, dev `087613325`:** `guest_drops_on_pile_like_vanilla` 2 of 2 (the host's peak 332 and 308 against vanilla's 192 to
  302), `guest_drops_on_bottles_like_vanilla` 2 of 2 (the host's objects turn 6 and 7 times, vanilla's up to 5),
  `guest_pile_release_falls` 1 of 1 (16 turns, vanilla's up to 11.2), `guest-carries-into-pile-like-vanilla` 1 of 2
  (14.9 s, vanilla's 1.9 to 10.5), `guest-tavern-like-vanilla` 1 of 1 (a wine bottle never rests on the lane in 20 s).
  The pushes, `host-tavern` and `host-carries-into-pile` passed.

## Party scripts

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
  - Still not reproduced (2026-11-18, `agent/bugs-dialogue`): `host-beast-form-stays-host` passed 2 of 2 on dev.
  - **The stuck copy shows now** (2026-11-22, dev `aa723513`, 2 of 2, no crash): the end comparison finds the host's
    werewolf owner on `IdleWerewolfTransformation` (action 77826) and its copy on the guest on `WeapUnEquip` (action
    289711). So the copy never took the transform its owner did (the copy "refused `IdleWerewolfTransformation`" of
    2026-10-28). Start from that run's `at-failure.json` and the guest's log at the transform.
  - Start: what the copy's graph holds when the owner's race changes back (the respawn from its new base data).
  - Test: a `GameTests` race switch back with the copy mid-transform, once the fake keeps a copy's graph state.
- 2026-11-28, dev `087613325`: `host-beast-form-stays-host` 2 of 2, the same stuck copy (owner on action 77826, copy on 289711);
  `client-beast-form-stays-client` the same in reverse once, then passed.

## Party travel

### A guest's recruitment stalls

A guest picks "Follow me. I need your help." (topic `B0EE6`, info `B0EE9`) on a placed Lydia: the host logs
`Dialogue: player 2 picked topic B0EE6 (info B0EE9)` and then nothing, the guest's menu stays open and the host's Lydia
reads `ai.talking` (2026-11-07, 2 of 2: outdoors by `talk`, in QASmoke by `pick_with_keys`). She was in the follower
slot later, with her conversation still open on the host. `follower-guest-recruits` fails on the same step. Unread.

Agent task: read why the host's background conversation does not run the picked line (`HostConversation::OnPicked`).

Size: small. Needs: the duo, then GameTests.
- 2026-11-22, dev `aa723513`: `follower-guest-recruits` passed in the full run, then 1 of 3 reruns; the two failures
  stopped at "vanilla recruitment fills the shared follower alias" (10 s).

### A deleted copy in an AI job

The guest crashed at `SkyrimSE.exe+6F6070` (39372+0x40, a null read at +0x10), from 37541 in an AI job, on its Lydia copy
just deleted (`DeleteActor FF000E0B` 15 ms before, flags `kDeleted`, no parent cell), 2026-11-07, while `scene_reset`
took the party from the Whiterun plains to QASmoke with a guest's conversation open on her. 1 of 1. Crash log:
`crash-2026-11-07-14-58-08.log`, not kept. Unread. 37541 is `UpdateInDialogue`: its fault on a deleted
host NPC (a placed speaker) was fixed 2026-11-06 (16d189d27, `placed-speaker-deleted-mid-conversation`); this one is a
copy deleted under a guest's conversation.

Agent task: read what 39372 and 37541 dereference and why a deleted copy is still in the job's list.

Size: small. Needs: IDA, then GameTests.

### A passenger jumps 57 units as it takes the reins

Seen on the duo 2026-11-18; `party-shares-a-horse` (Whiterun plains, `cow Tamriel 0 -4`) is its scenario.

Agent task: read what hands the horse to a seated passenger and why its body jumps (start below).

Size: small. Needs: IDA, the duo (`party-shares-a-horse`), then GameTests.

- `party-shares-a-horse`, "the host gets off": the guest's own player, seated as a passenger, moved 57 units in one
  frame (19 ms) 0.84 s into the host's get-off, as the horse was handed to it. 1 of 3 runs; the other two smooth.
- Not the rider-off message: it reached the guest after the host's copy was already off (`Unboard ...: its copy on 0,
  sit state 4`) in every logged run.
- Start: trace the guest's player and horse around `PlayerTakeReins` (`EndMountPair`, `HoldRider`, 40725,
  `EvaluatePackage` 37401) with `riders_smooth`'s body trace; read what 40725 / the mount package do to a seated rider.
- 2026-11-22, dev `aa723513`: 1 of 3 runs, "the host gets off: [client 14] jumped 50 units in 16 ms at 0.77 s"; the
  other two passed whole (their end comparisons too).

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

## Crime and aggro

### The older crime scenarios

Since 2026-11-22 two arrests pass (`guest-guard-arrest-jail` and `guest-guard-arrest-resist`, 4 of 4 each on dev
`aa723513`); the fine, the theft controls and the Ivarstead arrests still stop before their point. A guest's alarm stays
on the host's player so that the arrest works (`crime.md`): read from the code, not run.

Agent task: Rerun the failures below on dev first (the theft judging changed after the run), then find why each stops at
its theft, and check the arrest with a guest's crime.

Size: medium. Needs: duo.

The full run of 2026-11-22 on dev `aa723513`, then a shuffled rerun of each failure:

- "the host is unseen in the guest theft control": `guest-lockpick-party-alarm`, `guest-pickpocket-party-alarm`,
  `guest-murder-party-alarm`, `guest-container-theft-and-fine` 2 of 2; `guest-theft-host-witnesses` and
  `guest-assault-party-alarm` 1 of 2. **Not rerun since** ee07e6d6c (2026-11-22, after this run): a hidden guest's theft
  is judged by who sees the guest, not the host, which is this control's property. Rerun them before reading further.
- `guest-guard-arrest-fine` 2 of 2 ("the sentence or fine moves the host", then "the witnessed theft charges the
  party"); `host-guard-arrest-jail` 2 of 4, each on a lock order report tagged ours on the guest
  (`+0x10587a6 > +0x16a695` against `+0x17014d > +0x1058a00`), from before the hook sizes were measured (0c324db9f):
  rerun before reading it.
- `guest-crime-flags`, which passed on 2026-11-05, now 2 of 2: "payment ends the guest's native alarm/combat" timed
  out after 10 s.
- `guest-ivarstead-jail-mouse` and `-fine-mouse-sweep` (after the worn-list and clip-deadlock fixes, 6 runs, no crash):
  "the guard naturally arrests the party" timed out once; the end comparison's clutter twice (a ladle, the sharpening
  wheel).
- Scenes: `guest-theft-warned` took nothing back ("guest_player carries 0 of 64B36 and orgnar holds 0"; on 2026-11-19
  "the party has spent 0 theft warnings, expected 1", 3 runs; it passed when written, 2026-11-13),
  `guest-theft-at-bar-bounty` passed its steps and failed its end comparison on a coin and a wine bottle.
- 2026-11-28, dev `087613325` (after ee07e6d6c): `guest-crime-flags` 2 of 2, the same timeout. `guest-ivarstead-fine-mouse`,
  `-fine-mouse-sweep` and `-jail-mouse` 2 of 2 each: "the guard naturally arrests the party" timed out after 20 s, the
  guest left 200 units from its baseline (one run of `-sweep` crashed the host first: [A host crash in the attack
  cone hook](#a-host-crash-in-the-attack-cone-hook)). `guest-murder-party-alarm`: "the host is unseen in the guest
  theft control" once, then a lock order inversion tagged ours on the host (`+0x8185a5 > +0x11d4493` against
  `+0x11d394e > +0x818c83`). So the theft control still fails after ee07e6d6c.

## Combat

### A guest's game stalls on its first swing

Seen once, 2026-11-27, in `melee-client-hits-host-npc` on the branch of PR #150 before its melee fix
(`HookDamageActor`, `HookStagger` off `World::Get()`): the guest's game gave the harness no answer for 3 s at the
guest's first swing (`graph_bool` for `IsAttacking`: "refused: no answer"), then went on; the rerun passed. Evidence:
that worktree's `build/coop-test/results/20261127-122636-96390f`. Not investigated: a stall that long on a first
swing reads like a file load on the game's thread (the swing's clips) or a lock held, not a slow frame.

Agent task: `coop_test.py scenario --repeat 10 melee-client-hits-host-npc` on fresh games; on a stall, the watchdog's
report and the guest's log around the swing say what the main thread waited on. Not seen in ten runs: delete this.

Size: small. Needs: the duo.

### A guest's horse in a trap

Agent task: route a trap's contact and hit for a guest-owned actor that is not its player (a ridden horse) to its owner's
game, with a mounted GameTests flow in which the horse loses health once.

Size: small. Needs: no duo first (GameTests), the duo to confirm.

- **A guest's ridden horse takes no trap damage** (review of PR #124, 2026-11-24, read, not measured): on the guest it
  is `Local`, so `TrapRules::OfContact` drops its contact; on the host it is a `Copy`, so `OfHit` drops the script's
  hit, and `HostCombat::OnTrapHit` accepts only player targets. `trap_rules_tests` currently expects the drop.
  - Start: `TrapRules.h` (`OfContact`, `OfHit`), `HostCombat::OnTrapContact` / `OnTrapHit` (owner checks).

### After the host loads a save, a guest's moved objects stay

Measured 2026-11-24 (WarehouseTraps, the MCP's `retry`): the host tripped a rock trap, then loaded a save from before
it. On the host the rocks hang again (79974 at z 132, keyframed); on the guest they still lay on the floor where the
last round left them (z -100, dynamic, awake), unchanged 12 s later. The save holds no change for them, so the host
sends no row, and nothing resets the guest's own. Anything measured for a guest after a reload starts from that state:
a guest's player stepped on a stale rock, took it, and its trap round then went differently (one hit where a fresh
scene gives three or four).

Agent task: read what a guest's load of the host's save resets (references the save does not list keep the guest's
state); a GameTests flow first (the host moves a body, loads a save from before, the guest's lies where the save has it).

Size: medium. Needs: no duo first.

## Found by the playthrough flow

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
- **Unclear (triage 2026-11-28):** not rerun since copies stopped being stepped toward the cell they left and late
  snapshots stopped taking a copy back (58583b68f, 7d64cb1d1, 2026-11-25; `copies_keep_place_through_door`). The
  flow's `reward` step against a session-idle game at the same spot settles it.

## From the 2026-11-08 playtest

The user hosting, the user's brother as guest (Windows), 21:09 to 21:44 local, Riverwood then Riften; a Steam relay at
about 400 ms and 0.8% loss. Evidence: the guest's `/export` (`hdTkv.zip`, the user's Downloads) and the host's
`SkyrimCoop-Chris_Cringle-2026-11-08-2144-d61.zip` (the host's prefix, `My Games/Skyrim Special Edition/SkyrimCoop/`),
with both games' 21:26 `/bug` reports. Times are the logs' local times.

### A guest's crash loading a wipe's save

Seen once.

Agent task: Name the stale form the guest's load reached in its 21:19 dump.

Size: small. Needs: IDA, the dump; duo to prove it.

- **The guest crashed in a wipe's load** (2026-11-08 21:19, the guest's export `SkyrimCoop-player-2026-11-08-2119-e04.zip`
  in `hdTkv.zip`, with its dump). c0000005 1.3 s into loading `SkyrimCoopJoin.ess`, a lock order report just before.
  The same save (3118302 bytes packed) loaded fine at 21:32, so the save is not the cause. The second wipe's
  wrong character (the save it carried) is fixed: the load now gives the host its character back
  (`docs/re/systems/save-game.md`, "The character a save belongs to"; GameTests "Two party wipes in a row both reload
  the host's own character").
  - Read in the dump and IDA: `Thread::LoadGame_HookTarget` → `BGSSaveLoadGame::InitAndFinishLoadedForms` →
    `sub_140638B20` → `sub_140638CB0` → `sub_140638FE0` calls vfunc `+0x160` on a form whose memory has
    `BSLightingShaderMaterial`'s vtable (a freed form pointer, the memory reused); commented at `0x140639011`.
  - Start: which list `sub_140638B20` walks and which form left it freed (the log's last line before the load:
    `Temporary Remote Deleted FF000DC3`, a copy deleted as the guest disconnects).

- 2026-11-22 (`agent/copy-teardown`): likely a victim of "a worn list freed twice" (fixed, inventory-changes.md): it
  followed a disconnect, which shows the guest's held copies again, and a doubly owned block explains a freed form
  pointer whose memory holds another class. Rerun a wipe with heap history (`expect_no_double_free`) before closing.

### A guest crash at the main menu

Seen once.

Agent task: Name the crash in the guest's 20:47 dump.

Size: small. Needs: no duo (the dump).

- **The guest's game crashed at the main menu** (2026-11-08, 20:47:58, no session). c0000005 at `SkyrimSE.exe+0x1012512`
  (from `+0x6e4d16`, `+0x60f283`), our `MenuTopicManager` menu hook having run 5 s before. The dump is in the guest's
  export (`SkyrimCoop-player-2026-11-08-2054-b0e`). That bundle's `plugins.txt` lists the brother's Vortex profile
  (1,642 lines), while the session ran three plugins: which list that run had is unknown.
  - Start: the dump's stack in IDA.

## Found by the full runs of 2026-11-11 and 2026-11-12

From the three full runs on the graph input stream's branch (388 scenarios each; progress.md, "Test suite"). Every
entry here showed on dev's plugin too, or passed alone on both builds. They were triaged from the runs' messages: their
engine paths are still to be read (step 0, CLAUDE.md). The run-by-run sorting of those failures is in git history
(this file before 2026-11-16).

### A guest's store of an item held in a list

Stands alone: one container scenario.

Agent task: Make the guest's own removal of a stored item take it from the list that holds it.

Size: small. Needs: the duo (the fake holds no plain lists on an actor).

- From the review of PR #83 (read, not run): a guest storing an item its game holds only in a list that reads as
  plain (worn, or a roll's `ExtraLeveledItem`) gets a plain negative grant (`ContainerTransfers::Stack` clears the
  worn flags). `TESObjectREFR::AddOrRemoveItem` passes no list for it, and 16059 takes only the loose count, so the
  guest keeps the item the chest was given. The host's side moves the copy's real list (`HostEngine::TransferContainer`).
  Dev does the same on the guest.
  - Start: in `AddOrRemoveItem`'s plain removal, take the rest through the lists that read as plain, each by its
    `ExtraCount`, as `SetCarriedStacks` does.

### A party item removed without its tag

Stands alone: one party stack on a player's copy.

Agent task: Make a party item a guest's game removes without naming its stack (console `player.removeitem`, a script's
`RemoveItem` of the base) leave every player as the party's store says.

Size: small. Needs: GameTests, then the duo.

- **The host's copy keeps the key** (2026-11-19, `party-item-moves-guest` then `guest-pickpockets-lucan` in one boot):
  the first scenario's cleanup removes "[P] Key to Breezehome" (`93B08`) from the guest's player by console; the
  removal's event names no stack, so it is no party change (HostPartyItems), and since one-simulator.md piece 3 nothing
  else carries it (a player's items part leaves party stacks out). The host's copy holds 1, the guest 0; the next
  scenario's end comparison fails on it. Before piece 3 the relay took it off the copy, the party's store still at 1.
  - Start: `SendRemoval` (TESObjectREFR.cpp) reads no list for a plain removal; what the party's store should do then.
- **Both copies keep it** (2026-11-19, review of PR #83): a player's copy now applies only its owner's private items and
  keeps its party stacks as the party's store sets them, so after that cleanup the store's 1 stays on the host's and the
  guest's copies (players 0), and `party-item-moves-host` run next stops at "a player carries [P] Key to Breezehome to
  begin with". Alone on a fresh boot it passes.
- 2026-11-22, dev `aa723513`: the second shape again in the full run, `party-item-moves-guest` stopping at its start
  (`{'host player': 0, 'host copy': 1, 'client player': 0, 'client copy': 1}`) after the party-item scenarios before
  it, and `party-quest-item-guest` beside it ("a player carries the Golden Claw to begin with"); both passed in the
  shuffled rerun.
- 2026-11-23 (`agent/golden-claw-quest`): `party-item-moves-guest` passed, then `party-item-moves-host` stopped at its
  start with both copies holding the key: the cleanup removed it from both players by console 0.1 s apart.

### The guest's highlight with a still mouse

Stands alone: one dialogue check.

Agent task: Find why the guest's highlight stops following the host's in `dialogue-mouse-highlight-stays-shared`.

Size: small. Needs: the duo.

- **The guest's dialogue highlight does not follow the host's while the guest's mouse rests on the list**
  (2026-11-11, every run on both builds): "the guest follows the host while its mouse stays still" times out after 5 s.
  Not read further: whether the guest's still mouse re-highlights its own row each frame, or the host's move never
  arrives.
  - Start: the guest's SWF `cursor_index` and the dialogue log through the host's move.
- 2026-11-22, dev `aa723513`, 2 of 2: it now stops one step earlier, "the host follows mouse option 1" timed out
  after 5 s. The same pair of menus also disagree in `dialogue-hover-while-guest-picks`, 2 of 2: "the menus differ in a
  conversation: cursor at +9.8s: host 1 vs guest 0". Read both from the same start.
- 2026-11-28, dev `087613325`: both again 2 of 2, the same messages.

### A refused join shows nothing

Either the join panel's refusal is broken or the stand-in host is: not told apart yet.

Agent task: Run `join-refused-shows-why` and read whether `coop_peer` sends its refusal and what the guest does with it.

Size: small. Needs: the duo.

- **A guest refused by the host never shows why** (2026-11-11, both builds, with `coop_peer` built).
  `join-refused-shows-why` times out after 15 s on "[client] refused: wrong_build" and `join-panel-shows-refusal` on
  "[client] refused: full". After the first, the guest stayed "joining" for the cleanup's 60 s, so its join neither
  failed nor timed out. `progress.md` records `join-refused-shows-why` passing in an earlier combined run, so either the join
  handshake changed under the stand-in (`Code/tests/peer/`) or the guest's handling of a refusal broke.
  - Start: the guest's log from the connect to the timeout; `coop_peer`'s output for the refusal it sent.
- 2026-11-22, dev `aa723513`, `coop_peer` built in the worktree: both again 2 of 2, the same messages;
  `join-refused-plugins-differ` passes. The first leaves the guest "already connected or connecting", so the runner
  boots the duo again after it.
- 2026-11-28, dev `087613325`: both again 2 of 2, the same messages.

### A guest crashed loading a copy's idle clips

Stands alone: seen once in Riverwood (2026-11-17, `agent/mount-per-player`'s duo).

Agent task: Find why the engine's clip loader faulted on a guest as a graph-stream copy loaded an idle's clip files.

Size: small. Needs: IDA, then the duo.

- Access violation in a job thread: 63997+0xC1 (`call [rax+0x08]`, the object's vtable read from a freed pointer:
  0xFFFFFFFFFFFFFFFF) from 63980+0x1A5, `AnimationFileManagerSingleton` in RBP. The guest's last log line: "Graph stream:
  copy 1348A loads IdleMillLoadStart's clip files into set 4: loading" (the mill worker). The party had just arrived in
  Riverwood by `coc`, both players still on a horse. Crash log: the guest prefix's
  `SKSE/crash-2026-11-17-23-28-37.log`.
  - Not fixed by PR #71 (2026-11-17, a deleted copy's held clip load after a `coc`) nor PR #153 (clip sets by graph
    manager): **seen again 2026-11-27** (`agent/cleanup-guest-crash`, the six mount scenarios, heap history and quarantine on;
    guest crash log `crash-2026-11-27-07-18-55.log` in that worktree's build): the same `call [rax+0x08]` at 63997+0xC1,
    `rax` 0, on a finished load's callback (`entry+0x40`, in `RCX`, not a MemoryManager block: our `ClipsLoaded` is
    made with `new`). The entry (0x68) was queued by our `ActionClips::Request` from a graph stream's deferred request
    15.8 s before and was still live. **Read:** `ActionClips::Request`'s instant set (4) assigns the process's callback
    (`instantClipsLoaded`) through 39731, which deletes the old one at its last reference, with no cancel first; the
    queue holds only the pointer (63968). **Hypothesis, not checked:** that this entry was such a replaced set-4 load
    (the timing allows a set-3 load of the same copy too). Copy `1348C` asked for set 4 at 07:18:38.722
    (`IdleWoodPickUpEnter`, the entry's time) and again at 07:18:52.026, 2.5 s before the crash; the engine's own
    39798 cancels the instant load before it releases that callback. Start: keep the old callback by a reference until
    its load reports back (as `HeldClipLoads` does for movement sets), and a GameTests flow on the fake's loads.

### Cover plates after the host leaves

Stands alone: seven references' pose.

Agent task: Find what turns the seven `CoverPlate01` activators on one game in `host-quits-to-main-menu`.

Size: small. Needs: the duo.

- **Seven activators end posed differently** (2026-11-11, every run on both builds, `host-quits-to-main-menu` and
  `host-ingredient-knowledge-control`): ACTI `1BA5A`, references `402A8AC`, `402A8AB`, `402A8AA`, `402A89B`,
  `402A1B8` among them, nodes `CoverPlate01`, `:18` and `:8` "turned differently". The same seven every time, in two
  scenarios that share a place, so a property of the place's animated plates rather than of either scenario: an
  ambient animation each game runs on its own clock, which the world check should then not compare, or a state one
  game misses.
  - Start: whether the plates animate with nobody touching them (two world checks a few seconds apart on one game).
- 2026-11-22, dev `aa723513`: `host-quits-to-main-menu` 2 of 2 and `host-ingredient-knowledge-control` once (its
  rerun crashed the guest first), the same seven plates.
- **Read, 2026-11-22 (piece F, duo, plate `402A89C`):** before the quit both games agree. After it the graph
  states are byte-equal (`Ready`) and `CoverPlate01`'s local turn is equal (about 13 degrees about x); only its world
  turn differs, and the stale side is the **host**, which loaded its own save: its world turn is the parent's alone
  (the local turn never carried up), and its 3D lost flag `0x4000000`. The guest is right because our `ApplyGraph`
  updates the node after restoring (2026-10-29). One run also failed earlier, on the guest's rejoin ("couldn't join
  (unreachable)"). Vanilla (the stand-in, no plugin of ours; its new `@node` readout): after an
  in-game save and load in QASmoke (the load shown by the player put back from z 8884 to 6976) the cover's world turn
  is still right, flags `0x8000e`. So vanilla's load does not leave it stale; the host in session does.
- **Narrowed, 2026-11-22 (duo; the stand-in's `@node` and `@quit`, the MCP's `quit_to_main_menu` verb):**
  - Right: vanilla after an in-game load, after a load from the main menu at boot, and after `@quit`
    (`Game.QuitToMainMenu` 55490) then a load; our plugin after a fresh load from the main menu at boot, and after an
    in-game save and load in session.
  - Stale: our host, on the load right after it quits its session to the main menu (System > Quit > Main Menu), before
    any guest joins (world turn `0, -1, 0`, the parent's alone); a second, in-game load puts it right.
  - Still stale with these hook files off (`SKYRIMCOOP_DISABLE_HOOKS`, three runs): Animation, FormChanges, References,
    TESObjectREFR, SaveLoad, SaveLoadManager, LoadGameEvents, PortalGraphFix; HavokStepFix, TimeManager, Sky,
    CopyRagdollPhysics, RagdollWatch, PlayerHands, PlayerCollision, SwingWatch, KnockState, MeleeStagger, Projectile,
    NiCamera; and the 41 gameplay files from Actor to WorldEvents. Not tested: BSTimer, BSThread, LoadingScreen,
    LockOrder, PlayerCharacter, QuitToMainMenu, VirtualMachine, HUDMenuUtils, BSTaskletManager (a run with these off
    booted, then was not read); SkyrimVM64 (the main loop), BSGraphicsRenderer, Memory, input, menus and UI stay on
    for the session to run; and code the switch does not cover (the graph stream's hooks, our services).
  - Cosmetic (a cover drawn at another angle on the host until the next load); parked by the user, 2026-11-22.
- 2026-11-28, dev `087613325`: `host-quits-to-main-menu` 2 of 2, `host-ingredient-knowledge-control` once (its rerun failed on
  the guest's player still casting `409782` against its copy's nothing).
  - Start: the run with the nine untested files off; then what our session's end leaves for the next load.

### A follower's party ahead of its copies

The common shape of the end comparisons in towns: copies of the host's townsfolk and animals trailing their owners, or
off their owners' furniture, when a scenario compares the games after a travel, a rejoin or a load. A player would see
an NPC standing apart or not sitting, if it lasts.

Agent task: Find why NPCs' copies are 65-150 units from their owners when `follower-travels-with-anchor` compares:
whether the same actors agree two seconds later (`expect stays`). If they do, the scenarios' waits are the bug and this
moves to Test tools; if not, read what holds the copy (its goal, its furniture).

Size: small. Needs: the duo.

- **`follower-travels-with-anchor`** (every run since 2026-11-11): Stump 111-159 units from his copy, Dorthe 124, an elk
  65 (41 of it in height: the copy on the ground plane, z -147.0, its owner at -105.9). The actors differ each run and
  all were moving.
- The same shape elsewhere (2026-11-22 to 2026-11-26): `save-load-gate` (Dorthe 103 units; Hilde sitting on the host
  and standing on the guest, `sit_sleep` 3 against 0), `copy_holds_furniture_prop` (Dorthe 58), `guest_shows_only_hosts_npcs`
  after its rejoin (Olava, Lucia and Sigurd sitting on the host and not on the guest; Mila Valentia 66 off),
  `rejoined-guest-sees-rider` (villagers' sit state, a rabbit 121 units off, Dorthe 94, an elk's facing),
  `copies_keep_place_on_arrival` (Embry and Hilde `sit_sleep` 0 against 3; rabbits drawn against sheathed), and in
  Solitude `guest-trips-platform-guard` (townsfolk scattering from the interrupted execution 57-94 units off; Roggvir
  facing 57 degrees apart).
- Creatures' copies 57-58 units off their owners in height alone, x and y within a unit (`wolf_copy_turns_with_owner`,
  2026-11-09: wolf `9B0DB` 58 below, deer `8480B` 57 above), their graphs equal to their owners' throughout. Unread:
  what holds the copy's capsule at that height.
- 2026-11-28, dev `087613325`: `follower-travels-with-anchor` 2 of 2 (Stump 152 units, Dorthe 124, Hilde seated on the host only, an
  elk). `save-load-gate` failed twice at other points: "the door's autosave did not restore the party outside the
  inn", then a wolf's `stamina_regen.delay` 2.7 on the host and 0.0 on its copy.

### Failures only deep in a long run

Each passed alone on both builds: something an earlier scenario leaves behind, or a rare race.

Agent task: Rerun each with the scenarios that ran before it in the full run (`build/coop-test/results/` keeps the
order) and file or fix what it finds.

Size: medium. Needs: the duo.

Each failed in the full runs (2026-11-11 to 2026-11-22) and passed alone. The others of that list passed in the full
run of 2026-11-22 and were dropped (triage 2026-11-28).

- **`dialogue-refused-pick-restores-list`** (see also [The guest's chat misses a refused
  start](#the-guests-chat-misses-a-refused-start)): "[client] its chat shows the host's refusal". 2026-11-11, 2026-11-16 (the
  run's 25th scenario, so not only deep in a run) and 2026-11-22 (the 23rd); passed in each rerun.
  2026-11-28, dev `087613325`: the same in the full run, passed in the rerun.

## Seen in the final review's duo runs (2026-11-12)

Each line is a run of the branch's current code unless it says committed build. All are intermittent; none is read yet
(step 0, CLAUDE.md). Rates are from runs of 3 to 6, so they say "sometimes" and no more.

### No kill move on a giant, sabre cat or bear

`kill_move_table` (host attacker, 2026-11-25): trolls pair, but every giant, sabre cat and bear row swings plainly. The
swing's target is the victim (the harness's `kill_move_check`: `should_attack_kill` and `killable` true, `attack_target`
the victim; a reused form id made it look like an earlier victim at first). Same on dev `c5265779` (A/B on the duo).
Draugr too: 7 of 7 rows on `agent/compass-enemy-dots` (2026-11-25), not compared with dev.
2026-11-28, dev `087613325`: `kill-moves-every` 257 rows wrong, the guest attacking too: "no kill move: sabre cat, dagger" and on,
with `should_attack_kill` and `killable` true.
Which of the kill move idles' conditions refuses (the race roots, `GetTargetHeight`, `GetRelativeAngle`, the attack
data) is not read (kill-moves.md, "Every kill move").

Agent task: the same swing in the stand-in (`vanilla_start`): if vanilla plays it, read the idle tree's pick (24570)
for a giant in our game.

Size: small. Needs: the duo, IDA.

### A victim the host removes while the guest holds it stays on the guest

`kill-moves-every` (2026-11-25, the guest attacking): the guest took a bear for a kill move (its own AI state records for
session 0x5F from 12:19:52), and the host's stage removed the bear while the guest still held it. Inferred, from one log: on the host it was a
copy then, so no removal reached the guest (a search of its log found none for 0x5F): the guest's bear went back to a copy with no
owner (`desync`: "copy in client with no owner"), and the guest's next swings aimed at it (damage to 0x5F, a refused kill
move on it) instead of the new victim. Not reproduced alone (the bear
rows never paired, so nothing was taken). The claim and give-back path (`ClaimService`, `KillMoveService`) is
suspected; never A/B'd against dev, so the copy combat groups' change is not ruled out.

Agent task: take a host NPC for a guest kill move, remove it on the host before the give-back, and read whether the
host's removal of a copy it lends out reaches the holder.

Size: small. Needs: the duo.

### A guest crash in the garbage collector's job

`guest-rides-across-border`, once (2026-11-12), the guest: RIP 0 from `coop_GarbageCollector_DeleteNextForm` (36479) on a
`Job_Garbage` thread, a queued entry whose deleting destructor was null. It followed a failed `host-rides-horse` (the rider
2100 units off its feet) in the same session. Not investigated: `docs/re/crashes/2026-11-12-guest-gc-deletes-null-vtable.md`.

Agent task: reproduce with the heap history on (`coop_test.py repro`), running the five scenarios the crash note names.

Size: medium. Needs: the duo, IDA.
- 2026-11-22, dev `aa723513`: `guest-rides-across-border` passed 1 of 1 (not after a failed `host-rides-horse`).

- 2026-11-22 (`agent/copy-teardown`): a null deleting destructor fits a block with two owners ("a worn list freed
  twice", fixed). Not shown: the five scenarios with heap history on would say.

### A kill move's dead copy awake or lying apart

`kill-moves-by-weapon`, 4 of 14 rows in one run (2026-11-22), all a guest's kill move, the copy on the guest: its
ragdoll still awake 30 s after the death, twice (a bandit under `2HMSlash`, a wolf under `1HMWolfA`: the log shows
`CopyRagdollPhysics: ... at its owner's rest: step 0 (22 bodies, near false, still false, woken true)` and no later
step), and asleep but 6 to 10 units and 12 to 29 degrees off its owner's body on 8 and 2 bones (`KillMoveD`,
`ShortC`; `rests asleep` then `step 3 (near false)`). The other 10 rows, the host's four among them, lay alike. The next run: 1 of
14 (`ShortA`, 11 bones 8 to 9 units off). Also unread: the host's `2HMSlash` row gave no `KillActor` and no dying
state in its trace on either game, both runs (the verb now says so). Not
read. Not known: whether the copy's death taken when heard (that day's change, about 100 ms before its owner's
frames) adds to it; 16 rows on the builds before and after that change lay alike with a 15 s wait.

2026-11-28, dev `087613325`: 1 of 14, the guest's `pa_KillMove1HMGrappleStab` with an iron dagger: the dead victim's 18 bodies still
awake.

Agent task: `kill-moves-by-weapon --repeat 3` with a `RagdollTrace` of each victim, then read why the rest's steps
stop at 0 (`CopyRagdollPhysics`, `BodyPrediction::StepToRest`).

Size: small. Needs: the duo.

### A claimed kill's victim holds its pose for the host's replay

On the game of a guest that kills a host NPC with a kill move, the victim holds its clip's last pose 0.08 to 0.35 s
after the blow before it drops (26 kill moves, 2026-11-22, `kill_move_on`; 1.5 s and more before that day's two fixes,
`docs/re/systems/kill-moves.md`, "The claimer's victim waits for the host's word"). The death is the host's, and
the host's game kills at its own replay's `KillActor` (`HostTakeBacks`), which runs that far behind the guest's clip.

Agent task: decide whose ragdoll starts first. The host killing at the give-back (before its own replay's blow) or
the claimer dropping its victim at its own `KillActor` both start one game's ragdoll ahead of the other's frames;
measure the pull that gives a copy (`RagdollTrace`) before choosing.

Size: small. Needs: the duo.

### Sync points asked for over and over

Whole-session logs of the duo show some copies starting on a new sync point about every sixth update for long stretches: one
actor 1189 times on the host, one hare 384 on the guest, against 69 and 127 restores logged in the same sessions. In the
last three-scenario run, starts and restores matched (4 and 4, 32 and 32). Which scenario loops, and whether the asks, the
points or the logging is what repeats: not found. It costs messages and a graph restore each time, so it is the first
thing to look at when the host is profiled in a crowded cell (graph-input-stream.md, "The final review").

Agent task: log why `ShouldAsk` fires again for one looping actor (`Asked` reset by `Feed` on a gap, `Replay` on a failed
load, `SeeGraph`), in the scenario that shows it.

Size: medium. Needs: the duo, GameTests.

## Found by the full run of 2026-11-16

From the 402-scenario run on dev (`0a2f618a`, this triage's branch): 364 passed, 38 failed; then three reruns of every
entry's scenario that had passed, and of the failures new to this file. Each entry below was not listed before.
Evidence: `build/coop-test/results/<time>/at-failure.json` in the `triage-bugs` worktree. Their engine paths are still
to be read (step 0, CLAUDE.md).

### A guest that never arrives after a fast travel

`party-fast-travels`, 1 of 5 runs (the full run; `20261116-172932-1b1cf2`, `-172936-b39f96`): "[client] arrived in
QASmoke" timed out after 60 s, and the cleanup then got no answer from the guest's game (`{'cmd': 'dialogue'}
refused: no answer`), so the duo was booted again. It passed 3 of 3 after; its lock audit passed each time (the alias/list
pair: lock-order.md, "Reports read"). Not read: the guest's log through the travel.

Agent task: read the guest's log from the travel to the timeout: a load that never ended, or a hang.

Size: small. Needs: the evidence above, then the duo (`party-fast-travels --repeat 8`).
- 2026-11-22, dev `aa723513`: `party-fast-travels` passed; `late-join-gets-load-door-lock` failed the same way once
  ("[client] arrived in QASmoke" timed out after 60 s, then `{'cmd': 'dialogue'} refused: no answer`; passed in the
  rerun). The guest's game had crashed: `crash-2026-11-22-03-04-37.log` in the worktree's client prefix, a null read
  (`+0x18`) in `hkbBehaviorGraph` code (60025, from 59686, 59311, 58329) under our graph hooks (two frames of ours,
  from 63587 and 63358). So at least one such hang is that crash.

## Found by the full run of 2026-11-22

From the run on dev `aa723513` (one-simulator.md pieces 1-10 merged; the header above). Each entry failed in the full
run and again in a shuffled rerun of every failure, unless it says otherwise. Evidence:
`build/coop-test/results/<time>/at-failure.json` and the client prefix's `SKSE/crash-*.log` in the `bugs-audit-suite`
worktree. Engine paths are still to be read (step 0, CLAUDE.md) unless an entry says read.

### A host 980 units from its saved place after a load

`held_copy_shown_again_frees_no_list_twice` (2026-11-22, `agent/copy-teardown`): 3 of 10 runs failed
`host_loads_party`'s "[host] did not return to its saved place: at [-1189, 1513, 6976], saved at [-363, 2035, 6976]",
each the first run after a duo boot inside a batch; alone it passed 5 of 5 and through the MCP 2 of 2. The party had
gone to the Ivarstead inn and back to QASmoke seconds before the save. Measured: `coc QASmoke` lands the host at
(-365, 2034), so the saved place is right and the load (or something after it) left the host elsewhere in QASmoke.
`host-quickloads-party` (no travel first) passed 4 of 4 beside it. Not read: what is at (-1189, 1513), and the
host's log through the load.

2026-11-28, dev `087613325`, 1 of 1 (after a duo boot in a batch again): at (-1211, 1500), saved at (-363, 2035).

Agent task: read the host's log from `load HostLoadSelected...` to the check in a failing run (a queued travel or an
arrival placement replayed after the load?), then the engine path it names.

Size: small. Needs: the duo (`held_copy_shown_again_frees_no_list_twice` after another scenario, `--repeat 4`), IDA.

### A copy at Sheathed while its owner draws

`client-angers-host-ally` 2 of 2 (2026-11-22): the guest burns the host's ally Lydia until she turns on it; every step
passes, then the end comparison finds her owner at `state.weapon_state` 2 (Drawing, `WeaponState.h`) and her copy on
the guest at 0 (Sheathed), both times. Neither capture shows a transition: whether the copy's draw never started, or
started and was undone, is not read.

**Unclear (triage 2026-11-28):** piece D's fixes for a copy left drawing or sheathed against its owner (b5657ebee,
f9b978cfe) landed after this run; `client-angers-host-ally` was not rerun. Its owner itself read Drawing (2), a
passing state, at the end. Rerunning it settles whether this is left.

Agent task: trace Lydia's weapon state on both games through the fight, then read where the copy's draw starts or
fails to.

Size: small. Needs: the duo, then GameTests.

### The party's sword unnamed at a merchant

`party-item-sold-guest`: on 2026-11-22 (2 of 2) the guest's barter list lacked "[P] Iron Sword"; on 2026-11-23 the tag
showed (quest-object tags were fixed 2026-11-27, 606168380) and a later step failed:

2026-11-23 (`agent/golden-claw-quest`, 1 of 1): the guest's sale found "[P] Iron Sword" and went through (nobody
carried it, the purse was paid); the host's buy-back then failed, Belethor on the host listing no sword. The host's log
shows the guest's sale only as the guest's count change (`Party item 12EB7 ... asked -1`, no container put): the sold
sword reaches no merchant on the host, so there is nothing to buy back. Not read: where a guest's barter sale goes.

2026-11-28, dev `087613325`, 2 of 2: the tag missing again, "[P] Iron Sword is absent" with a plain "Iron Sword" listed, after
606168380.

Agent task: read where a guest's barter sale lands on the host, and check the tag in the guest's barter list again.

Size: small. Needs: the duo, then GameTests.

### A fighter deleted mid-fight holds the party's respawn

Seen 2026-11-27 (`drag_rounds`): a host wolf fighting the standing host player, frozen, then removed (`unstage`) while
in combat. The dead guest stayed down with "Party in combat" for good. `HostDeath::StandingPlayerFights` reads each
character's last AI state (`m_ai`), and nothing seems to drop a deleted actor's (not read). In play: an NPC a script
disables or a summon that expires mid-fight would hold every downed player's respawn.

Agent task: read what removes a character's entry from `HostDeath`'s records when its actor goes; a HostAuthorityTests
case for a fighter removed mid-fight.

Size: small. Needs: no duo for the test.

### A dragged corpse's copy apart from its owner

`guest-drags-host-corpse` 2 of 2 (2026-11-22): the guest drags a host wolf's corpse back. First run: its copy on the
guest does not lie as the host's body (bones 1 to 19 degrees and 1-8 units off). Second: "[host] the ragdoll of
FF0019A6 comes to rest" timed out after 30 s. Not read.

Agent task: trace the corpse's bodies on both games after the drag (`ragdoll_trace`), then read which game's rest is
kept.

Size: small. Needs: the duo, then GameTests.

### Seen once in the run of 2026-11-22

Each failed once and passed in the shuffled rerun (or ran once). Recorded so a second sighting can join them.

Agent task: when one shows again, give it its own entry.

Size: small. Needs: the duo.

- `dive-host-player`: "copy's health while under water [97, 98, 99, 100], owner's 100".
- `names-in-every-case`: its end comparison found a container (`C2CE8`, CONT `C2CDD`) with 1 of each of about 15
  items on one game and 0 on the other.
- `guest-sets-down-item`: "[host] shows it where it rests" timed out after 5 s.
- `guest-bump-moves-host`: "[host] 1 objects back where they were" timed out after 30 s.
- `world-changes-reach-guest`: "the client's world does not follow the host's". Second sighting 2026-11-27 (the
  cleanup branch `20a4acde`, 1 of 8 runs that night): the `open` step, the host saw `lock_changed` and `open`, the
  guest only `lock_changed`; the next 4 runs passed.
- `guest-steals-created-item` (scene, one run): "the party's bounty is {'host': {}, 'guest': {}}, expected at least 1".
- `object-rests-beside-orgnar` (scene, one run): "[guest] cheese has the host's rest" timed out after 2 s.
- `guest-rejoins-while-joined` and `guest_shows_only_hosts_npcs` (scenes): see [Headtracking on copies](#headtracking-on-copies)
  and [A follower's party ahead of its copies](#a-followers-party-ahead-of-its-copies).
- `guest-weather-own-region` (2026-11-23, every step passed): its end comparison found a Snow Fox (`107C2B`, Winterhold)
  at `weapon_state` 0 on the host and its copy on the guest at 3 (Drawn). The reverse of
  [A copy at Sheathed while its owner draws](#a-copy-at-sheathed-while-its-owner-draws); not read whether its copy
  missed a sheathe.
  Second sighting 2026-11-28, dev `087613325`: the same fox's copy idle (animation action, event and idle 0) against its owner's
  `moveStop`.
- `guest-mounts-bought-horse` (2026-11-23, every step passed): its end comparison found Ria (`1A6D8`, QASmoke) at
  `values.stamina_regen.delay` 1.17 on the host and 0.0 on its copy. A new readout (the stamina-regen gates, same
  day); not read whether a copy's delay should follow its owner's at all.
- `guest-rides-host-horse` (2026-11-28, `agent/killmove-simultaneous-race`, right after `furniture-client-player`): the
  guest crashed (c0000005) 0.2 s after its mount package began, replaying the host's copy's graph input stream:
  `GraphStreams::Replay` -> `SkyrimGraphTarget::SendEvent` -> `BShkbAnimationGraph::SendEvent` 63591 ->
  `hkbBehaviorGraph::HandleEventOnActiveNodes` 58377 -> `hkbStateMachine::StartTransition` 59383 -> `SetActiveState`
  59390 -> 59411 (+0x24). Not read. 0 of 3 again in the same order on the same build.

- Older single sightings, each not seen since in its scenario's later runs (moved here 2026-11-28):
  - `guest-makes-character-then-joins` (2026-11-11, 1 of 3): "[client] the starting kit changed", `F11B6` beside the
    two expected items.
  - The guest's horse menu lacked the bought horse (2026-11-16, once).
  - `knockdown-lies-as-host` (2026-11-12, once in about 25): Lydia's copy placed 62 units above its owner as she stood.
  - `death-spectate-guest` (2026-11-16, 1 of 7): the guest's player held effect `B8782` eighteen times before its
    death and none after its revive.
  - A deer's copy leaving water jumped 47 units in a frame and back (2026-11-24, `6E297` in Riverwood, `races`), on
    its first step after `Place`.
  - The HUD crashed in GFx (`FloatingQuestMarker::Update` 51676 on the guest, the compass update 51668 on the host;
    a read of 0xFFFFFFFFFFFFFFFF), 2026-11-15, once each, during physics trials.

## Found by the full run of 2026-11-28

The run of all 460 scenarios on dev `087613325` (this triage's branch; it changes no code): 369 passed, 44 failed, 48
never ran (a guest crash, the first entry, left the duo unbootable). Then a shuffled rerun of the 92, and of the 44 the
same crash stopped again. "2 of 2" counts the full run and a rerun. Evidence: `build/coop-test/results/<time>/` and
the prefixes' `SKSE/crash-*.log` in the `bugs-triage` worktree. Engine paths are not read (step 0, CLAUDE.md) unless
an entry says read.

### A guest crash in `unequipall` after a reload

`guest_keeps_own_state_on_reload` 2 of 2: its steps pass, its cleanup's console `player.unequipall` on the guest is
"still queued after 10s", and when it runs the guest crashes: a read of freed memory (the quarantine's `0xDBDB...`
poison) in `InventoryChanges::VisitWornItems` (16096+0x45) under `UnequipAll` (38899) from the console. So the
player's worn list the visitor walks was freed under it, after the scenario's reload. Each time the runner then
could not boot the duo again, so every later scenario went unrun. Crash logs: `crash-2026-11-28-05-21-34.log` and
`-05-52-03.log` (client prefix). A worn list freed twice was fixed on `agent/copy-teardown` (2026-11-22); this one is
the player's own.

Agent task: read which owner frees the guest player's worn list across a reload (`ExtraWornList`, our inventory
apply) and why the console line waits 10 s first.

Size: small. Needs: IDA, the duo (`guest_keeps_own_state_on_reload`, heap history on), then GameTests.

### A host crash in the attack cone hook

`guest-ivarstead-fine-mouse-sweep`, 1 of 3 runs: the host crashed on an AI job thread, Harrald (`19DEB`) swinging an
iron dagger in a UseWeapon procedure: `HookInAttackCone` (`Combat/RidersPassThrough.cpp`, 47297) ->
`HitRules::RideTogether` -> `PartyOf` (`Combat/HitOwnership.h:15`) -> `ActorExtension::IsLocalPlayer`, a read at
`0x490`. Read from the code: `PartyOf` checks the actor for null but not its extension; `Actor::GetExtension` returns
null for an actor that is neither a `Character` form nor the player. Not read: which target the cone was asked about.
Crash log: the host prefix's `crash-2026-11-28-05-25-43.log`. The hook came with 5fc86cd97 (riders pass through).

Agent task: find the target with no extension, guard `PartyOf` (or the hook) for it, and add a GameTests case of an
NPC's swing at such a target.

Size: small. Needs: IDA, then GameTests.

### A player's combat flag on, its copy's off

`death-host-npc` 2 of 2 stops on its first removal: "desync before removing": the guest's player (the owner) at
`ai_state.in_combat` true and its copy on the host false, and `values.stamina_regen.rate` 1.75 against 5.0 (the
in-combat rate). The same pair, host or guest player, ended `party-travel-mounted-guest`, `dive-host-player` (both
passed in the rerun) and `host-drags-dead-guest` (1 of 1). Either a player's combat flag outlives its fight, or a
copy never takes its owner's: not read.

Agent task: read whether each owner's `in_combat` is right at the failure (`at-failure.json`), then where a player's
copy takes its combat flag.

Size: small. Needs: the duo, then GameTests.

### A merchant's barter line never offered to a guest

`dialogue-guest-barter-earns-speech` and `dialogue-guest-placed-barter-reopens`, 2 of 2 each, apart in the shuffled
order: "the merchant offers its barter line" timed out after 30 s. Moved from "Failures only deep in a long run" (seen
since 2026-11-16): it now fails alone.

Agent task: read the guest's topic list for the merchant at the timeout (`JudgedFor`) and whether the host judged the
barter line for the guest's character.

Size: small. Needs: the duo.

### The guest's chat misses a refused start

`dialogue-refused-start` 2 of 2: the guest talks to a copy whose owner on the host is in combat, and "[client] its
chat says the host can't talk to them" times out after 10 s; the cleanup then waits for the host's conversation to
end. `dialogue-refused-pick-restores-list` misses its refusal in chat the same way, but only now and then.

Agent task: read whether the host refused (its log) and whether the refusal reached the guest's chat.

Size: small. Needs: the duo.

### The guest's Load list never opens

`guest-cannot-load` 2 of 2: the guest's console load and quickload are refused in chat as meant; then the host's
Journal > System > Load list opens and the guest's does not ("the load list opens" timed out after 10 s). **Unclear:**
since 9eab5ed60 (2026-11-27) a game's Load lists only the loaded playthrough's saves, so a guest with none may show no
list at all, and the scenario's expectation would be the stale part.

Agent task: read what the guest's Load list does with no save of its playthrough, then fix the scenario or the menu.

Size: small. Needs: the duo.

### A new guest's race menu

`guest-makes-character-then-joins`: "[client] the Body category is open" timed out after 10 s, then the host refused
the guest ("has no character in the host's save and brings none") and the duo was booted again (2026-11-22, and once
here, passing in the rerun). `first-time-join-breton` failed the same menu once ("[client] the race list takes the
press" timed out), then its end comparison on a door's lock level (`200CAAC`, 25 against 0).

Agent task: read the race menu's state at the timeout (the menus open, the category list) in a failing run.

Size: small. Needs: the duo (`--repeat 4`).

### A guest's rejoin from the host's menu load

`host-menu-loads-party`: "the guest loads the save from its store and rejoins" timed out after 120 s (2026-11-22, and
in this run's second rerun). Its full run failed earlier: Aela's copy on the guest with action, event and idle 0
against her owner's `torchEquip`.

Agent task: read the guest's log from the host's load to the timeout: the store load, then the rejoin.

Size: small. Needs: the duo.

### Seen once in the run of 2026-11-28

Each failed once and passed in a rerun, or ran once. Agent task: when one shows again, give it its own entry.

Size: small. Needs: the duo.

- `guest-arrival-leaves-host-in-place`: the guest crashed loading, a read of freed memory in
  `ExtraAliasInstanceArray` (12728+0x48) from `TESObjectREFR::FinishLoadGame` (19531) under
  `BGSDynamicPersistenceManager::PromoteList` (15328) called by our code; Aela and the Animal Pelt Collection quest on
  the stack. `crash-2026-11-28-03-51-50.log` (client prefix).
- `load-lists-only-playthrough` (passed in the full run): the host crashed in `BGSSaveLoadManager::Save_Impl` (35727),
  passed through by `HookSaveImpl` (`SaveGameHooks.cpp:35`) for a queued engine save request, ucrtbase reading a freed
  string (the save's name?). `crash-2026-11-28-05-55-26.log` (host prefix).
- `kill-moves-every`, 40 minutes in: the guest crashed in d3d11 as `BSTriShape::LoadBinary` loaded a water mesh
  (`Water2048`, `Ripples02`). The runner then could not boot the duo, so `guest-barter-follows-merchant-stock` went
  unrun (its full run had failed: "[client] the copy's reconcile takes the sword").
- `guest-drags-dead-host`: "the dead player's own body lies 1 units from where it fell: the drag never reached its
  game", then in the rerun "the wolf fights the standing player" timed out after 5 s.
- `knockdown-front-and-back`: the copy's visible skeleton up to 17.6 units from its owner's.
- `dive-client-player`: the guest's player at `swimStop` and its copy on the host at `SwimStart`.
- `party-item-moves-host` and `-guest`: both players' copies carried the house key and neither player did, at the start.
- `party-buys-horse-host`: "[client] its player is off its horse" timed out (riding true, sit state 3).
- `created-chest-reaches-guest-empty`: "[client] does not take input: menus ['Fader Menu']".
- `dialogue-guest-s9-exits` ("the guest opens dialogue alone" timed out) and `dialogue-guest-picks-at-greeting-end`
  ("the host accepts the selected topic and INFO", the guest's SWF unchanged by the press).
- `rejoined-guest-sees-rider`: its end comparison on an iron longsword (`35514`): in the world on one game, inside a
  container on the other.

## Test tools

### The gate fails on GameTests now and then

`scripts/check.sh` fails on a GameTests case now and then and passes on the next run with nothing changed (since
2026-11-11; the gate keeps each shard's log and prints the failed case, `build/check/`). The case seen most is "A host
NPC gets up from a knockdown: its copy's ragdoll follows its owner's until both stand" (`physics_flows.cpp`: the copy's
head 15.7 to 42.4 units from its owner's, limit 15; about 3 in 100 under load, 13 of 440 on dev `c5265779`; not the
blend, measured with `RagdollStart`'s blend on and off).

2026-11-27, measured (dev `bba3a3b2`, the gate's eight shards, 28 cores): 0 of 4 runs failed on an idle machine; 6 of 6
with 26 busy loops beside them, on ten cases of 645: six in `physics_flows.cpp` (the ragdoll cases at 514, 653 and
997, the bucket cases at 2095 and 2200), `copy_playback_flows.cpp` 178 and 484, `animation_replay_flows.cpp` 605,
`friendly_fire_flows.cpp` 422, `travel_flows.cpp` 533. Two kinds: a `Party::Until` that gives up after 2 s of wall
time, and a check of a result that real sleeps were meant to line up within milliseconds. The cause is the harness,
not the cases: an in-process game runs in wall time (the host's network tick, `SynchronizedClock`, enet's own timers,
`Seams.cpp`'s frame delta), so a busy machine changes what a test sees. Counting frames instead of seconds would not
help: the loop spins frames while it waits for real time.

Agent task: time the tests step. The seams that exist: `World::Step(delta, frameMs)`, `FakeEngine::FrameMs`,
`HostNetwork`'s in-process routes. What is missing: the session tick and `SynchronizedClock` from a clock the test
advances, an in-memory line in place of enet between the test's games, the 29 `now()` calls in `Code/sync` through
that clock, and the tests' own `while (now() < until)` loops as "run this many ms". A design first, proven on
`physics_flows.cpp`.

Size: medium to large. Needs: nothing but the fast suite.

### The end comparison's ambient differences

Scenarios that pass every step and then fail the runner's end comparison on world objects a player cannot tell apart
between the games, a different set each run. Each kind is either game-local (leave it out of the comparison, as piece
F did for containers' `0x8000000` and loose items' places) or a real desync (give it an entry).

Agent task: for each kind below, two world checks a few seconds apart on one game say whether it moves on its own;
leave the game-local ones out of the comparison and file the rest.

Size: medium. Needs: the duo.

- About 55 fish references (ACTI `10C3BA`) posed differently in Riverwood (`copy-leaves-furniture-steady`,
  `faendal-letter`), 59 references in Whiterun (`copy-holds-spell-after-trip`), 2026-11-05 to 2026-11-07.
- Trees' branch bones (TREE `5D2D7`, `5D2D9`: `BranchBoneCluster02`, `BranchBoneLow02`) turned differently,
  `golden-claw-guest` and `golden-claw-party-tag` (2026-11-27, 1-6 references a run): likely the wind pose.
- Furniture `1B752` (FURN `74A4E`) graph variable `IsReady` 1 on the host, 0 on the guest (`courier-force-greet-continues`,
  2026-11-27).
- Weapon racks (ACTI `8AF59` `WeaponRackMidACTIVATOR`) with `0x8000000` on one game (`copy-keep-spell-after-party-travel`,
  2026-11-22: an activator has no container).
- Resting objects a few units apart: the Riverwood forge's sword (35514, `IronLongSword01`, 5 units:
  `rejoined-guest-sees-rider`, `two-players-race-for-horse`, 2026-11-26/27), a ladle and the sharpening wheel (the
  Ivarstead crime scenarios), a coin and a wine bottle (`guest-theft-at-bar-bounty`), a door among 45-52 references
  (`copies_follow_owners_graphs_in_town`, 2026-11-09).
- The same scenarios' guest `take` from Arvel's body missed twice in four runs ("the crosshair is on nothing, not
  arvel: 130/191 units off"): the body still moving after the console kill. `take` should wait for the body to rest
  (`wait_rest`) before it aims.

### Scenario checks that misjudge

Scenarios whose own step or check is wrong, so a run fails (or would pass) for the harness's reasons. Each was a bug
entry before the triage of 2026-11-28.

Agent task: fix each scenario's step or check as its bullet says, and run it to see what it then shows.

Size: small each. Needs: the duo.

- **`copy-strafes-with-host`'s stutter check counts frames, not time** (`movement.py` `frame_speeds` takes every frame
  at 60 a second), so a 25 ms frame on a loaded machine reads as a stutter. Judge only frames whose neighbours ran
  evenly (the trace stamps each frame, `t`). It failed 12 of 12 on a quiet machine on 2026-11-09 and passed 4 of 4 on
  2026-11-22: the copy's capsule catching at one spot in `QASmoke` (x -398, y 1800) was not seen since.
- **The end comparison compares a copy's last action**, which only the comparison and the owner's resurrect stamp
  read (`HookResurrect`); the graph is the stream's. After a travel the host's new copy of a guest's player held the
  action before its owner's last (`JumpFall` against `swimStop`, `JumpLand` against `JumpLandEnd`; once `state1`'s
  swimming bit), 2 of 9 runs of `party-travel-mounted-guest` (2026-11-11) and once in `party-buys-house-host`
  (2026-11-22). Leave the field out, or trace which message the host drops (`party-travel-mounted-guest`).
- **`form-state-round-trips` reads a copy's base factions** (section `0x40`), but `Actor::SetFactions` (37677) writes
  a created base's reference's `ExtraFactionChanges`, never the base's list (read in IDA). The guest's join-time
  script adds a sixth faction a second after its announcement; the copy has it, its base lists five, so the check
  fails on every fresh join (2026-11-18, 2026-11-22). Read the copy's factions as the engine does (base and reference).
- **`green-flash-ivarstead-wine`'s mouse sweep drops the prop** "before the contact probe completed" (2026-11-16: 3 of
  4; 2026-11-22: 1 of 4). Start: the game's grab readout at the failure (`at-failure.json`) against the sweep. 2026-11-28, dev `087613325`: 2 of 2.
- **A respawned player's Ready Weapon is pressed while it falls into place** (`death-spectate-host`,
  `client-melee-kill-credit`; once each in 58 runs, 2026-11-12): 0.65 s after the respawn, `WeapEquip` refused by the
  engine mid-fall. The respawn step should wait for the player to stand.
- **`death-spectate-host`'s wolf turns to the guest's copy** (2026-11-16, 1 of 4): the party's NPCs hit the nearest
  player by design, and the guest stood closer. Hold the guest away from the staged fight.
- **`frozen-wolf-kill-move` waits on a state that lasts only until the host's refusal comes back** ("[client] player's
  kill_move is True" timed out after 3 s, 2026-11-22, once): wait on what the refusal leaves instead.

### A scenario stages its speaker frozen

Harness only: the scenario stops at its own staging.

Agent task: Stage Vilkas in `placed-speaker-deleted-mid-conversation` so the talk verb takes him, and see what the
scenario then shows.

Size: small. Needs: the duo.

- **`placed-speaker-deleted-mid-conversation` fails before its point** (every run on both builds, 2026-11-11; known
  since `talk` began refusing a frozen speaker, progress.md): "vilkas is staged frozen: stage a speaker with frozen:
  false" (`actions/play_verbs.py`: a frozen speaker never finishes a line). The scenario stages him frozen, so whether a placed speaker deleted mid-conversation is
  handled has no running check.
  - Start: stage him unfrozen and restrained, as `dialogue.py`'s other placed speakers are.
- 2026-11-22, dev `aa723513`: 2 of 2, the same message.
- 2026-11-28, dev `087613325`: 1 of 1, the same message.

### A staging spot 1500 units off with no floor

`dialogue-nobody-moves` 2 of 2 (2026-11-22) stops at its staging: `place` puts the host's player 1500 units from the
guest at bearing 90 and finds "no clear spot". Both failure files report obstructions at 90 (a copy 832 units from the
spot), 230, 250 and 270 degrees, and no floor at the other bearings. `place` checks the sight line only after its floor
ray hits (`EngineCalls.cpp`), so the obstructed bearings have floor: no candidate passes every check, which is the
measured failure. Whether a conversation goes on with nobody moving has no running check meanwhile.
2026-11-28, dev `087613325`: 2 of 2, the same refusal.

Agent task: stage the host where `place` finds a clear spot (a shorter distance, or a bearing clear of the copy), and
see what the scenario then shows.

Size: small. Needs: the duo.
