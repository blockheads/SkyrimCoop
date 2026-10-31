# 2026-10-30: lock-order reports with our code on the stack

Two "[ours]" lock-order patterns failed scenarios on 2026-10-30. `guest-cannot-load` hit the first. The second
appeared in `party-changes-cells` and `follower-guest-recruits`. Both are the engine's own lock orders. No deadlock
was seen, and no product change was made for them. Read with `coop_test.py locks` and the plugin log's stacks
(`reporting.symbolized`). Lock IDs are Address Library IDs, and offsets are SkyrimSE.exe 1.6.1170 RVAs.

## Pattern 1: `+0x3cdb45 > +0x1823d0 vs +0x182e91 > +0x3d1507` (guest)

The reporting thread was the VM job, where our update runs (`HookVMUpdate`). The guest applied the host's quest
fills there: `GuestWorldState::OnQuests` called `GuestEngine::ApplyFills`, then `ClearFills` and
`TESQuest::ClearRefAlias` (25051).

1. `ClearRefAlias` takes the quest's alias lock for write (`+0x3cdb45`).
2. It clears the actor's scene (`Actor::SetCurrentScene` 37248), which asks for a new package (`CheckForNewPackage`
   39114).
3. That read-locks the actor's alias array (`ExtraAliasInstanceArray` 12721, `+0x1823d0`).

The other order came from the join's save load on the main thread. `TESObjectREFR::FinishLoadGame` (19531) runs
12728, which write-locks the alias array (`+0x182e91`) and then reads the quest (25066, `+0x3d1507`).

A variant was reported in the v1-copy-desync worktree the same day, `+0x3cdb45 > +0x182241 vs +0x1823d0 > +0x3d1507`.
Here `ClearRefAlias` takes the actor's alias array for write (12719) through `BGSRefAlias` 24012. Against it, the
main thread's `coc` initialized a quest alias (25083, `InitializeRefAlias` 24011), picked a package and judged a
`GetIsAliasRef` condition. That holds the alias array for read and asks for the quest.

Both are the order of `ClearRefAlias` itself: quest first, then the actor's aliases. A Papyrus `Alias.Clear()` takes
the same path on the same VM job. The opposite orders come from a load, which runs on the main thread between frames
while the job threads wait, or from the harness's console. The harness runs queued commands in `Main::Update` after
the actor and VM jobs start (`2026-10-29-synchronous-save-phase.md`). A player's console pauses the game.

Left as is. Applying fills from the main loop's tasks (`MainLoopTasks`) would keep them off the job threads. It is
recorded in `docs/ideas.md`, not built: no deadlock was seen, and vanilla scripts already do the same.

## Pattern 2: `+0x17a479 > +0xe82682 vs +0xe85437 > +0x16b61d` (and `> +0x17928d`)

On the main thread, a cell change (`TES::SetCurrentCell` 13316) detaches a reference's Havok (`DetachHavok` 19717).
That removes extra data under the extra list's write lock (12355) and then takes the physics world's lock (77803).
The physics job does the reverse: `bhkWorld` 77851 holds the world, a trigger's enter event (54001) gets a handle
and reads the reference's extra data (12329, 12030).

Our only frames were pass-through hooks (`HookProcessQueuedTravel`, the main loop's), so this is the engine's own
order.

## Mislabelled "[ours]": pass-through hooks

On 2026-10-30 the duo's boot alone reported 14 "[ours]" patterns. Each was an AI job thread evaluating a package's
conditions (12721, then 25066, 15ff00, 17928d, 161ef7). Our frames were `HookUpdateAI` (37347) and
`HookCheckForNewPackage` (39114). For an actor that is not a copy, both only call the engine's function.

The checker counts any frame of ours below an engine frame as ours, unless it is in `LockOrder::IgnoreFrames`. Both
hooks now keep their copy work in their own non-inlined function (`UpdateCopyAI`, `KeepCopyPackage`), and their own
code is ignored. The byte sizes come from the PDB (110 and 77 bytes; the ranges run to the next 16-byte boundary).
`HookIsTrue` was already handled this way. The ignore table now holds 8 entries.
