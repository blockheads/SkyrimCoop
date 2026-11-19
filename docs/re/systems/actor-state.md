# Actor state beside the graph (2026-11-18)

Who writes each bit of `ActorState` (flags1 at `Actor+0xC8`, flags2 at `+0xCC`), and so where a copy gets it
(one-simulator.md, piece 2). Read in IDA 1.7.104 unless marked.

## What an action writes (39004, `coop_ApplyActionGraphEffects`)

Run by `PerformAction` 38949 and `ForceAction` 38953 after the graph took (or refused) the action's event. A copy
on its owner's stream plays no action, so none of this happens on it by itself.

- **Attack state** (flags1 bits 28-31): only its start. 1 (draw), 6 (bash, attack data flag 2), 8 (bow/crossbow,
  weapon types 7 and 9); `0xA -> 0xB` for one event; the `pa_Kill` actions 1.
- **Sneak** (37951 `Actor::SetSneaking`): graph int `iIsInSneak`, the perk entry 69 spell on entering, flags1 bit 9.
- **Sit state** (flags1 bits 14-17) by event: 2, 3 (`HorseEnterInstant`), 4 (`chairExitStart`), 6, 8.
- **Block** (flags2 `wantBlocking` 0x100): set by action 45 (`kActionLeftAttack`) with the block-start event,
  cleared by 47 (`kActionLeftRelease`). No graph handler writes it.
- **Recoil** (flags2 0x400 / 0x800) on the recoil events.
- **Sprint** (65/66): 37948 sets one graph bool and nothing else, so the stream carries it.
- **Moves and turns** (97-105): one graph int each.
- **A kill move** (`pa_Kill...`, strnicmp): 38613 on the actor (no killer) and on its target (killer: the actor).

## What a copy's own graph handlers write

They run on the events a streamed copy's graph raises (graph-input-stream.md):

- attack state after its start: `weaponSwing` 42810 -> 2, `HitFrame` 42828 -> 3 (only from 2), `AttackWinStart`
  42812 -> 4 (13 for bow states), `AttackWinEnd` 42813 -> 5 (14), `attackStop` 42814 -> 0
- recoil cleared: `recoilStop` 42815
- sit state's ends (42868, 42881, 42835), knock state (42879, 42880), weapon state 3/0 (42824, 42825)

## A kill move (38613, 38614)

`Character::EnterKillMove` 38613 sets `kIsInKillMove` (0x4000 at `+0x204`), starts a deferred kill and stores its
killer's handle at `+0x108` (`Actor::killerHandle`). Its only caller in 1.7.104 is 39004 (xrefs): no graph handler
enters a kill move. `EndKillMove` 38614 clears the flag and kills an actor with a recorded killer.

## So: one writer per bit

```text
the record's ActorState part     sneak, life states but dying/dead, wantBlocking,
(ActorStateService)              drawn hands; a kill move for a copy made mid-move
its owner's flags, with each     everything else: attack state's and sit state's start, recoil, and
action on its stream and each    the bits not read yet (movement 0-8, swimming, fly state, staggered,
sync point (FromSnapshot)        allowFlying...), as before the record
the copy's own handlers          the rest of attack, sit and recoil; knock; weapon draw 3/0
the copy's own body              knock state, weapon transition (kept from snapshots)
```

Only the bits read here moved to the part. The others still come with their owner's flags as they always did: who
writes them on a copy is not read (an audit on 2026-11-18 caught a first version that dropped them from actions).

`Games/Animation/ActorStateRules.h` holds the masks; ClientTests `[actor-state]` checks they never overlap.

- A part is applied when the copy's stream playback reaches the owner's frame it was read at, so it never runs ahead
  of the graph it goes with. Off its stream, at once.
- Attack state stays on the stream: a part arriving out of order with the handlers would rewind a swing (a late 1
  over 2, and `HitFrame` 42828's "only from 2" fails). Death stays there too, with its death clip.
- Two copies of one pair enter their kill move at the action on the stream, both in one frame
  (`ReplayState::EntersKillMove`): entered from each one's own part they were two frames apart (`host-kill-move`,
  2026-11-18, each copy has its own playback clock). The part enters one only for a copy that missed the action.
- A kill move's end is the copy's own `KillMoveEnd` (42871) while its pair plays (in an instance 32808, or
  `bIsSynced`). A copy still in one its owner left, with no pair playing, is taken out: its killer handle cleared
  first, then 38614, which queues its kill only with a killer (read in IDA).
- Hands: the owner declares them once settled (drawn or sheathed), never while dead or dying (its hands drop with its
  death, which its copies play later). A copy that shows the other one is put right (`ShowWeaponDrawn` on its
  stream); mid-animation it is looked at again each frame until the animation ends. This replaced a 0.25 s poll.
- Cost of the owner's read (flags, weapon state, name per owned actor, each frame), measured 2026-11-18 with about 80
  wolves on the host: 152 us a frame mean (597 max), of 6 ms of our update; 13 us on the guest.

## Names and factions

Their own parts (rare changes; one part would resend them with every crouch).

- **Name:** the owner reads each of its actors' `GetDisplayFullName` (19781) after each frame. That covers what raises
  nothing: SKSE's `SetDisplayName` (ExtraTextDisplayData written past the alias hooks) and `SetName` on a player's
  base (names.md). A quest alias on the host (24011, 24012) renaming an actor another game owns is sent to that owner
  (`NotifyActorName`), whose part carries it on. The event carries the name as the call left it: by the time it is
  handled, a copy shows its owner's name again.
- **Factions:** declared on `FactionEvents`' hooks (37686, 37688, 37677, 37381, 54735); a copy takes them when they
  arrive and again when its 3D is in.

## A copy's swing target (38603, 38608, 47272)

`HitFrameHandler` 42828 -> 38603 -> `Actor_GetTarget` 38608:

- `ExtraForcedTarget` (0x9D, read by 12037) wins only while `IsInCombat` (38562) is false, and skips cone and reach:
  `DealDamage` at any distance. A copy's `IsInCombat` there is its own answer (false), so a forced target on a copy
  would hit whatever it names. Not used.
- Else `Actor_PickSwingTarget` 47272 with `GetAttackReach` 38538: with `currentCombatTarget` (`+0x104`) set, only that
  actor, in the swing cone (47297) and within reach after both bodies (47273), or none. With none, the nearest live
  actor in the cone, the player included.
- 38603's second chance (`CalculateCurrentHitTargetForWeaponSwing` 38628) keeps a target for an NPC only if it is its
  `+0x104`.

A copy's `+0x104` is its owner's combat target (`SkyrimBridge::SetCopyAiFields`, kept past 37273 by
`HookUpdateCombatSearchState` since 2026-11-02). So by the code, an NPC copy's swing lands only on its owner's
target, within reach on the game where it lands. Measured on the duo (2026-11-18): the host's wolf fighting the guest's
copy, the host's player staged in front of it and nearer (gap -37 against the guest's farther place): on the guest the
wolf copy's `+0x104` read the guest's player and 47272 picked the guest. The 2026-11-06 reading of the nearest
(combat-targets.md) did not come back; bugs.md's "An NPC copy's swing picks the nearest" closed.
