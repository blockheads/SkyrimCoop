# Open bugs

Every entry is a valid, open bug: none is accepted or shipped with. Each entry: what a player sees, the evidence, where to start, and the
test that shows it. Fixing one: a test in the fastest tier that can show it first (CLAUDE.md), then the fix, then
delete the entry and say where the evidence went. Unfinished work and unexplained behaviour stay in
`docs/progress.md` ("Loose ends"); ideas in `docs/ideas.md`. The fixes and regression evidence for feature 14's
visible actor bugs are in [small-bugs.md](design/small-bugs.md). Native bug fixes and regression evidence are in
[native-bugs.md](design/native-bugs.md). Fixes that landed after a playtest (S2's green flash, the
2026-11-01 dialogue fixes) keep a dated entry in `docs/progress.md`.

Each `###` section is one task for one agent: a paste-ready "Agent task", its size (small: a day or less; medium: a few
days), what it needs, and what it waits for. Delete a section when its last entry goes, and its index line with it.

Where the evidence comes from:

- **The full run of 2026-11-16:** 402 scenarios on dev (`0a2f618a`), 364 passed, then reruns; it closed entries whose
  scenarios now pass every run and added [its own section](#found-by-the-full-run-of-2026-11-16). Which entries went
  and why: progress.md, "Test suite", 2026-11-16.
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
  there too. The entries dated 2026-11-01 come from the S1 work (the 2026-10-31 playtest;
  `docs/design/physics-sync.md`, "Steered holds" and "Matches a game without co-op").

How the entries relate (shared causes, wrong checks, what to fix first): [bugs-map.md](bugs-map.md).

## Index

Each line gives a section's size. "waits" means another branch or plan comes first; "no duo" means no Skyrim is needed;
every other section needs the duo.

**Copy-desync follow-ups**

- [A seated owner's copy keeps an old action](#a-seated-owners-copy-keeps-an-old-action): small
- [After a guest rejoins](#after-a-guest-rejoins): small
- [A copy's interaction idle](#a-copys-interaction-idle): small
- [A copy's graph after a turn or a pick](#a-copys-graph-after-a-turn-or-a-pick): small
- [Headtracking on copies](#headtracking-on-copies): medium
- [Animation mods' random picks](#animation-mods-random-picks): small
- [Creature copies wobble](#creature-copies-wobble): small
- [A copy without its weapon](#a-copy-without-its-weapon): medium
- [End comparisons in towns](#end-comparisons-in-towns): medium
- [A strafing copy catches for a frame](#a-strafing-copy-catches-for-a-frame): small
- [Crossed kill moves: a short winner's blow lands first](#crossed-kill-moves-a-short-winners-blow-lands-first): small
- [After a kill move the guest's own body floats](#after-a-kill-move-the-guests-own-body-floats): small
- [Doors the host's NPCs unlock](#doors-the-hosts-npcs-unlock): medium
- [The Greybeards scene never casts the host's player](#the-greybeards-scene-never-casts-the-hosts-player): small
- [A frozen NPC a guest's kill move missed](#a-frozen-npc-a-guests-kill-move-missed): small

**Ragdolls and death poses**

- [Revive leaves the body wrong](#revive-leaves-the-body-wrong): medium
- [Copy ragdolls at rest](#copy-ragdolls-at-rest): medium

**Physics**

- [A chest lid's pose](#a-chest-lids-pose): small
- [Physics against vanilla](#physics-against-vanilla): medium

**Dialogue**

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
- [The guest's eye is its own estimate](#the-guests-eye-is-its-own-estimate): medium, a scenario first

**Combat**

- [After the host dies](#after-the-host-dies): medium, caused by the next one
- [A still guest unseen up close](#a-still-guest-unseen-up-close): small, minor
- [A guest's summon](#a-guests-summon): medium

**Found by the playthrough flow**

- [A looted corpse keeps items on the guest](#a-looted-corpse-keeps-items-on-the-guest): small
- [The host's copy after a door](#the-hosts-copy-after-a-door): small
- [Players stacked after a door](#players-stacked-after-a-door): small

**From the 2026-11-08 playtest**

- [A guest's crash loading a wipe's save](#a-guests-crash-loading-a-wipes-save): small
- [The sneak eye with the other player looking](#the-sneak-eye-with-the-other-player-looking): small, a repro first
- [A guest crash at the main menu](#a-guest-crash-at-the-main-menu): small, no duo

**Found by the full runs of 2026-11-11 and 2026-11-12**

- [The guest player's copy wears nothing](#the-guest-players-copy-wears-nothing): small
- [A starting kit with an extra item](#a-starting-kit-with-an-extra-item): small
- [A spawn's inventory over a newer equip](#a-spawns-inventory-over-a-newer-equip): small
- [The host shows a guest's relief box](#the-host-shows-a-guests-relief-box): small
- [A party wipe's fight that misfires](#a-party-wipes-fight-that-misfires): medium
- [An action sent before its copy exists](#an-action-sent-before-its-copy-exists): small
- [The green flash scenarios' grab](#the-green-flash-scenarios-grab): small
- [The guest's highlight with a still mouse](#the-guests-highlight-with-a-still-mouse): small
- [A refused join shows nothing](#a-refused-join-shows-nothing): small
- [A player at a chest reads lower than its copy](#a-player-at-a-chest-reads-lower-than-its-copy): small
- [A rider at a scenario's end](#a-rider-at-a-scenarios-end): small
- [Riverwood's scenery drifts apart](#riverwoods-scenery-drifts-apart): small
- [Whiterun plains' critters differ](#whiterun-plains-critters-differ): small
- [A guest crashed loading a copy's idle clips](#a-guest-crashed-loading-a-copys-idle-clips): small
- [The host's roll for a guest's loot](#the-hosts-roll-for-a-guests-loot): small
- [Cover plates after the host leaves](#cover-plates-after-the-host-leaves): small
- [A follower's party ahead of its copies](#a-followers-party-ahead-of-its-copies): small
- [Failures only deep in a long run](#failures-only-deep-in-a-long-run): medium
- [A character's base bytes after joining](#a-characters-base-bytes-after-joining): small

**Seen in the final review's duo runs (2026-11-12)**

- [A guest's console kill that never downs it](#a-guests-console-kill-that-never-downs-it): small, not read
- [A kill move that never pairs](#a-kill-move-that-never-pairs): small
- [A guest crash in the garbage collector's job](#a-guest-crash-in-the-garbage-collectors-job): medium
- [A dead player's copy a little off its owner's body](#a-dead-players-copy-a-little-off-its-owners-body): small
- [A respawned player's draw that does nothing](#a-respawned-players-draw-that-does-nothing): small
- [The attacker's copy not in its kill move](#the-attackers-copy-not-in-its-kill-move): small
- [A knocked-down copy placed 62 units off](#a-knocked-down-copy-placed-62-units-off): small
- [Sync points asked for over and over](#sync-points-asked-for-over-and-over): medium

**Found by the full run of 2026-11-16**

- [A revived guest's abilities](#a-revived-guests-abilities): small
- [The death scenario's wolf turns on the other player](#the-death-scenarios-wolf-turns-on-the-other-player): small
- [A guest that never arrives after a fast travel](#a-guest-that-never-arrives-after-a-fast-travel): small

**Test tools**

- [Heap history crashes the host at a kill move](#heap-history-crashes-the-host-at-a-kill-move): small
- [The gate fails on GameTests now and then](#the-gate-fails-on-gametests-now-and-then): small
- [A pass-through hook counted as ours](#a-pass-through-hook-counted-as-ours): small
- [A scenario stages its speaker frozen](#a-scenario-stages-its-speaker-frozen): small

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
  change (`AiStateEvents`, `seen`). So the order the spawns arrive in would decide it. 2026-11-18: the copy record
  (one-simulator.md, piece 1) applies a target when its copy appears; GameTests "A part naming an actor that spawns later"
  shows that order on the fake. Not shown on the duo yet: `rejoin_client` on dev's plugin left the guest at the main menu
  and its join from there found the host unreachable, so this path has no scenario.
- **A copy that stays off its owner** (2 of 2, Whiterun, 2026-11-09, `guest_shows_only_hosts_npcs` after its rejoin): a
  Skeleton (`109620`, high process, the same package in both) stood 81 units from its owner and turned 123 degrees from
  it through two reads 5 s apart, neither moving; 130 units and 130 degrees in the next run. Its spawn action is
  `RagdollInstant`: a body lying in its ragdoll. Also there: a copy looking at nobody, the bullet above. Only after a
  rejoin with a guest holding its own NPCs (copy-contract.md, "A guest's own NPCs"), where each NPC is hidden and gets
  its 3D again only after the host's spawn; the same scenario without the rejoin agreed on it. Not read: where a
  ragdolled copy's body settles when its 3D loads after its spawn placed it.
- **Also after a join with the host's save** (2 of 3): one hanging plant in the shop (`FLOR 38B0C`, `38BE6` or `38BE7`), its `b_Lower` bone
  turned differently. `released-copy-takes-up-its-ai` ends in the blank cell, so it does not fail on this.
- **A guest joining again does not wear what it wore** (`guest-swaps-armor-after-join`, 2026-11-10): `[client] the guest
  wears iron armor again` times out, on dev's plugin 3 runs in 4 and on the graph input stream's 2 in 3 and 2 in 4
  (with and without players on the stream). The first run of a session passes more often than the next ones. Not
  read.

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
  - A creature's copy 57-58 units off its owner in height, x and y within a unit, at the end of
    `wolf_copy_turns_with_owner` (2026-11-09: wolf `9B0DB` 58 below, deer `8480B` 57 above, one each in two runs). Their
    graphs matched their owners' at every update through those runs (the live check), so not the graph. Unread: what
    holds the copy's capsule at that height.
  - `copies_follow_owners_graphs_in_town` (Whiterun, 2026-11-09) ends with the same containers (`0x8000000` on the host
    only), loose food and a door, 45-52 references.
  - `copy_holds_furniture_prop` (Riverwood, 2026-11-16) passes its steps and ends with the fish (`10C3BA`, `10B035`), the
    containers (`0x8000000`) and Riverwood's mill log, 47 references.
  - `guest-trips-platform-guard` (Solitude, 2026-11-17, three runs) passes its steps and ends with townsfolk scattering
    from the interrupted execution 57-94 units off their owners (Lisette, Greta, Jala; Roggvir 66 off and facing 57 deg
    apart), and once the guest's player 40 units into the floor, owner and copy alike. Unread: whether the copies lag a
    run or hold a stale point.

### A strafing copy catches for a frame

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

### Crossed kill moves: a short winner's blow lands first

`kill-moves-cross` fails its end comparison when the first swing is `pa_KillMoveShortD` and the loser's clip does not
leave at once (2026-11-09, 1 run in 2 with the graph input stream's phase 3, whose off-stream action timing is dev's;
dev passed its one such draw in 6 runs).

Agent task: make the winner's pair start on the loser's game before the winner's hit can kill there.

Size: small. Needs: duo, then GameTests (`friendly_fire_flows.cpp`, the crossing flows).

- **Measured** (trace `20261109-231028-kill-moves-cross`): host swings at 117 ms, guest at 153 (`pa_KillMoveI`). The
  guest yields at 219, but its own clip goes on (`KillMoveStart` 235, `KillMoveEnd` 374: `pa_KillMoveI` sits behind an
  uninterruptible transition, kill-moves.md). The host's `HitFrame` comes at 501 and its forwarded hit kills the
  guest's player at 545, a plain death. The host's pair is tried on the guest at 605 and refused; the copy's last
  action stays `moveStart`.
- The stops are resent for 30 of the copy's updates while the player's `IsInKillMove` or `bIsSynced` holds
  (`AnimationSystem::Update`, `ReplayState::ResendPairStop`): 500 ms, longer than the short move's 384 ms to its hit.
  Not read: which of the two still held after `KillMoveEnd`.

### After a kill move the guest's own body floats

After `kill-moves-cross` or `spectator-watches-kill-move`, the guest's own third-person skeleton stands about 2,100
units above its feet wherever it next arrives, sinking about 20 units a second; its root and its copy on the host are
right. In first person the guest does not see it (its shadow may show it). Every scenario after it that compares the
guest's body then fails: `host-rides-horse` 14 runs of 14, "skeleton lifted off its feet (pelvis 2123 above them)".

Agent task: find what the kill move leaves on the guest's player that lifts its pelvis and head (the yielded or
watched pair's end, or its foot IK), starting with which bone above the root carries the offset.

Size: small. Needs: IDA, the duo, then GameTests.

- **Measured** (2026-11-10): from a fresh duo, `kill-moves-cross` then a trip to Riverwood: `pose.pelvis` 2138 on the
  guest's own player (72 standing), `pose.root` 0.5; the host's own player 72. Dev's plugin (the `devcheck`
  worktree's build of `2fcb5452`) the same: 2117 on `host-rides-horse` straight after it. So it is not the graph input stream's.
- In the cell the scenarios start from it reads low instead (21-40), and it lasts through a cog swap.
- None of `host-kill-move`, `client-kill-move`, `teleport-keeps-copies`, `copy-jumps-with-owner`, `sneak-*`,
  `dialogue-copy-looks-at-speaker` or the rides leaves it (one run each from a fresh duo).
- After `spectator-watches-kill-move` too (2026-11-11 and -12, merged here 2026-11-16): `host-rides-horse` straight
  after it 4 of 4 on dev's plugin `2fcb5452`, and on the stream's branch; it passes when it does not follow either.
- 2026-11-16, dev (`0a2f618a`): `kill-moves-cross` then `host-rides-horse` 2 of 2 (pelvis 2115-2146), and
  `spectator-watches-kill-move` then `host-rides-horse` 2 of 2 (2110-2122). `ragdoll-lies-as-host` right after each
  failed ride passed (2 of 2), so the lifted skeleton does not unsettle the next ragdoll (that entry, "A ragdoll that
  never settles after a failed ride", closed: 8 of 8 that day).

### Doors the host's NPCs unlock

Whiterun's doors open on the host, locked on the guest.

Agent task: Find what unlocks these doors on the host, and make that change reach the guests.

Size: medium. Needs: duo, IDA.

- **Three doors unlocked on the host stay locked on the guest** (2026-11-09, `guest_shows_only_hosts_npcs` after its
  steps, at noon). Doors of base `252C7` in Whiterun's exterior cell `1A278` (`1A6F5`, `252CD`, `24E25`); both games'
  have change flag `0x10000000`. First seen once a guest stopped running its own NPCs (copy-contract.md, "A guest's
  own NPCs"): the guest's NPC packages likely locked and unlocked them there before, hiding it.
  - Not read yet: whether package door locks do it (the lock-doors package flags), and whether the host sends a lock
    change its own NPC made (a guest proposes only its player's, `GuestWorldState::OnLockChange`; `HookLockChange`
    19512 raises `LockChangeEvent` for every lock change).
  - Test: that scenario's end comparison, or `world_state` on the three doors in both games.

### The Greybeards scene never casts the host's player

`greybeards-scene-casts-party` runs from its story save now (`duo --save LateAfterDragonRising`, from
`scripts/fetch_late_saves.py`; the save booted in 37 s on 2026-11-16) and fails at its first step: after `setstage
MQ105 270` on the host, "[host] a scene casts its player" timed out after 60 s, 1 of 1. The players stood at
(-389, 2731, 105) and (-282, 2731, 105), first person, no menu. Not read: whether the stage ran its fragment, and whether
the scene's actors (the Greybeards) are loaded where the party stands.

Agent task: read MQ105's stage 270 fragment and its scene's start conditions, then rerun with the party where the
scene plays.

Size: small. Needs: the duo with the late save.

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

Agent task: find why the killed guest's copy flickers where its owner does not in `kill-move-kills-guest`, from the
copy's `RagdollTrace` samples.

Size: medium. Needs: duo (`RagdollTrace`), then a fast test.

- **A killed guest's copy flickers and turns bodies back** (`kill-move-kills-guest`, the full run of 2026-11-16: "6
  flickers, its owner 0", body 9 turned back 4 times within a second, a bone up to 16 units off its body; samples
  `20261116-173724-kill-move-kills-guest`). The same shape was a third of the runs on 2026-11-12 and 3 of 23 with
  deaths on the stream ([A kill move that never pairs](#a-kill-move-that-never-pairs)).
  - Fixed 2026-11-11 and still holding: the copy now comes to rest (steered to its owner's rest for 30 physics steps,
    then placed and left, `BodyPrediction::StepToRest`); `ragdoll-lies-as-host` passed 4 of 4 on 2026-11-16, so its
    lying copy's bones agree now too.
  - Start: the flickers' frames in the samples against the copy's steering (`CopyRagdollPhysics`) before its rest.

## Physics

### A chest lid's pose

Stands alone: one node after an interrupted open.

Agent task: Replay the host's final open state so a chest's lid agrees after an interrupted open.

Size: small. Needs: duo, then GameTests.

- **A chest's lid can rest in another pose on the two games after its open is interrupted** (2026-10-28, live menus).
  The host saving while a guest's chest menu opens, or the chest disabled while open, leaves its `DoorLeft` node turned
  differently on the two games (the world check after `host-saves-with-guest-menu-open`, sometimes
  `guest-menus-close-when-target-goes`). The items and the open state agree; only the lid's pose differs.
  - Still there 2026-11-11: `C2CE8` (CONT `C2CDD`), `DoorLeft` and `DoorLeft:6`, left by
    `host-saves-with-guest-menu-open` in the full run; it passed alone on both builds.
  - Start: compare the lid's animation state on both games after the interruption (the chest's `BGSOpenCloseForm`
    open state, `SetOpenState` 14287 in form-state.md) and replay the host's final open state.

### Physics against vanilla

What the vanilla baseline still fails on (2026-11-15, `docs/design/physics-sync.md`, "One simulator per pile").

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
- **2026-11-16, dev's full run:** 5 of the 8 passed; `guest-carries-into-pile-like-vanilla` (a rest 3.4 units apart),
  `host-carries-into-pile-like-vanilla` (staging timed out) and `host-push-mixed-like-vanilla` (a pot turning 7 and 11
  times where vanilla turns up to 6), each a shape below.
- **Staging times out now and then** (`stage_objects`, "[host] ... rests on the lane"): a staged object rests, then
  moves again with nobody near, past the 20 s wait. Seen before and after the pile changes; not traced.
- **The guest crashed once in the HUD** (`FloatingQuestMarker::Update` 51676 into `GFxValue::GetDisplayInfo`, read
  of 0xFFFFFFFFFFFFFFFF, 2026-11-15 18:54, log in `build/coop-test/kept/`), during a carry trial; not traced,
  maybe not physics. The host crashed the same way at 20:48 (HUD compass update 51668 into GFx 83250/87834, same
  bad read, 28 minutes up; `kept/host-crash-2026-11-15-20-48-12.log`).
- **The guest's game hung once in the graphics present call** (d3d11/dxgi under `Present`, HangWatchdog, 2026-11-15
  19:57); not ours as far as the stack shows.
- **The harness's `world_state` crashed the host once** (access violation in `EngineCalls.cpp` `ReadPose`, walking an
  object's 3D), on the first call after staging; not seen again.

## Dialogue

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
  - Still the only failure of `vampirism-is-each-players` (2026-11-11, every run on both builds): "created STAT 3B at
    (-165, 2038, 6976): present client only".

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
    same addresses (`+0x3cdb45 > +0x182241`).
  - 2026-11-16, dev: `party-loads-overlap` and `party-fast-travels` passed their lock audits 3 of 3. The same lock pair
    showed in `party-buys-house-guest`'s audit on the host instead (full run, our code on the stack): `+0x187a10 >
    +0x165490` against `+0x172a5b > +0x187881`. Read in IDA, each named that day:
    `ExtraAliasInstanceArray::FindAliasPackageThatCanRun` (12721, the alias array's lock for read) asks
    `TESPackage::CanRunFor` (29619) of each alias package, which for a once-per-day package looks in the reference's
    run-once packs under the extra-data list's lock (`ExtraDataList::HasRunOncePack` 11737, `0x140165460`); the other thread is
    `ExtraDataList::RemoveAliasInstance` (12047: the list's lock for write, then 12719 the array's). So the pair is an
    alias leaving a reference while that reference's packages are judged. Its `[ours]` was wrong: see
    [A pass-through hook counted as ours](#a-pass-through-hook-counted-as-ours).

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

Three ladles in the baseline cell differ between the games: on vanilla too, in about one flow in fifty.

Agent task: Find what moves or flags the three ladles (`32AFE`, `32AFF`, `32B00`) on one game only, starting from a run
that just reported one (`unsynced`, `save-diff`).

Size: small. Needs: duo.

- **Movement-record flags on guest clutter after a vampire rejoin** (B8, 2026-10-29).
  With all six fix mods, `vampirism-kept-across-rejoin` passes its behavior checks, then the world check finds only
  movement-record change flags differing on three clutter references (`32AFF`, `32AFE`, `32B00`, base `319E5`), set
  only on the guest; positions agree within the check's tolerance. Once, not seen on vanilla (2026-10-29: the scenario
  passes with no world difference and `unsynced` reports every host change sent with a row). A guest's game refuses
  every save, so its records never persist. Evidence: [mod-compatibility.md](mod-compatibility.md),
  [lock-history follow-up](re/crashes/2026-10-29-lock-history-and-button-pose.md).
  - Start: `coop_test.py unsynced` and `save-diff` right after the scenario on the six-mod profile; if the host
    lacks a move the guest has, which engine path moved the clutter on the guest only.
- **The same three references on vanilla, with no rejoin and no vampire** (2026-11-11, both builds). They are wooden
  ladles (`WoodenLadle01`, MISC `319E5`) near (2623, 1985, 7043). The world check found, on one of them or all three:
  - the rotation differing by 0.02 to 0.3 rad (`host-enchantment-knowledge-control`, `host-quickloads-party`,
    `party-travel-mounted-guest` on the branch; `party-buys-house-guest` on dev's plugin);
  - change bit `0x2` on the guest only, on all three (`late-join-gets-calendar`);
  - the pose and the collision body turned differently (`guest-murder-party-alarm`).
  Six of about 500 flows run that day; each passed when rerun. Again 2026-11-16 on dev: change bit `0x2` on the guest
  only on all three (`late-join-gets-calendar`), and `32AFE` 5 units off with bit `0x2`
  (`death-wipe-twice-keeps-character`), 2 of 402 flows. No scenario touches them, so something in the cell
  pushes them (a player or a copy walking through, or a body settling) and the two games' physics disagree.
  - Start: which game moved it and when: `body_trace` on `32AFE` through a scenario that staged players beside it.

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
  - Since 2026-11-13 (later the same day) a guest's seen theft is warned like the host's (`crime.md`, "The steal
    warning"); a warning held off by an NPC's scene still reports the theft. These were not rerun since.
  - Matters because a guest's alarm stays on the host's player so that the arrest works (`crime.md`): that is read from
    the code, not run.
  - Full run and reruns of 2026-11-11, dev's plugin and `agent/copy-idle-graph` alike:
    - "the host is unseen in the guest theft control": `guest-lockpick-party-alarm` every run on both;
      `guest-theft-host-witnesses` and `guest-murder-party-alarm` 2 of 4 on the branch, passed once on dev.
    - "the fine confiscates stolen rolls and keeps the unowned one": `guest-container-theft-and-fine`, alone on both
      (in the full run it stopped on the unseen control first).
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

### The guest's eye is its own estimate

On the guest, the stealth eye shows what its NPC copies judge themselves, not what the host's NPCs see.

Agent task: Show with a scenario that the guest's eye and the host's NPCs disagree in a way a player feels; only then
make the guest's eye show the host's detection.

Size: medium. Needs: duo, then a decision.

- **NPC copies judge the guest's player in the guest's game** (2026-11-08, found with the sneak eye fix,
  `docs/re/systems/stealth.md`). The eye (40593 through 41408) counts every high-process actor's state of the player,
  and a copy's detection pass still runs: Lydia's copy read -21, fresh, while the guest sneaked in QASmoke. On 2026-11-01
  the host's eye read Lydia's real 30 and the guest's its copy's 20 (the 2026-10-31 playtest, S6): an estimate,
  not stuck.
  - The host's side is no better yet: nothing keeps the host's NPCs' state of a guest's copy fresh (`crime.md`, Hulda 4.7 s
    old), and [A still guest unseen up close](#a-still-guest-unseen-up-close) reads no line of sight to it.
  - Shape if it matters: the host asks its NPCs about each guest's copy and sends the highest level (a message, at a rate:
    detection has no event); the guest's eye shows it, and NPC copies become unseen witnesses (`DetectionRules`).
  - Test: a scenario where the guest's eye says hidden while a host NPC spots the guest (combat, a crime reported), or
    the reverse. With none, this entry goes.

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

### The sneak eye with the other player looking

S6 of 2026-10-31 again. Hardened (PR #40), not reproduced.

Agent task: Catch the eye open with the other player looking, name its source with the readout, and fix that source.

Size: small. Needs: duo, the user's report.

- **The other player looking counts as being seen** (2026-11-08, the user: "friends when looking at you detect you";
  "just standing next to another player if we're sneaking and they are looking at us"). Not reproduced on the duo
  (2026-11-08/09, dev's hook): standing or sneaking face to face, both games, the eye stayed closed on screen; the
  copy's knowledge held no state; talking to or pickpocketing a player's copy is refused. The playtest's 21:26 `/bug`
  reports caught no sneaking moment (Riverwood Trader, Lucan and Camilla by).
  - Hardened (`docs/re/systems/stealth.md`): a player's copy now carries `kDoNotShowOnStealthMeter` (41408 skips it),
    and `HookUpdateDetectionState` installs an unseen pass instead of skipping (a skip left the request queued; 42702
    then answered an older level at any age, and 37765's forced 100 through 42705 the same).
  - Suspects left: a real NPC near (the Sleeping Giant: Orgnar read -5 and "seen" on a sneaking host), NPC copies on the
    guest ([The guest's eye is its own estimate](#the-guests-eye-is-its-own-estimate)), combat (40593 shows 100 minus
    the search level).
  - Start: the user's conditions (whose eye, where, who was near), or a `/bug` taken while the eye shows detected; the
    harness's `crime stealth_meter` lists each actor holding a level of the player (`sources`).
  - Test: scenario `other-player-looking-leaves-eye-hidden` is the base; it must fail before a fix on what the readout
    names.

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

### The guest player's copy wears nothing

`dialogue-picked-text-visual` (2 of 3) and `quest-gold-turn-in-guest` (1 of 3), 2026-11-11: at the end, the host's copy of
the guest's player has no equipment (`[]`, owner `[77387, 77385, 77382, 77389, 79760]`) and empty hands. The same shape
was in `dialogue-options-are-the-partys` on 2026-10-30 and 2026-10-31 (the copy kept one item of five), so it predates the
graph input stream. Unread: whether the copy was spawned again during the scenario, and what applies its worn set after.

Agent task: read what gives a re-spawned player copy its owner's worn set, then make a GameTests flow for it.

Size: small. Needs: the duo (`dialogue-picked-text-visual --repeat 4`), then GameTests.

### A starting kit with an extra item

`guest-makes-character-then-joins` (1 of 3, 2026-11-11): `[client] the starting kit changed`, with `F11B6` beside the
two expected items. First seen in this run. Unread: what `F11B6` is and which side added it.

Agent task: read where `F11B6` came from, in the guest's log and the host's.

Size: small. Needs: the duo.

### A spawn's inventory over a newer equip

Stands alone: one copy's worn set.

Agent task: Make a copy keep an equip its owner sent after the spawn's inventory was read.

Size: small. Needs: GameTests, then the duo.

- **A fresh copy loses what its owner equipped right after spawning** (`host-quickloads-party`: branch 3 of 4, dev 2
  of 4, 2026-11-11). The host places Aela; her game equips her bow 50 ms later and sends the equipment change. On the
  guest the change lands first ("equip 1D4EC: ours", "Equipment reconcile: reset worn set"), then the spawn's
  inventory, read before the bow, is applied when the copy's 3D is ready ("Setting inventory for actor") and unequips
  it: the copy stands with an empty hand until the next change.
  - Start: `CharacterService`'s `WaitingFor3D` inventory against `InventoryService`'s equipment changes for the same
    copy: the older one must not win.
  - Test: a GameTests flow, an NPC that equips one frame after it is announced, the copy's 3D ready later.
  - Also `host-loads-party` (branch 1 of 4, dev 1 of 4) and `host-menu-loads-party` (branch 2 of 4, dev 0 of 4),
    2026-11-12: Aela's copy lacks `1D4EC` and its right hand is empty, the same shape.
  - 2026-11-16, dev's full run: `host-quickloads-party` and `save-load-gate`, Aela's copy lacking `1D4EC` both times.

### The host shows a guest's relief box

`relief-takes-client-relics` (branch 3 of 4, dev's plugin 2 of 4, 2026-11-12; on dev 2026-11-16 2 of 3 reruns, after
passing in the full run): the guest puts the axe in Volunruud's
relief and the box saying the sword is missing shows on the host's game too (`[host] shows the client's box`). Once on
dev it failed before that, with no menu cursor on the host. Unread: which stack's `Message.Show` reached the host's
screen (party-calls.md: a guest's action's message is its own).

Agent task: read where the door script's `Message.Show` runs for a guest's activation, then a GameTests flow.

Size: small. Needs: the duo (`relief-takes-client-relics --repeat 4`), then GameTests.

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
    (`HostCharacters::OnMovement`), so a spawn built from the record (no host copy, a respawn) plays the latest;
    HostAuthorityTests "a player's latest relayed action". Not yet seen on the duo whether the travels' copies come
    from the record or from the host's copy (`ReadCopy`, its own action, the one behind).
  - Start: which spawn the host's new copy came from after the travel: the record, or an earlier one in flight.

### The green flash scenarios' grab

Stands alone: one scenario stops before its point.

Agent task: find why the mouse sweep in `green-flash-ivarstead-wine` drops the wine before the contact probe is done.

Size: small. Needs: the duo.

- **`green-flash-ivarstead-wine` stops on its grab** (2026-11-16: 3 of 4, the full run and two of three reruns): "the
  mouse sweep dropped the prop before the contact probe completed". Its neighbours pass now (`-handoff` and `-guest` 4
  of 4, the observer scenarios), so the S2 green-flash fix has a running check; this one is the wine's grab alone.
  - Start: the game's grab readout at the failure (`at-failure.json`) against the scenario's sweep.

### The guest's highlight with a still mouse

Stands alone: one dialogue check.

Agent task: Find why the guest's highlight stops following the host's in `dialogue-mouse-highlight-stays-shared`.

Size: small. Needs: the duo.

- **The guest's dialogue highlight does not follow the host's while the guest's mouse rests on the list**
  (2026-11-11, every run on both builds): "the guest follows the host while its mouse stays still" times out after 5 s.
  Not read further: whether the guest's still mouse re-highlights its own row each frame, or the host's move never
  arrives.
  - Start: the guest's SWF `cursor_index` and the dialogue log through the host's move.

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

### A player at a chest reads lower than its copy

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

Left of it (2026-11-17, `agent/horse-riding`): the horse-buying scenarios at Whiterun's stables. The rest is fixed:
the crouched rider ("pelvis 0") was the scenario getting off within ~0.1 s of being seated, which the engine answers
with no exit clip (vanilla too, `docs/re/systems/mounts.md`); a copy left seated by such a ride's end now gets off.

Agent task: Find why the stables' bought horse can't be got off or aimed at, and make `party-buys-horse-host/guest`
pass.

Size: small. Needs: the duo.

- **The host can't get off the guest's bought horse** (`party-buys-horse-guest`, 3 of 3 runs 2026-11-17): seated
  (sit state 3, the horse's pair over), Activate pressed with the crosshair clear: no dismount package, no Detach,
  still riding 15 s later. The same horse unbought, or given the purchase's owner and faction by console, lets the
  host off at once. Not read: what the purchase (StablesScript, its packages 98454/3EAB9) leaves on the horse;
  `Character::ActivateMount` (37864) takes a ClearMountPosition package instead of Dismount when the mount lacks
  flags2 bit 4.
- **The guest's crosshair misses the host's bought horse** (`party-buys-horse-host`, 4 of 4): "the crosshair is on
  nothing, not 429428: 123-138 units off", the aim at its body too. The unbought horse, same stall, is aimed at and
  ridden.
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

### Whiterun plains' critters differ

Stands alone: `party-shares-a-horse` (Whiterun plains, `cow Tamriel 0 -4`).

Agent task: Find why the critters the plains spawn differ between the games, and make the scenario's end check pass.

Size: small. Needs: the duo.

- 2026-11-17, 2 of 3 runs: every step passes, then the end comparison fails only on created references nobody touched:
  `ACTI B97AF` / `ACTI 22219` (flag `0x8000000` on the host's alone, graph `fTakeOffDamped` set on the host's, 0 on
  the guest's) and `STAT C2D47` present on the guest only. The third run passed clean.
  - Start: read which game spawns these (a critter spawner's script, created references) and whether HostWorldState
    sends them; `Riverwood's scenery drifts apart` below may share the answer.

### Riverwood's scenery drifts apart

Stands alone: every long scenario in Riverwood.

Agent task: Find why Riverwood's fish, mill logs and loose items end up in different places on the two games, and
make the horse recordings' end check pass there.

Size: small. Needs: the duo.

- The recorded horse scenarios (`horse-passes-between-riders`, `two-players-race-for-horse`,
  `rejoined-guest-sees-rider`; 2026-11-17) pass every step, then fail the end comparison on one or two of: a
  FishController's position (ACTI 10C3BA/10B035, the river's fish), the mill's log (FURN 71C47), cabbages or ingots
  turned differently. With the clock pinned (`scene(pin=True)`) many more drift, the mill and every fish. None was
  touched by the scenario; the short `mount_roundtrip` scenarios in the same cell pass.
  - Start: `scene` now baselines the world differences already there; these appear during the run. Read which of
    them the host sends (HostWorldState) and which each game simulates alone.

### The host's roll for a guest's loot

Stands alone: one loot scenario.

Agent task: Find why the guest's container never shows the host's leveled roll in `guest-loots-hosts-roll`.

Size: small. Needs: the duo, then GameTests.

- **The guest does not get the host's roll** (2026-11-11, every run on both builds): "[client] the guest gets the
  host's roll {...}" times out after 10 s, the host's roll different each run (so the host does roll). What the guest
  holds instead is not in the message: its own roll, or nothing.
  - Start: the guest's container contents at the timeout against the host's; then where a container's leveled items
    are resolved on a guest (`loot` in `docs/design/`).
  - Test: a GameTests flow, a leveled container opened by the guest first.

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

### Failures only deep in a long run

Each passed alone on both builds: something an earlier scenario leaves behind, or a rare race.

Agent task: Rerun each with the scenarios that ran before it in the full run (`build/coop-test/results/` keeps the
order) and file or fix what it finds.

Size: medium. Needs: the duo.

Each failed once in the 388-scenario run of 2026-11-11 and passed alone on dev's plugin and on the branch:

- **`party-aggro-host-first`** (the first scenario of the run): "the enemy flipped away from the higher-damage player:
  20". Again first in the run of 2026-11-12, then branch 2 of 4 and dev's plugin 1 of 4 alone (on dev "[host] host's
  first hit lands" timed out): it fails alone too. 2026-11-16: first in the run again (the same message), then 1 of 3
  alone ("the enemy initially targets the first attacker (host)" timed out).
- **`dialogue-refused-pick-restores-list`:** "[client] its chat shows the host's refusal". Again 2026-11-16, the
  run's 25th scenario, so not only deep in a run; 3 of 3 alone after.
- **`guest-changes-looks-in-session`:** "the vampire change replaced the existing copy".
- **`furniture-host-player`:** the guest's player `pose.head` 99.6 against its copy's 124.1 afterwards: the same shape
  as [the chest's](#a-player-at-a-chest-reads-lower-than-its-copy). Again in the full run of 2026-11-16 (100 against
  124), 3 of 3 alone after.
- **`host-kill-move`:** "[client] wolf's graph sends KillActor" timed out after 5 s.
- **`dialogue-guest-barter-earns-speech` and `dialogue-guest-placed-barter-reopens`,** one after the other: "the
  merchant offers its barter line" timed out after 30 s.
- **`dialogue-two-starts-at-once`:** "[client] FF000D73 is between the player and andurs": an actor some earlier
  scenario left in the cell.
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

### A kill move that never pairs

`kill-move-kills-guest`: `Timed out after 3s: [host] the kill move pairs the host with the guest's copy`, 3 of about 16
runs on builds with the review's edits (once with the player left 100 units off its baseline, so the duo booted again),
then none in 40 on `1a4a44a4` and none in 40 on `0492014e` (2026-11-12). Chance, or the wrong check those builds carried.
The same 80 runs passed 27 of 40 and 24 of 40: the scenario failed a third of the time on both, always in the dead copy's
ragdoll (flicker, `NEVER came to rest`). With deaths on the stream (2026-11-12): 20 of 23, three flickers of 2 to 5. The same scenario's other intermittents (flicker, `NEVER came to rest`) are under
[Copy ragdolls at rest](#copy-ragdolls-at-rest).

Agent task: run `kill-move-kills-guest --repeat 20` on `1a4a44a4` and on the current code, then read why the pair did not
start (`HookTryMoveTo`, the instance, `PairOnStream`).

Size: small. Needs: the duo, IDA.

### A guest crash in the garbage collector's job

`guest-rides-across-border`, once (2026-11-12), the guest: RIP 0 from `coop_GarbageCollector_DeleteNextForm` (36479) on a
`Job_Garbage` thread, a queued entry whose deleting destructor was null. It followed a failed `host-rides-horse` (the rider
2100 units off its feet) in the same session. Not investigated: `docs/re/crashes/2026-11-12-guest-gc-deletes-null-vtable.md`.

Agent task: reproduce with the heap history on (`coop_test.py repro`), running the five scenarios the crash note names.

Size: medium. Needs: the duo, IDA.

### A dead player's copy a little off its owner's body

`death-spectate-host`, 4 of 46 runs with deaths on the stream (2026-11-12), none in 8 on the build before it, then none in
25 more: `[client] the copy does not lie as the host's body`, once 13 to 16 units and about 30 degrees off, the other
times within a unit but turned 3 to 14 degrees, over the check's 0.05 radians.

Measured (`RagdollTrace` on the dead player's copy, now part of the scenario): the copy's bodies reach the owner's rest
within 0.2 to 0.8 units and fall asleep at about the moment the owner's do, then wake again: sleep for a frame, awake
about half a second, sleep, in 11 of 13 traces, the owner's staying asleep. One wake lines up with the owner's rest
arriving (steering wakes a body each step, `StepToRest`), the next with its place step 30 steps later. Woken, they settle
again a few degrees off. Tried: leaving a copy whose every body is already asleep near its rest; the condition never
held in 13 runs (the reason not found: its angle, or what `BodyRead::Active` means), so it was taken out.

Agent task: log, at the owner's rest's arrival, each of the copy's bodies' awake flag, distance and angle from the rest,
and read why "asleep and there" never holds; then leave such a copy alone.

Size: small. Needs: the duo.

### A respawned player's draw that does nothing

`death-spectate-host` (`[host] hands ready`) and `client-melee-kill-credit` right after `friendly-fire-kill`
(`[client] hands ready`), once each in 58 runs (2026-11-12): the just respawned player presses Ready Weapon and no draw
follows in 5 s. The host's log has the press 0.65 s after the respawn, the player still falling into place (`JumpFall`,
`SwimStart`, `WeapEquip` refused by the engine). Not seen in the session's earlier runs, and in none of 34 more runs after
(24 with deaths on the stream, 8 without).

Agent task: make the respawn step wait for the player to stand (its fall over) before it presses, then see what is left.

Size: small. Needs: the duo.

### The attacker's copy not in its kill move

`host-kill-move`: `Timed out after 3s: [client] player's kill_move is True`, once in 14 runs on the build that records
through every pair (2026-11-12); not seen in about 20 runs before it. On its stream the attacker's copy is put in the kill
move by `AnimationSystem` at its owner's `pa_Kill` action (`ReplayState::EntersKillMove`); a copy off its stream at that
moment plays the action instead. Which path missed: not read.

Agent task: log which branch took the `pa_Kill` action for the attacker's copy, then `host-kill-move --repeat 30`.

Size: small. Needs: the duo.

### A knocked-down copy placed 62 units off

`knockdown-lies-as-host`: `desync before removing FF00099A: Lydia placement.position: host [-987, 2095, 6976] vs client
copy [-987, 2094, 7037]` once (62 units up) in about 25 runs. Also once `the copy 250 ms behind its owner; turned away from
its owner's facing once, 31 degrees for 33 ms`, and `bone 8 / bone 29 off` shapes (the get-up entry, closed 2026-11-16). Rate
over the day: 3 of 5 and 0 of 3 only while a wrong check refused sync points (removed), 6 of 6 and 3 of 3 once it was out.

Agent task: read what places a copy that stands up 62 units above the floor (its capsule against the bodies' last frame).

Size: small. Needs: IDA, the duo.

### Sync points asked for over and over

Whole-session logs of the duo show some copies starting on a new sync point about every sixth update for long stretches: one
actor 1189 times on the host, one hare 384 on the guest, against 69 and 127 restores logged in the same sessions. In the
last three-scenario run, starts and restores matched (4 and 4, 32 and 32). Which scenario loops, and whether the asks, the
points or the logging is what repeats: not found. It costs messages and a graph restore each time, so it is the first
thing to look at when the host is profiled in a crowded cell (graph-input-stream.md, "The final review").

Agent task: log why `ShouldAsk` fires again for one looping actor (`Asked` reset by `Feed` on a gap, `Replay` on a failed
load, `SeeGraph`), in the scenario that shows it.

Size: medium. Needs: the duo, GameTests.

## Found with the dev commands (2026-11-16)

### A frozen NPC a guest's kill move missed

The duo, QASmoke, by hand: two wolves staged frozen (`stage`), the guest in daedric gear hits one (`fight`). The guest's
idle tree picks `pa_KillMove1HMWolfB` on the fatal swing, its engine refuses the pair ("refused by the engine, not
sent"), and from then on the host refuses every forwarded hit on that wolf ("Forwarded hit on FF00087F by FF000838 not
applied (gone, or held by a kill move)") for over 50 s; the host's copy of the guest logs "paired action 13005
'pa_KillMove1HMWolfB' not played, FF00087F was in a kill move". The same at vanilla odds (`/killmove default`) with a
third wolf. `kill_move_check` on the host: `in_kill_move` false. Unread: what holds the host's wolf (its kill move flag
or a recorded killer, 38613/38614), and whether an unfrozen NPC does it too.

Agent task: read what `IsHeldByKillMove` answers for the host's wolf after the guest's refused pair, then a GameTests
flow for it.

Size: small. Needs: the duo (two frozen wolves, the guest's fatal swing), then GameTests.

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

### A guest that never arrives after a fast travel

`party-fast-travels`, 1 of 5 runs (the full run; `20261116-172932-1b1cf2`, `-172936-b39f96`): "[client] arrived in
QASmoke" timed out after 60 s, and the cleanup then got no answer from the guest's game (`{'cmd': 'dialogue'}
refused: no answer`), so the duo was booted again. It passed 3 of 3 after; its lock audit passed each time (see
[The AI/quest lock pair](#the-aiquest-lock-pair)). Not read: the guest's log through the travel.

Agent task: read the guest's log from the travel to the timeout: a load that never ended, or a hang.

Size: small. Needs: the evidence above, then the duo (`party-fast-travels --repeat 8`).

## Test tools

### Heap history crashes the host at a kill move

With `SKYRIMCOOP_HEAP_HISTORY` set on a duo boot (2026-11-12), the host crashed 4 of 4 at the start of
`spectator-watches-kill-move`, inside the tool: `HeapHistory::Capture` (its `RtlCaptureStackBackTrace`) under
`HookFormAllocate`, an access violation in ntdll's unwinder. Read, not measured: Wine's unwinder faults on a frame with no
unwind data where Windows' stops (a hook's trampoline is one). The other four scenarios of that run passed under it.

Agent task: make the capture stop at a frame with no unwind data (`RtlLookupFunctionEntry` first, or a guarded walk),
then use it on "A guest crash in the garbage collector's job".

Size: small. Needs: the duo.

### The gate fails on GameTests now and then

`scripts/check.sh` at a push, 2026-11-11 and 2026-11-12 on `agent/copy-idle-graph`: `check: FAILED (run GameTests)`, twice in
about twelve runs, passing on the next run with nothing changed. `GameTests` run alone passed 10 of 10 after the second one
(its assertion count differs run to run, 5627 to 5650: some flow is timing-dependent). The gate now keeps each shard's
log and prints the failed case (`check.sh`, `build/check/`), so the next one names its case.

Agent task: loop `GameTests` until it fails and fix that case's timing.

Size: small. Needs: nothing but the fast suite.

### A pass-through hook counted as ours

The lock-order checker tagged an engine-only inversion `[ours]` and failed `party-buys-house-guest` (the full run of
2026-11-16; [The AI/quest lock pair](#the-aiquest-lock-pair)). Our frames on its two stacks (symbolized from the
host's `tp_client.2.log`, 18:25:09) are `HookCheckForNewPackage` (+0x5d), `HookUpdateAI` (+0x59) and
`AliasOnReference<RealClearRefAlias>` (+0x91), all pass-through hooks `LockOrder::IgnoreFrames` lists. But
`HookUpdateAI` is listed with 0x50 bytes (`Actor.cpp`), and its call returns at +0x59: the hook has grown past its size,
so its frame counts as ours.

Agent task: give `HookUpdateAI` a size that covers it, and make the checker fail loudly when a listed hook's code is
longer than its size (or measure the size instead of writing it).

Size: small. Needs: a build; the duo to confirm (`party-buys-house-guest`).

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

## A passenger jumps 57 units as it takes the reins (duo, 2026-11-18)

- `party-shares-a-horse`, "the host gets off": the guest's own player, seated as a passenger, moved 57 units in one
  frame (19 ms) 0.84 s into the host's get-off, as the horse was handed to it. 1 of 3 runs; the other two smooth.
- Not the rider-off message: it reached the guest after the host's copy was already off (`Unboard ...: its copy on 0,
  sit state 4`) in every logged run.
- Start: trace the guest's player and horse around `PlayerTakeReins` (`EndMountPair`, `HoldRider`, 40725,
  `EvaluatePackage` 37401) with `riders_smooth`'s body trace; read what 40725 / the mount package do to a seated rider.
