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
- [After a guest rejoins](#after-a-guest-rejoins): small
- [A copy's interaction idle](#a-copys-interaction-idle): small
- [A copy's graph after a turn or a pick](#a-copys-graph-after-a-turn-or-a-pick): small
- [Headtracking on copies](#headtracking-on-copies): medium
- [Animation mods' random picks](#animation-mods-random-picks): small
- [Creature copies wobble](#creature-copies-wobble): small
- [A copy without its weapon](#a-copy-without-its-weapon): medium
- [End comparisons in towns](#end-comparisons-in-towns): medium
- [A strafing copy catches for a frame](#a-strafing-copy-catches-for-a-frame): small
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

- [A follower stops following](#a-follower-stops-following): not reproducible on 1.7.104 (2026-11-07), not worked on
- [A guest's recruitment stalls](#a-guests-recruitment-stalls): small
- [A deleted copy in an AI job](#a-deleted-copy-in-an-ai-job): small
- [A guest crashes attaching a cell after the mount scenarios](#a-guest-crashes-attaching-a-cell-after-the-mount-scenarios): medium
- [A passenger jumps 57 units as it takes the reins](#a-passenger-jumps-57-units-as-it-takes-the-reins): small

**Saves**

- [The scratch save in the Load list](#the-scratch-save-in-the-load-list): small, no duo

**Plugin compatibility**

- [Quest start and stop events](#quest-start-and-stop-events): medium

**Crime and aggro**

- [The older crime scenarios](#the-older-crime-scenarios): medium

**Combat**

- [After the host dies](#after-the-host-dies): small, the lock order inversion left
- [A guest's game stalls on its first swing](#a-guests-game-stalls-on-its-first-swing): small
- [A guest's horse in a trap](#a-guests-horse-in-a-trap): small, no duo first
- [After the host loads a save, a guest's moved objects stay](#after-the-host-loads-a-save-a-guests-moved-objects-stay): medium

**Found by the playthrough flow**

- [The host's copy after a door](#the-hosts-copy-after-a-door): small
- [Players stacked after a door](#players-stacked-after-a-door): small

**From the 2026-11-08 playtest**

- [A guest's crash loading a wipe's save](#a-guests-crash-loading-a-wipes-save): small
- [A guest crash at the main menu](#a-guest-crash-at-the-main-menu): small, no duo

**Found by the full runs of 2026-11-11 and 2026-11-12**

- [A starting kit with an extra item](#a-starting-kit-with-an-extra-item): small
- [A guest's store of an item held in a list](#a-guests-store-of-an-item-held-in-a-list): small
- [A party item removed without its tag](#a-party-item-removed-without-its-tag): small
- [An action sent before its copy exists](#an-action-sent-before-its-copy-exists): small
- [The green flash scenarios' grab](#the-green-flash-scenarios-grab): small
- [The guest's highlight with a still mouse](#the-guests-highlight-with-a-still-mouse): small
- [A refused join shows nothing](#a-refused-join-shows-nothing): small
- [A player at a chest reads lower than its copy](#a-player-at-a-chest-reads-lower-than-its-copy): small
- [A rider at a scenario's end](#a-rider-at-a-scenarios-end): small
- [A guest crashed loading a copy's idle clips](#a-guest-crashed-loading-a-copys-idle-clips): small
- [Cover plates after the host leaves](#cover-plates-after-the-host-leaves): small
- [A follower's party ahead of its copies](#a-followers-party-ahead-of-its-copies): small
- [Failures only deep in a long run](#failures-only-deep-in-a-long-run): medium
- [A character's base bytes after joining](#a-characters-base-bytes-after-joining): small

**Seen in the final review's duo runs (2026-11-12)**

- [A guest's console kill that never downs it](#a-guests-console-kill-that-never-downs-it): small, not read
- [A kill move that never pairs](#a-kill-move-that-never-pairs): small
- [No kill move on a giant, sabre cat or bear](#no-kill-move-on-a-giant-sabre-cat-or-bear): small
- [A victim the host removes while the guest holds it stays on the guest](#a-victim-the-host-removes-while-the-guest-holds-it-stays-on-the-guest): small
- [A guest crash in the garbage collector's job](#a-guest-crash-in-the-garbage-collectors-job): medium
- [A respawned player's draw that does nothing](#a-respawned-players-draw-that-does-nothing): small
- [The attacker's copy not in its kill move](#the-attackers-copy-not-in-its-kill-move): small
- [A kill move's dead copy awake or lying apart](#a-kill-moves-dead-copy-awake-or-lying-apart): small
- [A claimed kill's victim holds its pose for the host's replay](#a-claimed-kills-victim-holds-its-pose-for-the-hosts-replay): small
- [A knocked-down copy placed 62 units off](#a-knocked-down-copy-placed-62-units-off): small
- [Sync points asked for over and over](#sync-points-asked-for-over-and-over): medium

**Found by the full run of 2026-11-16**

- [A revived guest's abilities](#a-revived-guests-abilities): small
- [The death scenario's wolf turns on the other player](#the-death-scenarios-wolf-turns-on-the-other-player): small
- [A guest that never arrives after a fast travel](#a-guest-that-never-arrives-after-a-fast-travel): small

**Found by the full run of 2026-11-22**

- [A host 980 units from its saved place after a load](#a-host-980-units-from-its-saved-place-after-a-load): small
- [A copy at Sheathed while its owner draws](#a-copy-at-sheathed-while-its-owner-draws): small
- [The party's sword unnamed at a merchant](#the-partys-sword-unnamed-at-a-merchant): small
- [A dragged corpse's copy apart from its owner](#a-dragged-corpses-copy-apart-from-its-owner): small
- [A copy's corpse slips out of the hand sooner](#a-copys-corpse-slips-out-of-the-hand-sooner): small
- [A fighter deleted mid-fight holds the party's respawn](#a-fighter-deleted-mid-fight-holds-the-partys-respawn): small
- [Seen once in the run of 2026-11-22](#seen-once-in-the-run-of-2026-11-22): small

**Test tools**

- [The gate fails on GameTests now and then](#the-gate-fails-on-gametests-now-and-then): small
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
- 2026-11-22, dev `aa723513`: `party-buys-house-host` 2 of 2 failed, neither on Proventus: once a player copy's
  action ([An action sent before its copy exists](#an-action-sent-before-its-copy-exists)), once the lock pair.

### After a guest rejoins

The harness's `rejoin_client` joins again from a running game, keeping that game's world; a player leaves to the main menu and joins
with the host's save (`joins_with_world`). Differences follow both paths at Lucan's in Riverwood (2026-11-07). The
references its end comparison found (a fork, a wine bottle, a salmon, the hanging plants, containers by `0x8000000`)
are no longer compared (piece 10, and piece F 2026-11-22: [piece-f-plan.md](design/piece-f-plan.md)).

Agent task: find why a rejoined guest's armor and Lucan's head tracking differ, starting from the scenarios below.

Size: small. Needs: the duo (Riverwood), then GameTests.

- **A guest joining again does not wear what it wore** (`guest-swaps-armor-after-join`, 2026-11-10): `[client] the guest
  wears iron armor again` times out, on dev's plugin 3 runs in 4 and on the graph input stream's 2 in 3 and 2 in 4
  (with and without players on the stream). The first run of a session passes more often than the next ones. Not
  read. 2026-11-22, dev `aa723513`: 4 of 4 passed (the full run and 3 reruns); no fast test covers it, so it stays
  until one does.
- **2026-11-22, dev `aa723513`:** `guest-rejoins-while-joined` 2 of 2 now stops before its end comparison: Lucan's
  `ai_state.head_track` differs 4.5-7.1 s into a 10 s hold (host `['session 4']`, guest `['session 4', 'session 4']`,
  and the reverse in the other run). `guest_shows_only_hosts_npcs` 2 of 2 ends on townsfolk after its rejoin:
  Olava, Lucia and Sigurd sitting on the host (`sit_sleep` 3) and not on the guest (0); then Mila Valentia 66 units
  off and Olfina on another action. The Skeleton that stayed off its owner (2026-11-09) was in neither run: with the
  run of 2026-11-21 that is the two clean runs its bullet asked for, and GameTests "A guest joining beside a host
  corpse sees it lie as the host's" covers it, so that bullet is gone.

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

**Unclear, 2026-11-22:** the live check read no run of 2 s apart through `death-spectate-host` (48 samples),
`death-spectate-guest` (84) and `party-travel-dead-guest` (69) on dev `aa723513`, one run each. No fast test has a
wolf's copy beside a player's death, so the entry stays until one does.

One shape left of the old live check's runs of 2 s or more. The others (a wolf's turns, creatures' idle picks, the
counter idle's pick) were a copy's graph fed different inputs: on the graph input stream every streamed copy equals
its owner at every update (`wolf_copy_turns_with_owner` 180 s, Whiterun 120 s, `npc-counter-copy-falls-back-in`,
2026-11-09).

Agent task: rerun the death scenarios with the stream's live check (`expect stream`) and say whether this shape is left.

Size: small. Needs: the duo (`death-spectate-host`, `death-spectate-guest`, `party-travel-dead-guest`).

- **A wolf `Attack1State` beside a player's death** (`FF000E05`, 13.8 s, `death-spectate-host`; `FF000826`, 15.4 s,
  `death-spectate-guest`; `FF000859`, 7.7 s, and `FF000D74`, 8.8 s, `party-travel-dead-guest`, until the actor stopped being
  read; all before the stream): its owner's `RootBehavior` stays in the attack, its copy in combat idle. May be the
  owner's graph held by the kill: [After the host dies](#after-the-host-dies).

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

Some NPCs' and other players' copies show no weapon on a guest's game.

Agent task: Add a readout of the weapon's 3D on a copy, catch it empty-handed, then read what took the weapon. Make the
equipment snapshot compare the hands' weapons as it does their spells.

Size: medium. Needs: IDA, a harness readout, duo, then GameTests.

- **Seen by the user in play** (2026-11-06), not reproduced: party travel and one fight kept hands and worn items agreeing
  (bugs-map.md, "A copy's hands"). A move did strip a copy's armour until the 3 s reconcile.
- The snapshot compares worn items and hand spells, not hand weapons; a copy refuses the engine's equips.
- Lead: copy `13485`'s engine tried to equip about once a second for over 2 minutes in the 2026-11-04 playtest.
- **A mechanism, measured** (2026-11-19, one-simulator.md piece 3): the engine's outfit reset (TESNPC 418622, run on a
  3D load by 24725) empties a copy's hands and leaves the weapon's worn flag, so a repair comparing worn flags saw
  nothing (harness `reset_worn` on Aela's copy: hands [0, 0], her owner's torch and bow). The repair now checks each
  hand holds its worn weapon or torch (GameTests "A copy whose hands the engine's outfit reset emptied holds its
  owner's weapon again"). Not closed: no scenario of play that fails on dev shows a copy without its weapon.
- **A hand-slot difference in scenarios** (2026-11-22, dev `aa723513`): `rejoined-guest-sees-rider` 2 of 2 ends with
  Faendal's copy on the guest holding `[0, 80261]` where his owner holds `[80261, 80261]` (`state.hands`, the process's
  slots), and `elder-scroll-vision-reaches-party` (late save) with the host player's copy at `[0, 80260]` against
  `[80260, 80260]`. Both are bows (`13985` Hunting Bow, `13984` Long Bow), sheathed on both games (`weapon_state` 0,
  `weapon_drawn` false), the copy still holding the bow in the other slot. So it is a slot-0 mismatch on a sheathed
  bow; whether it shows (a drawn bow, its 3D) is not observed, and the user's report stays unreproduced. Start from
  `rejoined-guest-sees-rider`'s `at-failure.json`: what the hand repair compares for a form held in both slots.
  Also `guest-weather-own-region` (2026-11-22, branch `agent/weather-per-region`): both Winterhold Guards' copies at
  `[0, 79261]` / `[0, 80257]` against their owners' `[X, X]`, after the guest's solo moves and timescale 2000 (night).
- **Not the repair's equip** (2026-11-22, piece D step 0): read in IDA, `coop_RealEquip` (38929) takes a bow's own
  BothHands slot unless the race's flags bit 30 keeps the passed hand; measured on `rejoined-guest-sees-rider`, the
  repair's RightHand equip of Faendal's bow on his copy left `[13985, 13985]`. The `[0, bow]` writer is still unread
  (39811 sets one hand per call: outfit reset, MoveToHigh, disarm; `docs/re/systems/actor-state.md`).
- **The bow's slot fixed** (2026-11-23, PR #108, re/systems/hand-slots.md): measured, Faendal's copy is at `[0, bow]` at
  its first repair after a rejoin (the writer not read), and the repair checked one hand per item. It now checks both
  hands of a two-hander (the right only beside a worn torch) and equips it with BothHands; `rejoined-guest-sees-rider` passes (GameTests
  "A copy left holding its bow in one hand holds it in both again").
- **The guards' torches (2026-11-22 playtest) not reproduced** (2026-11-23): two Winterhold guards at night held
  `[torch, 0]` and `[torch, sword]` on both games for 30 s. No equip puts a torch in the right hand (17619), so a host
  guard with one in each hand would come from elsewhere; a `/bug` on one would show its hands and worn marks.
- Still open: the report itself, a weapon's 3D missing in play, is not observed; the 3D readout above is the next step.

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
  references). (Hod's `pose.head` 117 against 88, once, was a culled
  skeleton read as visible through the stale `was_in_frustum`: the observer now reads `pose_visibility.drawn`,
  culled-npc-pose-readings.md, 2026-11-05.)
  - Test: that scenario, after its steps.
  - The rabbit and Olava with no owner are fixed: [copy-lifecycle.md](re/systems/copy-lifecycle.md).
  - A creature's copy 57-58 units off its owner in height, x and y within a unit, at the end of
    `wolf_copy_turns_with_owner` (2026-11-09: wolf `9B0DB` 58 below, deer `8480B` 57 above, one each in two runs). Their
    graphs matched their owners' at every update through those runs (the live check), so not the graph. Unread: what
    holds the copy's capsule at that height.
  - `copies_follow_owners_graphs_in_town` (Whiterun, 2026-11-09) ends with a door among 45-52 references.
  - Piece F (2026-11-22, [piece-f-plan.md](design/piece-f-plan.md)): containers' `0x8000000` (the host's leveled
    roll, 16128; the guest is given the same items) and where loose items lie are no longer compared; Riverwood's mill
    log follows its worker on every game (F3: a copy's furniture gets its graph events, scenario
    `copy-worker-drives-furniture` 2 of 2).
  - `guest-trips-platform-guard` (Solitude, 2026-11-17, three runs) passes its steps and ends with townsfolk scattering
    from the interrupted execution 57-94 units off their owners (Lisette, Greta, Jala; Roggvir 66 off and facing 57 deg
    apart), and once the guest's player 40 units into the floor, owner and copy alike. Unread: whether the copies lag a
    run or hold a stale point.
- 2026-11-22, dev `aa723513`: `copies_follow_owners_graphs_in_town` passed; `copy_holds_furniture_prop` ended on
  Dorthe's copy 58 units off, `guest-trips-platform-guard` on Evette San 75 units off and the guest's copy. On the
  `LateAfterDragonslayerBackinSkyrim` boot `faendal-letter` passed whole (once), and
  `copy-keeps-spell-after-party-travel` ended on 12 references (weapon racks, ACTI `8AF59`
  `WeaponRackMidACTIVATOR`, with `0x8000000` on one game: not read, an activator has no container).
  2026-11-23: the Sleeping Giant's rack weapons ended apart too; `HoldAt` each update until asleep passes
  `guest-grabs-orgnar-bottle` (1 run), `docs/re/systems/physics.md`.

### A strafing copy catches for a frame

**Unclear, 2026-11-22:** `copy-strafes-with-host` passed 4 of 4 on dev `aa723513` (the full run and 3 reruns), where it
failed 12 of 12 on a quiet machine before. What changed is not shown and no fast test covers it, so the entry stays
until one does (or the shape below is read).

`copy-strafes-with-host` fails on dev's plugin as often as on the graph input stream's (2026-11-09: dev 17 runs in 27,
the branch 30 in 44; back to back on a quiet machine dev 12 in 12, the branch 9 in 12). Two things trip its stutter
check, neither a copy out of step with its owner.

Agent task: read why the copy's capsule catches where its owner's does not, and make the check count real time.

Size: small. Needs: duo, IDA (the copy's step, `StepCopy` under 37356).

- **The copy moves short one frame and long the next at one spot** (4.0 then 8.5 units where its owner moves 5.8 a
  frame), with its goal, its playback and the watching game's frames all steady. In those two frames it jogs 2 units
  sideways and back (root x -398.0, -400.3, -397.7 at y 1800-1804 in `QASmoke`, the same place in every run, 4-7 frames
  after it starts): its capsule meets something its owner's passes. Not read: what is there, and why a stepped copy
  (`CopyMove::Step`) loses the frame.
- **A long frame reads as a stutter.** The check takes each frame's move at 60 frames a second
  (`movement.py` `frame_speeds`), so a 25 ms frame of either game shows a move half again as long. Runs with other
  load on the machine had 100-200 such frames of 600 and failed most often. The trace stamps each frame (`t`, in the
  plugin): judging only frames whose neighbours ran evenly drops those and leaves the shape above.

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

## Physics

- **A copy leaving water jumps 47 units in a frame and back** (2026-11-24, deer `6E297` in Riverwood, 7.5 s after an
  arrival, so not the arrival's own race): its owner's snapshot turned on the ground, so it went from placed to stepped;
  the next frames it was at z -224 and (11175, -51018), then back near its goal (11187, -51037, -269). Seen once
  (`races`); the engine path is not read. Start: its controller's state as it is stepped the first time after `Place`.
- **`copies_keep_place_on_arrival` fails its end comparison** (2026-11-25, every race step passing): back in Riverwood
  seated NPCs' copies (Embry, Hilde) read `sit_sleep` 0 against 3, rabbits' `weapon_drawn`/`weapon_state` true/3 against
  false/0-1. Embry and a fox read so in `scene`'s own list before the fix. The seats: as Olava's above, not traced here.

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
- **The guest crashed once in the HUD** (not seen in the 2026-11-18 reruns) (`FloatingQuestMarker::Update` 51676 into `GFxValue::GetDisplayInfo`, read
  of 0xFFFFFFFFFFFFFFFF, 2026-11-15 18:54, log in `build/coop-test/kept/`), during a carry trial; not traced,
  maybe not physics. The host crashed the same way at 20:48 (HUD compass update 51668 into GFx 83250/87834, same
  bad read, 28 minutes up; `kept/host-crash-2026-11-15-20-48-12.log`).
- **The harness's `world_state` crashed the host once** (access violation in `EngineCalls.cpp` `ReadPose`, walking an
  object's 3D), on the first call after staging; not seen again (nor in the 2026-11-18 reruns).
- **GameTests "A host NPC gets up from a knockdown: its copy's ragdoll follows its owner's until both stand" fails 1
  run in 13 to 20** (the copy's head ends 25 to 32 units from its owner's, 0 while both rose): measured 2026-11-23, 3
  of 40 runs on dev `4dc32008` and 2 of 40 on `agent/physics-audit`, so not from the authority change. With dev `38e7098d`
  (pose blending) merged and the touch fix: 8 of 30 alone; which of the two raised it was not measured. Not traced:
  the copy stands a playback delay before its owner's last frames are due, by wall-clock frame timing.
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
  - Follow-up: graph bounds/types, descriptor invalidation and owner-value restoration are implemented; concurrent animation-snapshot protection is also implemented. Repeated native transitions pass, but the historical crash remains open. See [stability follow-up](v1-session-items-2026-10-28.md#stability-follow-up).
  - Still not reproduced (2026-11-18, `agent/bugs-dialogue`): `host-beast-form-stays-host` passed 2 of 2 on dev.
  - **The stuck copy shows now** (2026-11-22, dev `aa723513`, 2 of 2, no crash): the end comparison finds the host's
    werewolf owner on `IdleWerewolfTransformation` (action 77826) and its copy on the guest on `WeapUnEquip` (action
    289711). So the copy never took the transform its owner did (the copy "refused `IdleWerewolfTransformation`" of
    2026-10-28). Start from that run's `at-failure.json` and the guest's log at the transform.
  - Start: what the copy's graph holds when the owner's race changes back (the respawn from its new base data).
  - Test: a `GameTests` race switch back with the copy mid-transform, once the fake keeps a copy's graph state.

## Party travel

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
- 2026-11-22, dev `aa723513`: `save-load-gate` failed 2 of 2 on townsfolk copies at its comparison before removing
  its follower ([A follower's party ahead of its copies](#a-followers-party-ahead-of-its-copies)), not on this shape.

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
`crash-2026-11-07-14-58-08.log`, not kept. Unread.

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
- 2026-11-26, dev `3d763578`: `rejoined-guest-sees-rider` 4 of 4 fail on the end comparison, each on another nearby
  actor or object (villagers' sit state, a rabbit 121 units from its copy, a mill log), with two agents compiling
  beside the games. The cleanup branch fails the same way (5 of 5: an elk's facing, Dorthe 94 units off, a rabbit).
  The same sword at Riverwood's forge (35514, `IronLongSword01`, resting 5 units apart) ends dev's fourth run and
  `two-players-race-for-horse` once on the cleanup branch (2026-11-27; it passed the run before).
- From the closed "Whiterun plains' critters differ" (2026-11-20, the claims refactor and piece 5, not read): once the
  horse itself 53 units from its copy at the end, once two host elks' copies about 159 units off their owners. Not seen
  in the two passing runs of 2026-11-22.

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

## Crime and aggro

### The older crime scenarios

Since 2026-11-22 two arrests pass (`guest-guard-arrest-jail` and `guest-guard-arrest-resist`, 4 of 4 each on dev
`aa723513`); the fine, the theft controls and the Ivarstead arrests still stop before their point.

- **2026-11-22, dev `aa723513`** (the full run, then a shuffled rerun of each failure):
  - "the host is unseen in the guest theft control": `guest-lockpick-party-alarm`, `guest-pickpocket-party-alarm`,
    `guest-murder-party-alarm`, `guest-container-theft-and-fine` 2 of 2; `guest-theft-host-witnesses` and
    `guest-assault-party-alarm` 1 of 2.
  - `guest-guard-arrest-fine` 2 of 2 ("the sentence or fine moves the host", then "the witnessed theft charges the
    party"); `host-guard-arrest-jail` 2 of 4, each on a lock order report tagged ours on the guest
    (`+0x10587a6 > +0x16a695` against `+0x17014d > +0x1058a00`), not read.
  - `guest-crime-flags`, which passed on 2026-11-05, now 2 of 2: "payment ends the guest's native alarm/combat" timed
    out after 10 s.
  - `guest-ivarstead-fine-mouse`, `-jail-mouse`, `-fine-mouse-sweep`: 2 of 2 each, the guest's game crashing in four of
    the six (a worn list freed twice, fixed 2026-11-22: inventory-changes.md), the others "the guard naturally arrests
    the party" timed out. 2026-11-22 after the fix and the clip deadlock's (animation-file-loads.md, "A request under a
    graph's lock"): no guest crash or hang in 6 runs (`-jail-mouse` 3, `-fine-mouse-sweep` 3); what still fails them is that
    timeout (1 run) and the end comparison's clutter (a ladle, the sharpening wheel: 2 runs).
  - Scenes: `guest-theft-warned` (one run) took nothing back ("guest_player carries 0 of 64B36 and orgnar holds 0"),
    `guest-theft-at-bar-bounty` passed its steps and failed its end comparison on a coin and a wine bottle,
    `guest-theft-aggro` passed.

- `guest-theft-warned` (2026-11-19, dev `9c55b453` and `agent/piece-3-items` alike): "the party has spent 0 theft
  warnings, expected 1", 3 runs; a fourth left the host stuck at `coc RiverwoodSleepingGiantInn`, which only a new
  boot cleared. It passed when written (2026-11-13).

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
  - Since 2026-11-13 (later the same day) a guest's seen theft is warned like the host's (`crime.md`, "The steal
    warning"); a warning held off by an NPC's scene still reports the theft. These were not rerun since.
  - Matters because a guest's alarm stays on the host's player so that the arrest works (`crime.md`): that is read from
    the code, not run.
  - Full run and reruns of 2026-11-11, dev's plugin and `agent/copy-idle-graph` alike:
    - "the host is unseen in the guest theft control": `guest-lockpick-party-alarm` every run on both;
      `guest-theft-host-witnesses` and `guest-murder-party-alarm` 2 of 4 on the branch, passed once on dev.
    - "the fine confiscates stolen rolls and keeps the unowned one": `guest-container-theft-and-fine`, alone on both
      (in the full run it stopped on the unseen control first).
      Found 2026-11-19 (`agent/piece-3-items`): the guest's taken rolls were not stolen on the guest's own game (16059
      stamps a theft's owner only on the item it gives a target, the host's copy of the guest; inventory-changes.md).
      Fixed there (`guest-container-theft-is-stolen` fails 3 of 3 on dev, passes 5 of 5); this scenario then passed
      the confiscation once and stopped on the unseen control twice, as on dev.
    - "the guard naturally arrests the party" (20 s): `guest-ivarstead-fine-mouse` every run on both;
      `guest-ivarstead-jail-mouse` in the full run only. Two of them left a player behind: the host's player 27-32
      units into the floor, the guest's copy 200 units from its owner.
  - Full run and three reruns of 2026-11-16, on dev (`0a2f618a`):
    - Still stopping at the witnessed theft: `guest-guard-arrest-fine`, `-resist`, `-jail` and `host-guard-arrest-jail`,
      the full run (the four were not rerun).
    - "the host is unseen in the guest theft control": `guest-container-theft-and-fine` (full run),
      `guest-murder-party-alarm` 2 of 3 reruns (passed in the full run), `guest-theft-host-witnesses` 1 of 3.
    - `guest-ivarstead-jail-mouse` 1 of 4: "the guard sees the guest at 9E572" timed out.
    - Pass now: `guest-lockpick-party-alarm` and `guest-ivarstead-fine-mouse`, 4 of 4 each (every run on 2026-11-11).

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

### After the host dies

What is left of it. The half-stop (NPCs searching for a still guest once the host died) was the guest's copy making none
of its owner's noise, fixed 2026-11-21 (`docs/re/systems/stealth.md`, "A player's noise"; scenario
`guest-bump-heard-on-host`, GameTests "A guest's bump is heard on the host").

Size: small. Needs: duo.

- **Lock order inversion with the host's player dead** (2026-11-05, `host-dies-guest-still-fought`, 2 of 6 runs, with and
  without the fix): `ExtraAliasInstanceArray` 12720 (0x140187955) then the player's reference lock in `Actor::IsGhost`
  (0x1406756F4), against 0x140172A5B then 12719 (0x140187881); our code on the stack, which frame not read.
- 2026-11-22, dev `aa723513`: `host-dies-guest-still-fought` passed 3 of 3 (its lock audit too). At 2 of 6 before,
  three runs do not show the pair gone.
- 2026-11-22, measured hook sizes (agent/hook-sizes): passed 1 of 1 without the pair; only the engine's Havok/list pair
  (lock-order.md). If it returns, its report's `ours from` line names the frame that tagged it.

### A guest's horse in a trap

Agent task: route a trap's contact and hit for a guest-owned actor that is not its player (a ridden horse) to its owner's
game, with a mounted GameTests flow in which the horse loses health once.

Size: small. Needs: no duo first (GameTests), the duo to confirm.

- **A guest's ridden horse takes no trap damage** (review of PR #124, 2026-11-24, read, not measured): on the guest it
  is `Local`, so `TrapRules::OfContact` drops its contact; on the host it is a `Copy`, so `OfHit` drops the script's
  hit, and `HostCombat::OnTrapHit` accepts only player targets. `trap_rules_tests` currently expects the drop.
  - Start: `TrapRules.h` (`OfContact`, `OfHit`), `HostCombat::OnTrapContact` / `OnTrapHit` (owner checks).

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

### A starting kit with an extra item

`guest-makes-character-then-joins` (1 of 3, 2026-11-11): `[client] the starting kit changed`, with `F11B6` beside the
two expected items. First seen in this run. Unread: what `F11B6` is and which side added it.

Agent task: read where `F11B6` came from, in the guest's log and the host's.

Size: small. Needs: the duo.
- 2026-11-22, dev `aa723513`: not seen in 2 runs (the full run stopped earlier, at "[client] the Body category is
  open"; the rerun passed whole).

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

### An action sent before its copy exists

Stands alone: a copy's last action.

Agent task: Make a copy made just after its owner's action hold that action as its last.

Size: small. Needs: GameTests.

- **A copy's last action stays one behind its owner's** (`party-travel-mounted-guest`, 2 of 9 on the branch,
  2026-11-11; the scenario itself fails 6 of 10 on dev for other reasons). After the party's travel the guest's player
  sent `swimStop` 0.8 s after `JumpFall`; the host's new copy kept `JumpFall` (once `SwimStart`, with the swimming bit
  of `state1` still set). Actions ride movement updates (`CharacterService::OnReferencesMoveRequest`), which are
  dropped for an actor with no copy yet, and the spawn carries the owner's last action from when it was announced.
  On the stream the copy's graph is still its owner's (its sync point); the action record and actor state are not.
  - The same shape after the other travels, in the full run only (each passed alone on both builds):
    `party-fast-travels` (owner `JumpLand`, idle 689444; copy `JumpFall`, idle 686365) and `party-buys-house-guest`
    (owner `JumpLandEnd`, copy `JumpLand`).
  - 2026-11-16, dev: `party-travel-mounted-guest` 1 of 3 reruns, the guest's owner on `swimStop`, the host's copy on
    `SwimStart` with `state1` 33792 against 32768.
  - 2026-11-17 (`agent/horse-riding`): the host's record of a player now takes the actions it relays
    (`HostMovement::OnMovement`), so a spawn built from the record (no host copy, a respawn) plays the latest;
    HostAuthorityTests "a player's latest relayed action". Not yet seen on the duo whether the travels' copies come
    from the record or from the host's copy (`ReadCopy`, its own action, the one behind).
  - Start: which spawn the host's new copy came from after the travel: the record, or an earlier one in flight.
- 2026-11-22, dev `aa723513`: `party-travel-mounted-guest`, `party-fast-travels` passed; `party-buys-house-host`
  ended once on the shape (the guest's player `JumpLandEnd`, idle 704248; the host's copy `JumpLand`, idle 689444),
  then on the alias/list lock pair tagged ours by a stale hook size (measured since 2026-11-22: lock-order.md).
- **2026-11-22, piece D:** the window is not found by reading. A host NPC's spawn is built from its live state when
  sent (a GameTests flow of an action before the copy exists passes), and the host's record of a player takes the
  last action of every movement update. Next: trace `party-travel-mounted-guest` for which message the host's game
  drops. A copy's last action is read only by the comparison and the owner's resurrect stamp (`HookResurrect`).

### The green flash scenarios' grab

Stands alone: one scenario stops before its point.

Agent task: find why the mouse sweep in `green-flash-ivarstead-wine` drops the wine before the contact probe is done.

Size: small. Needs: the duo.

- **`green-flash-ivarstead-wine` stops on its grab** (2026-11-16: 3 of 4, the full run and two of three reruns): "the
  mouse sweep dropped the prop before the contact probe completed". Its neighbours pass now (`-handoff` and `-guest` 4
  of 4, the observer scenarios), so the S2 green-flash fix has a running check; this one is the wine's grab alone.
  - Start: the game's grab readout at the failure (`at-failure.json`) against the scenario's sweep.
- 2026-11-22, dev `aa723513`: 3 of 4 passed (the full run, then 2 of 3 reruns); the failure was the same message.

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

### A player at a chest reads lower than its copy

**Unclear, 2026-11-22:** `chest-take-all` and `furniture-host-player` passed 4 of 4 each on dev `aa723513` (the full
run and 3 reruns), where the chest failed every run before. What changed is not shown and no fast test covers it.

Stands alone: one end comparison.

Agent task: Find what the guest's player is doing at the end of `chest-take-all` that its copy is not.

Size: small. Needs: the duo.

- 2026-11-16, dev's full run: both owners this time, the host's player `pose.head` 81 against its copy's 124 and the
  guest's 63 against 124, the guest's read as kneeling (pelvis 27 above its feet).
- **After taking all from a chest the guest's player stands 26-31 units lower than its copy on the host**
  (`chest-take-all`, 2026-11-11, every run on both builds): `pose.head` 93-96 on the owner against 124 on the copy,
  `pose.pelvis` 45 against 71; once the owner also read as kneeling (pelvis 26 above its feet). The copy's values are a
  standing body's. So the owner is still bent or crouched when the comparison runs, and its copy never was or already
  stood up. Not read: the action each holds at that moment.
  - Start: `observe` both at the end: the owner's last action and sneak state against the copy's.

### A rider at a scenario's end

Left of it (2026-11-23): the guest's horse menu once lacking the bought horse. The crouched rider ("pelvis 0") was a
get-off within ~0.1 s of being seated (mounts.md); a get-off that does nothing with the bought horse is it standing in
its shed, with no room beside it (mounts.md, "No room to get off"): not a bug.

Agent task: when the horse menu shows the gap again, find what the guest's menu lists.

Size: small. Needs: the duo.

- **The guest's horse menu lacks the horse** (2026-11-16, once): not seen again on a clean world.
  - Start: buy through the MCP (`talk`, the topics), then `mount_readout` and the horse's packages before getting on.

### A guest crashed loading a copy's idle clips

Stands alone: seen once in Riverwood (2026-11-17, `agent/mount-per-player`'s duo).

Agent task: Find why the engine's clip loader faulted on a guest as a graph-stream copy loaded an idle's clip files.

Size: small. Needs: IDA, then the duo.

- Access violation in a job thread: 63997+0xC1 (`call [rax+0x08]`, the object's vtable read from a freed pointer:
  0xFFFFFFFFFFFFFFFF) from 63980+0x1A5, `AnimationFileManagerSingleton` in RBP. The guest's last log line: "Graph stream:
  copy 1348A loads IdleMillLoadStart's clip files into set 4: loading" (the mill worker). The party had just arrived in
  Riverwood by `coc`, both players still on a horse. Crash log: the guest prefix's
  `SKSE/crash-2026-11-17-23-28-37.log`.
  - Probably fixed by `agent/coc-whiterun-crash` (PR #71, merged 2026-11-17: the same job, 63997, faulting on a
    deleted copy's held clip load after a `coc`); rerun a `coc` with riders to confirm, then delete this entry.
  - **Seen again 2026-11-27** (`agent/cleanup-guest-crash`, the six mount scenarios, heap history and quarantine on;
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
  - Start: the run with the nine untested files off; then what our session's end leaves for the next load.

### A follower's party ahead of its copies

Stands alone: one travel scenario.

Agent task: Find why NPCs' copies are 65-150 units from their owners when `follower-travels-with-anchor` compares.

Size: small. Needs: the duo.

- **Copies of the host's NPCs trail their owners at the comparison before the follower is removed** (2026-11-11,
  every run on both builds): Stump 111-150 units from his copy, Dorthe 124, an elk 65 (41 of it in height: the copy
  on the ground plane, z -147.0, its owner at -105.9). The actors differ each run and all were moving, so either the
  comparison runs before the copies settle after the travel, or copies in a just-loaded area lag by more than the
  check allows.
  - Start: whether the same actors agree two seconds later (`expect stays`); if they do, the scenario's wait is the
    bug, and this moves to Test tools.
- 2026-11-22, dev `aa723513`: 2 of 2, Stump 159 and 146 units from his copy. The same shape in other scenarios that
  end in Riverwood: `save-load-gate` (Dorthe 103 units, then Hilde sitting on the host and standing on the guest,
  `sit_sleep` 3 against 0), `copy_holds_furniture_prop` (Dorthe 58), and in Solitude `guest-trips-platform-guard`
  (Evette San 75). Townsfolk copies trailing or off their owners' furniture at a comparison is the common shape.

### Failures only deep in a long run

Each passed alone on both builds: something an earlier scenario leaves behind, or a rare race.

Agent task: Rerun each with the scenarios that ran before it in the full run (`build/coop-test/results/` keeps the
order) and file or fix what it finds.

Size: medium. Needs: the duo.

Each failed once in the 388-scenario run of 2026-11-11 and passed alone on dev's plugin and on the branch:

- **`dialogue-refused-pick-restores-list`:** "[client] its chat shows the host's refusal". Again 2026-11-16, the
  run's 25th scenario, so not only deep in a run; 3 of 3 alone after. Again 2026-11-22 (the 23rd), then passed in the
  shuffled rerun.
- **`guest-changes-looks-in-session`:** "the vampire change replaced the existing copy". Passed in the full run of
  2026-11-22.
- **`host-kill-move`:** "[client] wolf's graph sends KillActor" timed out after 5 s. Passed in the full run of
  2026-11-22.
- **`dialogue-guest-barter-earns-speech` and `dialogue-guest-placed-barter-reopens`,** one after the other: "the
  merchant offers its barter line" timed out after 30 s. 2026-11-22: both again one after the other in the full run,
  then `-earns-speech` again in the shuffled rerun (so it fails away from its neighbour too), `-placed-barter-reopens`
  passed.
- **`dialogue-two-starts-at-once`:** "[client] FF000D73 is between the player and andurs": an actor some earlier
  scenario left in the cell. Passed in the full run of 2026-11-22.
- Gone 2026-11-22 (dev `aa723513`, 4 of 4 each: the full run and 3 reruns): `party-aggro-host-first` (the enemy's
  flip, its first hit) and `furniture-host-player` (the guest's player lower than its copy).
- The head-tracking four are under [Headtracking on copies](#headtracking-on-copies), the two travel ones under
  [An action sent before its copy exists](#an-action-sent-before-its-copy-exists),
  `form-state-round-trips` under [A character's base bytes after joining](#a-characters-base-bytes-after-joining).

### A character's base bytes after joining

The host's copy of the guest's character lacks one of its factions.

Agent task: find which faction the guest's player has that the host's copy of it lacks, and what gives a copy's base
its owner's factions after the spawn.

Size: small. Needs: the duo (`form-state-round-trips` after a full run's earlier scenarios), then GameTests.

- **`form-state-round-trips` fails deep in a full run** (once at 388 scenarios on 2026-11-11, once 225 scenarios in on
  2026-11-16; it passed alone on both builds before). The message reads now: section `0x40` differs (TESNPC's
  factions change flag, CommonLib `kFactions`), the guest's own base `1800c9c1...04c6b00c00` against the copy's
  `1400c9c1...0500`: the copy's list is one entry shorter. So something an earlier scenario does puts the guest's
  player in a faction that never reaches its copy's base. progress.md's loose end on a copy's spawn writing its NPC
  base's factions (`SkyrimBridge.cpp`) is the place to start.
- **Now on every fresh join** (2026-11-18, dev `f7a72494` and piece 2 alike, the same bytes): the guest announces with
  five factions and its own join-time script adds a sixth about a second later. The faction reaches the host's copy
  (piece 2: the factions part, applied with six), but `Actor::SetFactions` goes through 37677, which on a non-unique
  base (a copy's created base) writes the reference's `ExtraFactionChanges`, never the base's list (read in IDA). So
  the copy's base keeps the snapshot's five. Passes after a reload (the announcement then carries six). Start: decide
  whether the check should read the copy's factions (base and reference) or the copy's base list should be written.
- 2026-11-22, dev `aa723513`: 2 of 2, the same bytes (`...04c6b00c00` against `...0500`).

## Seen in the final review's duo runs (2026-11-12)

Each line is a run of the branch's current code unless it says committed build. All are intermittent; none is read yet
(step 0, CLAUDE.md). Rates are from runs of 3 to 6, so they say "sometimes" and no more.

### A guest's console kill that never downs it

Agent task: find why the guest's `player.kill` sometimes leaves its player up in `names-in-every-case`, after the
host's load. Do step 0 in IDA first.

Size: small. Needs: the duo. Waits for: nothing.

- **`names-in-every-case`: "Timed out after 15s: [client] the player is down"** (2026-11-18: 2 of 8 mixed runs on
  the piece 2 branch; then 10 back-to-back on dev `f7a72494` and 10 on piece 2: 0 of each). The step follows the host's load of a save
  and the party's rejoin; it passed alone and after the kill move scenarios alike. No crash log; the failure's
  readout showed the guest standing in the baseline cell. Not read.
- 2026-11-22, dev `aa723513`: the kill step passed both runs (the full run failed its end comparison on a
  container instead: [Seen once](#seen-once-in-the-run-of-2026-11-22)).

### A kill move that never pairs

`kill-move-kills-guest`: `Timed out after 3s: [host] the kill move pairs the host with the guest's copy`, 3 of about 16
runs on builds with the review's edits (once with the player left 100 units off its baseline, so the duo booted again),
then none in 40 on `1a4a44a4` and none in 40 on `0492014e` (2026-11-12). Chance, or the wrong check those builds carried.
The same 80 runs passed 27 of 40 and 24 of 40: the scenario failed a third of the time on both, always in the dead copy's
ragdoll (flicker, `NEVER came to rest`). With deaths on the stream (2026-11-12): 20 of 23, three flickers of 2 to 5. The dead copy's `NEVER came to rest` is fixed (2026-11-21, one-simulator.md piece 10:
its rest is glided to, put once and slept; 6 of 6 runs asleep on the duo).

Agent task: run `kill-move-kills-guest --repeat 20` on `1a4a44a4` and on the current code, then read why the pair did not
start (`HookTryMoveTo`, the instance, `PairOnStream`).

Size: small. Needs: the duo, IDA.
- 2026-11-22, dev `aa723513`: `kill-move-kills-guest` passed 1 of 1. Too few runs to close.
- 2026-11-22, `agent/kill-moves-polish`: passed 1 of 1 again; `kill_move_on` paired 35 of 35 (a bandit or a wolf, eight
  weapon classes, either player). Still too few of this scenario to close.

### No kill move on a giant, sabre cat or bear

`kill_move_table` (host attacker, 2026-11-25): trolls pair, but every giant, sabre cat and bear row swings plainly. The
swing's target is the victim (the harness's `kill_move_check`: `should_attack_kill` and `killable` true, `attack_target`
the victim; a reused form id made it look like an earlier victim at first). Same on dev `c5265779` (A/B on the duo).
Draugr too: 7 of 7 rows on `agent/compass-enemy-dots` (2026-11-25), not compared with dev.
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

### A respawned player's draw that does nothing

`death-spectate-host` (`[host] hands ready`) and `client-melee-kill-credit` right after `friendly-fire-kill`
(`[client] hands ready`), once each in 58 runs (2026-11-12): the just respawned player presses Ready Weapon and no draw
follows in 5 s. The host's log has the press 0.65 s after the respawn, the player still falling into place (`JumpFall`,
`SwimStart`, `WeapEquip` refused by the engine). Not seen in the session's earlier runs, and in none of 34 more runs after
(24 with deaths on the stream, 8 without).

Agent task: make the respawn step wait for the player to stand (its fall over) before it presses, then see what is left.

Size: small. Needs: the duo.
- 2026-11-22, dev `aa723513`: `death-spectate-host`, `client-melee-kill-credit` and `friendly-fire-kill` passed
  1 of 1 each. Too few runs to close at 1 in 58.

### The attacker's copy not in its kill move

`host-kill-move`: `Timed out after 3s: [client] player's kill_move is True`, once in 14 runs on the build that records
through every pair (2026-11-12); not seen in about 20 runs before it. On its stream the attacker's copy is put in the kill
move by `AnimationSystem` at its owner's `pa_Kill` action (`ReplayState::EntersKillMove`); a copy off its stream at that
moment plays the action instead. Which path missed: not read.

Agent task: log which branch took the `pa_Kill` action for the attacker's copy, then `host-kill-move --repeat 30`.

Size: small. Needs: the duo.
- 2026-11-22, dev `aa723513`: `host-kill-move` passed; `frozen-wolf-kill-move` failed once with the same message
  ("[client] player's kill_move is True" timed out after 3 s) and passed in the rerun.
- 2026-11-22, `agent/kill-moves-polish`: `kill_move_on` now says when either game's attacker or victim never entered
  the kill move: 0 of 23 kill moves (7 the host's), and `host-kill-move` and `frozen-wolf-kill-move` passed 1 of 1.
  Not read further. `frozen-wolf-kill-move` waits on a state that lasts only until the host's refusal comes back.

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

### A knocked-down copy placed 62 units off

`knockdown-lies-as-host`: `desync before removing FF00099A: Lydia placement.position: host [-987, 2095, 6976] vs client
copy [-987, 2094, 7037]` once (62 units up) in about 25 runs. Also once `the copy 250 ms behind its owner; turned away from
its owner's facing once, 31 degrees for 33 ms`, and `bone 8 / bone 29 off` shapes (the get-up entry, closed 2026-11-16). Rate
over the day: 3 of 5 and 0 of 3 only while a wrong check refused sync points (removed), 6 of 6 and 3 of 3 once it was out.

Agent task: read what places a copy that stands up 62 units above the floor (its capsule against the bodies' last frame).

Size: small. Needs: IDA, the duo.
- 2026-11-22, dev `aa723513`: `knockdown-lies-as-host` passed 1 of 1.

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

### A revived guest's abilities

`death-spectate-guest`, 1 of 7 runs (a rerun; `20261116-190032-8574d7`): "the revived player's effects ... are not its
abilities before". Before the death the guest's player held effect `B8782` (755586) eighteen times over beside its
other abilities; after the revive none of them, the rest the same. Which list is wrong, eighteen copies of one effect
before or none after, is not read.

Agent task: read what `B8782` is and what put it on the player eighteen times, then what the revive gives back
(`death.md`, abilities on revive).

Size: small. Needs: the duo (`death-spectate-guest --repeat 6`), then GameTests.
- 2026-11-22, dev `aa723513`: `death-spectate-guest` passed 1 of 1.

### The death scenario's wolf turns on the other player

`death-spectate-host`, 1 of 4 runs (a rerun; `20261116-185958-19a23a`): "the enemy's hit kills the player" timed out
after 15 s with the host's player at 12 health. The host's wolf (`FF0014C9`, at x -186) was not fighting the host's
player (x -726, about 540 units off) but the guest's player's copy (`FF0013E5`, x -43), swinging at it
(`attackStart_Attack1`). So the wolf switched from the victim the scenario staged to the other player. Not read: what
switched it (the guest's copy closer, a hit from the guest, or the party's aggro rules), and whether the scenario
should hold the guest away.

Agent task: read why the wolf's target moved to the guest's copy (`combat_watch` through the fight), then decide
whether it is the game's choice (the scenario stages wrong) or ours.

Size: small. Needs: the duo (`death-spectate-host --repeat 6`).
- 2026-11-22, dev `aa723513`: `death-spectate-host` passed 1 of 1.

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

Agent task: read the host's log from `load HostLoadSelected...` to the check in a failing run (a queued travel or an
arrival placement replayed after the load?), then the engine path it names.

Size: small. Needs: the duo (`held_copy_shown_again_frees_no_list_twice` after another scenario, `--repeat 4`), IDA.

### A copy at Sheathed while its owner draws

`client-angers-host-ally` 2 of 2 (2026-11-22): the guest burns the host's ally Lydia until she turns on it; every step
passes, then the end comparison finds her owner at `state.weapon_state` 2 (Drawing, `WeaponState.h`) and her copy on
the guest at 0 (Sheathed), both times. Neither capture shows a transition: whether the copy's draw never started, or
started and was undone, is not read.

Agent task: trace Lydia's weapon state on both games through the fight, then read where the copy's draw starts or
fails to.

Size: small. Needs: the duo, then GameTests.

### The party's sword unnamed at a merchant

`party-item-sold-guest` 2 of 2 (2026-11-22): the guest opens the barter menu to sell the party's sword and the list has
"Iron Sword" but no "[P] Iron Sword": the party item's name tag is not on it in the guest's barter list, so the
scenario cannot pick it. Not read: whether the guest still holds it as the party's stack, or only the tag is missing
from that menu's names.

2026-11-23 (`agent/golden-claw-quest`, 1 of 1): the guest's sale found "[P] Iron Sword" and went through (nobody
carried it, the purse was paid); the host's buy-back then failed, Belethor on the host listing no sword. The host's log
shows the guest's sale only as the guest's count change (`Party item 12EB7 ... asked -1`, no container put): the sold
sword reaches no merchant on the host, so there is nothing to buy back. Not read: where a guest's barter sale goes.

Agent task: read the guest's party stacks and the barter menu's names at that step, and where a guest's sale lands on
the host.

Size: small. Needs: the duo, then GameTests.

### A fighter deleted mid-fight holds the party's respawn

Seen 2026-11-27 (`drag_rounds`): a host wolf fighting the standing host player, frozen, then removed (`unstage`) while
in combat. The dead guest stayed down with "Party in combat" for good. `HostDeath::StandingPlayerFights` reads each
character's last AI state (`m_ai`), and nothing seems to drop a deleted actor's (not read). In play: an NPC a script
disables or a summon that expires mid-fight would hold every downed player's respawn.

Agent task: read what removes a character's entry from `HostDeath`'s records when its actor goes; a HostAuthorityTests
case for a fighter removed mid-fight.

Size: small. Needs: no duo for the test.

### A copy's corpse slips out of the hand sooner

`host-drags-dead-guest` (2026-11-27, after the fix that takes a copy's held corpse out of the played group): the host's
player walking backwards with the dead guest's copy let go after 1.3 s and 1.8 s (two runs); the host's own dead
bandit (base 13BA1), dragged the same way, held the whole 2 s (one run). The copy's bones follow the hand meanwhile
(33 units in 1.8 s) and the guest's own body follows them. Not read: which collider pair and `BodyGroups::Decide`
result pinned the copy's bones in the played group before the fix (only the hook switch showed it), and which still
differs in the moving group, or whether it is chance. The fake's pinned-grab rule (`FakeEngine::PlayerHolds`) rests on
that switch alone.

2026-11-27, traced with `ragdoll_contacts`: the copy's bones were meeting the axe in its own hand (a group apart, see
`physics.md`, "Dragging a corpse"); fixed, its held contacts now match the host's own corpse's. Then measured with
`drag_rounds` / `drag_npc_corpse` (1.5 s back, pelvis travel): the dead guest's copy grabbed at once, before the fix
58, 5, 33 units, after it 33, 32, 68, all held; a host bandit's corpse at rest, the host's own 59, 100, 94 and the
guest's copy of it 49, 50, 105, all held. The two overlap at three rounds each; the earlier breaks at 1.3-1.8 s were
2 s drags.

Agent task: rounds of both drags (`drags_dead_player`, and the same on a host NPC's corpse) to tell chance from a
difference; if it is one, trace what else acts on the copy's bones while held (its movement, CopyRagdollPhysics).

Size: small. Needs: the duo.

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
- `guest-makes-character-then-joins`: "[client] the Body category is open" timed out (the race menu), then the host
  refused the guest ("has no character in the host's save and brings none") and the duo was booted again.
- `host-menu-loads-party`: "the guest loads the save from its store and rejoins" timed out after 120 s.
- `guest-steals-created-item` (scene, one run): "the party's bounty is {'host': {}, 'guest': {}}, expected at least 1".
- `object-rests-beside-orgnar` (scene, one run): "[guest] cheese has the host's rest" timed out after 2 s.
- `guest-rejoins-while-joined` and `guest_shows_only_hosts_npcs` (scenes): see [After a guest rejoins](#after-a-guest-rejoins).
- `guest-weather-own-region` (2026-11-23, every step passed): its end comparison found a Snow Fox (`107C2B`, Winterhold)
  at `weapon_state` 0 on the host and its copy on the guest at 3 (Drawn). The reverse of
  [A copy at Sheathed while its owner draws](#a-copy-at-sheathed-while-its-owner-draws); not read whether its copy
  missed a sheathe.
- `guest-mounts-bought-horse` (2026-11-23, every step passed): its end comparison found Ria (`1A6D8`, QASmoke) at
  `values.stamina_regen.delay` 1.17 on the host and 0.0 on its copy. A new readout (the stamina-regen gates, same
  day); not read whether a copy's delay should follow its owner's at all.

## Test tools

- **GameTests "A host NPC gets up from a knockdown" fails about 3 in 100** (2026-11-24): the copy's head ends 15 to 19
  units from its owner's (limit 15). Interleaved runs under one load: 13 of 440 on dev `c5265779`, 16 of 440 with
  `agent/actors-zoom-on-load`. Not traced.

### The gate fails on GameTests now and then

`scripts/check.sh` at a push, 2026-11-11 and 2026-11-12 on `agent/copy-idle-graph`: `check: FAILED (run GameTests)`, twice in
about twelve runs, passing on the next run with nothing changed. `GameTests` run alone passed 10 of 10 after the second one
(its assertion count differs run to run, 5627 to 5650: some flow is timing-dependent). The gate now keeps each shard's
log and prints the failed case (`check.sh`, `build/check/`), so the next one names its case.
2026-11-23 on `agent/fabien-crash-3` (pre-push): "A host NPC gets up from a knockdown: its copy's ragdoll follows its
owner's until both stand" (`physics_flows.cpp:887`): the copy's head ended 27.8 units from its owner's (limit 15); 10 of
10 passed alone right after.
2026-11-23 on `agent/kill-moves-every`: the same case, 15.7; looped alone with `RagdollStart`'s blend on and off it was
15.7 and 16.4 in 1 of 12 runs each, 5.5 to 11.4 in the rest: not the blend.
Looped alone again: 1 of 20 over 15 (42.4) with the blend, 8 of 30 (up to 16.4) with it off.
2026-11-24 on `agent/traps-synced`: the same case, 29.8 (`physics_flows.cpp:996`); 1 of 6 alone.

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
- 2026-11-22: `scripts/check.sh` passed 2 of 2 in this worktree (one full, `CHECK_FORCE=1`).

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

### A staging spot 1500 units off with no floor

`dialogue-nobody-moves` 2 of 2 (2026-11-22) stops at its staging: `place` puts the host's player 1500 units from the
guest at bearing 90 and finds "no clear spot". Both failure files report obstructions at 90 (a copy 832 units from the
spot), 230, 250 and 270 degrees, and no floor at the other bearings. `place` checks the sight line only after its floor
ray hits (`EngineCalls.cpp`), so the obstructed bearings have floor: no candidate passes every check, which is the
measured failure. Whether a conversation goes on with nobody moving has no running check meanwhile.

Agent task: stage the host where `place` finds a clear spot (a shorter distance, or a bearing clear of the copy), and
see what the scenario then shows.

Size: small. Needs: the duo.

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
