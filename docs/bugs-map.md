# How the open bugs relate

A map of [bugs.md](bugs.md) from 2026-11-06: which entries share a cause, a code path or a fix, and which only share a
word. bugs.md stays the list; this says what to fix together and in what order.

How sure each link is:

- **read**: followed in IDA or in our code.
- **measured**: seen on the duo, with the run named.
- **inferred**: fits the evidence, nobody has shown it.

## The map

```
 CHECKS THAT REPORT WHAT DID NOT HAPPEN        REAL COPY BUGS BEHIND THEM
 --------------------------------------        --------------------------
 Drift detector, fixed: reads live ----------> Owner cancels an idle, copy plays it
   entries deleted 11-06                         "interaction idle"  (real, reworded)
                                                 "after a turn or a pick" (new)
 ai_state.head_track, fixed -----------------> a player's copy's look target
   dead actors, self slots (not read)
 town end comparison ------------------------> copy lifecycle (below)

 HANDS LIVE IN THE PROCESS, WORN MARKS IN THE INVENTORY
 ------------------------------------------------------
   a move or a rebuilt process can change either, and they are checked apart
     |-- hand spells on a copy     fixed 11-04, held 11-07
     |-- Revive's hands            fixed
     `-- a copy without its weapon OPEN: seen by the user, not reproduced

 COPY LIFECYCLE: an owner's actor unloads, moves or is removed
 -------------------------------------------------------------
   fixed 11-06: rabbit / Olava with no owner, placed guard's trip home, deleted NPC crash
   Sigurd marked own -------- open, cause not shown

 CREATED FORMS HAVE NO SESSION ID  (ToSession, FormIds.h)
 --------------------------------------------------------
   guest theft of a created item -- fixed 11-07: not this cause (owner actor)
   head_track on FF000820 --------- same call, AiStateService
   references a guest's script creates -- goes the way a drop does
   a guest's summon --------------------- same path (measured broken)

 REVIVE LEAVES THE BODY WRONG          SEPARATE, ONLY THE WORD "RAGDOLL"
 ----------------------------          ---------------------------------
   stays ragdolled                     copy ragdolls at rest (BodyService)
   kneels in its next conversation     get-up 5->6 handoff
   knocked down later, stuck in 6      a wolf's slow rest (host's own body)
```

## Clusters

### A. The drift detector compared with a frozen snapshot

**Fixed 2026-11-06:** the check reads owner and copy live (animation-replay-recovery.md, "Drift detector"). The entries
"A copy walks while its owner turns" and "A copy's idle variant" are deleted: the evidence is here. Measured after the fix:
Camilla's `npc-counter-matches`, 1144 samples over two runs, no run of 2 s; no owner turning against a walking copy in the second run's 30 flows (the reverse, a wolf, is in the
entry below). Left: [A copy's interaction idle](bugs.md#a-copys-interaction-idle),
[A copy's graph after a turn or a pick](bugs.md#a-copys-graph-after-a-turn-or-a-pick).

Cause, read: `PerformAction` (38949) sends its event through `NotifyAnimationGraph` (32901, 63362, 63591) to
`hkbBehaviorGraph::HandleEventOnActiveNodes` (58377), which updates active nodes at once (60079). So the owner's
snapshot is taken mid-transition: it holds the new states and the outgoing state's machines still blending
(`SnapshotGraphState` 65148 writes a machine mid-transition). The detector then measures the copy against that snapshot
until the next action, though the owner has moved on.

Measured, 2026-11-06, Riverwood, new verb `graph_watch` (owner and copy read live, 3 to 6 reads per actor per second, so
a 2 s run cannot be missed):

- 75 s, 10 actors, 2570 samples: no run of 2 s apart, while the detector called `105A25` apart for 45 s.
- 120 s, 6 actors, 4338 samples: the detector reported `1348A` for 44 s and `1348B` for 37 s; live, neither was ever apart.
  It reported the crab `8CACD` after `moveStart` for 3.4 s; live, the crab's only run was 2.0 s on a mandible idle.
- The crab's report is the "walks while its owner turns" shape: the owner's list keeps `StandingBehavior=TurnRight`
  beside `LocomotionState`. **That entry is a wrong check:** measured on the crab. The dog and wolf of the entry have the
  same log shape, and were not reported while being read live.
- Sigrid `13483` is really apart, 3.9 to 8 s a time, though not every report of hers is: 4 in that window, 2 live runs.

Sigrid, measured (trace `20261106-093521-mcp-sigrid`, t=41): the owner takes `IdlePickup_Ground` and drops it within
about 60 ms, with no `PickUp` event; the copy plays the whole clip (`PickUp` at 42.7, `IdleStop` at 44.2). Where the
owner's clip plays (t=52), the games agree. The live run's time fits t=41 (the verb's clock is not the trace's, so read as a fit, not a match).
**So the interaction idle entry has the direction backwards:** the copy
plays an idle its owner cancelled. What cancels it on the owner is not read.

Ruled out, measured: the graph recovery as a cause. Only 2 of 53 lasting reports start at a recovery.

Camilla's `Behavior02=MT_Idle.HKX` is the same shape in the log and was not sampled live: inferred a wrong check.

### B. A copy's hands

Entry: [A copy without its weapon](bugs.md#a-copy-without-its-weapon), the user's report of 2026-11-06.

Read: a held item lives in two places. Hands are the AI process's slots (`ActorProcess` 39192 writes +240/+248, called
from Equip 38929 and `InitWornArmor`); worn marks are inventory extra data. `PlayerCharacter::Resurrect` (40491) and a
copy's move have each lost one and kept the other (death.md, "The hands"; copy-hand-spells.md).

Read in code, three gaps a lost weapon would pass through:

- `OnNotifyEquipmentSnapshot` compares worn items and hand *spells*, never the hands' weapons.
- `EquipHook` refuses every equip of the engine's on a copy, so the engine cannot put a weapon back.
- Nothing reads the weapon's 3D. `state.hands` is compared only at a scenario's end, and only the process slot.

Measured, 2026-11-06:

- Party travel, sword sheathed: the copy lost its 4 armour pieces, kept its sword in hand; the engine's outfit equips
  (armour and `13790`, the outfit's axe) were refused; the 3 s reconcile restored the set. **So a move can leave a copy
  stripped for up to 3 s.** The next move, sword drawn, showed no loss, so it does not happen every time.
- Faendal fighting the guest: hands and worn items agree, his bow-to-dagger swap reached the copy.
- **An empty hand was not reproduced.** Neither test could see a missing 3D weapon.

Playtest 2026-11-04, guest log: copy `13485`'s engine tried to equip a weapon about once a second for over 2 minutes in
a fight (350 refused, `1397F`/`1398D`; before them `1399E`, an item it already wore). Trying to equip what it wears
suggests its hand was empty: inferred. That copy's combat tree is what PR #25 refuses, so PR #25 may stop the attempts
and not the cause.

### C. Headtracking

Entry: [Headtracking on copies](bugs.md#headtracking-on-copies). Step 1 done 2026-11-06, what stayed is in the entry; this adds:

- Measured 2026-11-06, Faendal fighting the guest: his copy's slot 1 held himself (`session 18`), his owner's nothing.
  Same class as cause 2, a self-target that turns no head, on a living NPC this time.
- Cause 3 (`FF000820`) belongs to cluster E.
- Kill-move scenarios (`host-kill-move`, `client-kill-move`, `spectator-watches-kill-move`) waited on step 1: all three
  passed both runs after it. Measured.
- `save-load-gate` stopped on a wolf's `head_track` on 2026-11-06, before this fix; on PR #28's build it gets
  past the check and fails later. Either keeps it from answering [Sigurd's registration](bugs.md#sigurds-registration).

### D. Copy lifecycle

Fixed 2026-11-06 ([copy-lifecycle.md](re/systems/copy-lifecycle.md)): the rabbit and Olava, the placed guard, the deleted
NPC crash. Decided the same day (the user): a copy with no owner goes too, as on the host.

- The guard did not share the guess made here: discovery drops him on the host (measured). The guest's reference is
  collected as it follows, so no copy there is right; the check was wrong.
- The crash's cause is read and measured: a placed reference marked deleted keeps its handle with no process.
- The rabbits in past failed runs were placed references, not temporary (`at-failure.json`, base `6DC9D`); which one
  was left with no owner was not recorded.

Open: [Sigurd's registration](bugs.md#sigurds-registration), not reproduced and not shown to share this.

The rest of the town entry (fish `10C3BA`, player heights, the inn's containers) is not lifecycle: it stays there.

### E. Created forms

Entries: headtracking's cause 3,
[References a guest's script creates](bugs.md#references-a-guests-script-creates).

Read in code: `AiStateService` (the head-track target) uses `ToSession`, which has no id for a created form;
`ToSessionOrCreated` (`FormIds.h`) already exists for this. The theft entry is gone (fixed 2026-11-07): read in IDA and
measured, the guest's alarm for a created item names its owner actor, not the item, so it never needed the item's id;
the host's permission recheck stopped it (crime.md, "A created item's theft names its owner"). Script-placed references
are the same gap with a design question on top (whose is the reference).

What a guest makes today (read in code and docs; none of it run for this map):

- Items it drops: synced. The guest makes its own at once, the host makes the real one, the guest's becomes its twin
  (dropped-items.md). So a script could keep its reference and the host still get one: the twin path may fit
  `PlaceAtMe`, and a blanket "guest-only" rule would wrongly hide a mod's visible object.
- **Decided 2026-11-06 (the user): one path.** A reference a guest's script places goes the way a drop does, no second
  mechanism.
- References its own scripts place, today: its game's alone. World scripts run on the host, so this is the guest's body and UI
  scripts, and mods' spells.
- Actors it makes: [A guest's summon](bugs.md#a-guests-summon). Measured: a guest's Conjure Familiar appears on neither
  game, the host's works. Suspect, read: `RunActorDiscovery` deletes a temporary actor on a guest that is no copy and has
  no session id, a rule written for saved NPCs.

One task for all of it, on the drop's path: scripts' references and a guest's actors.

### F. Revive

Entries: [Revive leaves the body wrong](bugs.md#revive-leaves-the-body-wrong), and death.md's open item (a revived player
knocked down stays in knock state 6).

Read in code: `PlayerCharacter::Revive` restores hands, graph flags and ragdoll, and neither the knock state nor the
graph's state machines. All three symptoms follow a death; `guest-knockdown-lies-on-host` passes before one and fails
after. Link inferred, the missing reset not yet read.

### G. Lock pairs

Entries: [The AI/quest lock pair](bugs.md#the-aiquest-lock-pair), and PR #25's report with the host's player dead.

Both turn on `ExtraAliasInstanceArray` (12718-12721). Same lock, different partner: inferred. One rerun answers both.

### H. Graph picks

Entries: [Players' idle variants](bugs.md#players-idle-variants), [A guest arriving mid-loop](bugs.md#a-guest-arriving-mid-loop).

One mechanism, `GraphRandom` sync points (read). Mid-loop is a test of it, players an extension of it. The crab's live
mandible difference above is a pick too: creatures' idle picks may not be covered. Measured once.

## Only the word is shared

- **Ragdolls.** Copy ragdolls at rest is `BodyService` steering (read in the entry); get-up is the owner/copy knock
  handoff; a wolf's slow rest is the host's own body. None runs through Revive.
- **Dialogue.** The queued greeting predates the silent device (PR #6) and may be gone: rerun. The lost press is a
  frame window in the menu (`HookShowList`), the overlap is `ShowPicked`. Separate functions, separate tasks.
- **Vampirism.** Clutter flags and the marker share a scenario family, not a path: inferred, nobody has read the clutter's.
- **Trade markers and Lydia's one-off action.** Both compare the latest action at one instant, and a copy follows 150 to
  300 ms behind. Belethor's copy on `CyclicFreeze` is action 0x13002, an idle Sigrid's copy also showed just before the
  owner's next action. Possibly lag caught by the check: inferred. Rerun before any work.

## Standalone

No link found, each stays one task: creature wobble, Open Animation Replacer's picks, long roll, dropped sword, chest
lid, `SendAnimationEvent`, the werewolf, crafting station exit, follower stops following, scratch save, quest
start/stop events, older crime scenarios, a mod's rename, the baseline after a death, Greybeards validation, an old crash reported as new.

[After the host dies](bugs.md#after-the-host-dies) waits for PR #25, which refuses copies' combat trees and finds the
half-stop is the engine's detection.

## Order

1. Fix the checks first (A, C step 1): done 2026-11-06.
2. Then anything a check was hiding reads true: Sigrid's cancelled idle, the four head-track cases.
3. Done: D's fix clears the town comparisons' missing owners: a copy goes when its owner's game drops the actor.
4. Everything else is independent.
